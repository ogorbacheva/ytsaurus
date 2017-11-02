#include "requests.h"

#include "retry_request.h"

#include <mapreduce/yt/client/transaction.h>

#include <mapreduce/yt/common/abortable_registry.h>
#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/helpers.h>
#include <mapreduce/yt/common/log.h>
#include <mapreduce/yt/common/node_builder.h>
#include <mapreduce/yt/common/serialize.h>
#include <mapreduce/yt/common/wait_proxy.h>

#include <mapreduce/yt/interface/errors.h>

#include <library/json/json_reader.h>

#include <util/random//normal.h>
#include <util/stream/file.h>
#include <util/string/printf.h>
#include <util/generic/buffer.h>
#include <util/generic/ymath.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

bool operator==(const TAuth& lhs, const TAuth& rhs)
{
    return lhs.ServerName == rhs.ServerName &&
           lhs.Token == rhs.Token;
}

bool operator!=(const TAuth& lhs, const TAuth& rhs)
{
    return !(rhs == lhs);
}

////////////////////////////////////////////////////////////////////////////////

static TString GetDefaultTransactionTitle(const TProcessState& processState)
{
    TStringStream res;

    res << "User transaction. Created by: " << processState.UserName << " on " << processState.HostName
        << " client: " << processState.ClientVersion << " pid: " << processState.Pid;
    if (!processState.CommandLine.empty()) {
        res << " command line:";
        for (const auto& arg : processState.CommandLine) {
            res << ' ' << arg;
        }
    } else {
        res << " command line is unknown probably NYT::Initialize was never called";
    }
    return res.Str();
}

////////////////////////////////////////////////////////////////////////////////

bool ParseBoolFromResponse(const TString& response)
{
    return GetBool(NodeFromYsonString(response));
}

TGUID ParseGuidFromResponse(const TString& response)
{
    auto node = NodeFromYsonString(response);
    return GetGuid(node.AsString());
}

void ParseJsonStringArray(const TString& response, yvector<TString>& result)
{
    NJson::TJsonValue value;
    TStringInput input(response);
    NJson::ReadJsonTree(&input, &value);

    const NJson::TJsonValue::TArray& array = value.GetArray();
    result.clear();
    result.reserve(array.size());
    for (size_t i = 0; i < array.size(); ++i) {
        result.push_back(array[i].GetString());
    }
}

TRichYPath CanonizePath(const TAuth& auth, const TRichYPath& path)
{
    TRichYPath result;
    if (path.Path_.find_first_of("<>{}[]") != TString::npos) {
        THttpHeader header("GET", "parse_ypath");
        auto pathNode = PathToNode(path);
        header.SetParameters(TNode()("path", pathNode));
        auto response = NodeFromYsonString(RetryRequest(auth, header));
        for (const auto& item : pathNode.GetAttributes().AsMap()) {
            response.Attributes()[item.first] = item.second;
        }
        Deserialize(result, response);
    } else {
        result = path;
    }
    result.Path_ = AddPathPrefix(result.Path_);
    return result;
}

yvector<TRichYPath> CanonizePaths(const TAuth& auth, const yvector<TRichYPath>& paths)
{
    yvector<TRichYPath> result;
    for (const auto& path : paths) {
        result.push_back(CanonizePath(auth, path));
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

TTransactionId StartTransaction(
    const TAuth& auth,
    const TTransactionId& parentId,
    const TMaybe<TDuration>& timeout,
    bool pingAncestors,
    const TMaybe<TString>& title,
    const TMaybe<TNode>& maybeAttributes)
{
    THttpHeader header("POST", "start_tx");
    header.AddTransactionId(parentId);

    header.AddMutationId();
    header.AddParam("timeout",
        (timeout ? timeout : TConfig::Get()->TxTimeout)->MilliSeconds());
    if (pingAncestors) {
        header.AddParam("ping_ancestor_transactions", "true");
    }

    if (maybeAttributes && !maybeAttributes->IsMap()) {
        ythrow TApiUsageError() << "Attributes must be a Map node";
    }
    TNode attributes = maybeAttributes ? *maybeAttributes : TNode::CreateMap();

    if (title) {
        attributes["title"] = *title;
    } else if (!attributes.HasKey("title")) {
        attributes["title"] = GetDefaultTransactionTitle(*TProcessState::Get());
    }

    header.SetParameters(AttributesToYsonString(attributes));

    auto txId = ParseGuidFromResponse(RetryRequest(auth, header));
    LOG_INFO("Transaction %s started", ~GetGuidAsString(txId));
    return txId;
}

void TransactionRequest(
    const TAuth& auth,
    const TString& command,
    const TTransactionId& transactionId)
{
    THttpHeader header("POST", command);
    header.AddTransactionId(transactionId);
    header.AddMutationId();
    RetryRequest(auth, header, "", false, false);
}

void PingTransaction(
    const TAuth& auth,
    const TTransactionId& transactionId)
{
    try {
        TransactionRequest(auth, "ping_tx", transactionId);
    } catch (yexception&) {
        // ignore all ping errors
    }
}

void AbortTransaction(
    const TAuth& auth,
    const TTransactionId& transactionId)
{
    TransactionRequest(auth, "abort_tx", transactionId);

    LOG_INFO("Transaction %s aborted", ~GetGuidAsString(transactionId));
}

void CommitTransaction(
    const TAuth& auth,
    const TTransactionId& transactionId)
{
    TransactionRequest(auth, "commit_tx", transactionId);

    LOG_INFO("Transaction %s commited", ~GetGuidAsString(transactionId));
}

////////////////////////////////////////////////////////////////////////////////

TString GetProxyForHeavyRequest(const TAuth& auth)
{
    if (!TConfig::Get()->UseHosts) {
        return auth.ServerName;
    }

    yvector<TString> hosts;
    THttpHeader header("GET", TConfig::Get()->Hosts, false);
    TString response = RetryRequest(auth, header);
    ParseJsonStringArray(response, hosts);
    if (hosts.empty()) {
        ythrow yexception() << "returned list of proxies is empty";
    }

    if (hosts.size() < 3) {
        return hosts.front();
    }
    size_t hostIdx = -1;
    do {
        hostIdx = Abs<double>(NormalRandom<double>(0, hosts.size() / 2));
    } while (hostIdx >= hosts.size());

    return hosts[hostIdx];
}

TString RetryRequest(
    const TAuth& auth,
    THttpHeader& header,
    const TString& body,
    bool isHeavy,
    bool isOperation)
{
    int retryCount = isOperation ?
        TConfig::Get()->StartOperationRetryCount :
        TConfig::Get()->RetryCount;

    header.SetToken(auth.Token);

    TDuration socketTimeout = (header.GetCommand() == "ping_tx") ?
        TConfig::Get()->PingTimeout : TDuration::Zero();

    bool needMutationId = false;
    bool needRetry = false;

    for (int attempt = 0; attempt < retryCount; ++attempt) {
        bool hasError = false;
        TString response;
        TString requestId;
        TDuration retryInterval;

        try {
            TString hostName = auth.ServerName;
            if (isHeavy) {
                hostName = GetProxyForHeavyRequest(auth);
            }

            THttpRequest request(hostName);
            requestId = request.GetRequestId();

            if (needMutationId) {
                header.AddMutationId();
                needMutationId = false;
                needRetry = false;
            }

            if (needRetry) {
                header.AddParam("retry", "true");
            } else {
                header.RemoveParam("retry");
                needRetry = true;
            }

            request.Connect(socketTimeout);
            try {
                IOutputStream* output = request.StartRequest(header);
                output->Write(body);
                request.FinishRequest();
            } catch (yexception&) {
                // try to read error in response
            }
            response = request.GetResponse();
        } catch (TErrorResponse& e) {
            LOG_ERROR("RSP %s - attempt %d failed",
                ~requestId,
                attempt);

            if (!NDetail::IsRetriable(e) || attempt + 1 == retryCount) {
                throw;
            }
            if (e.IsConcurrentOperationsLimitReached()) {
                needMutationId = true;
            }

            hasError = true;
            retryInterval = NDetail::GetRetryInterval(e);
        } catch (yexception& e) {
            LOG_ERROR("RSP %s - %s - attempt %d failed",
                ~requestId,
                e.what(),
                attempt);

            if (attempt + 1 == retryCount) {
                throw;
            }
            hasError = true;
            retryInterval = TConfig::Get()->RetryInterval;
        }

        if (!hasError) {
            return response;
        }

        NDetail::TWaitProxy::Sleep(retryInterval);
    }

    ythrow yexception() << "unreachable";
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
