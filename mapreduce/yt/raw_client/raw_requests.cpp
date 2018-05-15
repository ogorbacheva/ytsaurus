#include "raw_requests.h"

#include "raw_batch_request.h"
#include "rpc_parameters_serialization.h"

#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/helpers.h>
#include <mapreduce/yt/common/finally_guard.h>
#include <mapreduce/yt/http/retry_request.h>
#include <mapreduce/yt/node/node_io.h>

namespace NYT {
namespace NDetail {

///////////////////////////////////////////////////////////////////////////////

void ExecuteBatch(
    const TAuth& auth,
    TRawBatchRequest& batchRequest,
    const TExecuteBatchOptions& options,
    IRetryPolicy* retryPolicy)
{
    if (batchRequest.IsExecuted()) {
        ythrow yexception() << "Cannot execute batch request since it is already executed";
    }
    NDetail::TFinallyGuard g([&] {
        batchRequest.MarkExecuted();
    });

    const auto concurrency = options.Concurrency_.GetOrElse(50);
    const auto batchPartMaxSize = options.BatchPartMaxSize_.GetOrElse(concurrency * 5);

    TAttemptLimitedRetryPolicy defaultRetryPolicy(TConfig::Get()->RetryCount);
    if (!retryPolicy) {
        retryPolicy = &defaultRetryPolicy;
    }

    while (batchRequest.BatchSize()) {
        NDetail::TRawBatchRequest retryBatch;

        while (batchRequest.BatchSize()) {
            auto parameters = TNode::CreateMap();
            TInstant nextTry;
            batchRequest.FillParameterList(batchPartMaxSize, &parameters["requests"], &nextTry);
            if (nextTry) {
                SleepUntil(nextTry);
            }
            parameters["concurrency"] = concurrency;
            auto body = NodeToYsonString(parameters);
            THttpHeader header("POST", "execute_batch");
            header.AddMutationId();
            NDetail::TResponseInfo result;
            try {
                result = RetryRequestWithPolicy(auth, header, body, retryPolicy);
            } catch (const yexception& e) {
                batchRequest.SetErrorResult(std::current_exception());
                retryBatch.SetErrorResult(std::current_exception());
                throw;
            }
            batchRequest.ParseResponse(std::move(result), *retryPolicy, &retryBatch);
        }

        batchRequest = std::move(retryBatch);
    }
}

TNode Get(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    const TGetOptions& options)
{
    THttpHeader header("GET", "get");
    header.MergeParameters(NDetail::SerializeParamsForGet(transactionId, path, options));
    return NodeFromYsonString(RetryRequest(auth, header));
}

void Set(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    const TNode& value,
    const TSetOptions& options)
{
    THttpHeader header("PUT", "set");
    header.AddMutationId();
    header.MergeParameters(NDetail::SerializeParamsForSet(transactionId, path, options));
    auto body = NodeToYsonString(value);
    RetryRequest(auth, header, TStringBuf(body));
}

bool Exists(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path)
{
    THttpHeader header("GET", "exists");
    header.MergeParameters(NDetail::SerializeParamsForExists(transactionId, path));
    return ParseBoolFromResponse(RetryRequest(auth, header));
}

TNodeId Create(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    const ENodeType& type,
    const TCreateOptions& options)
{
    THttpHeader header("POST", "create");
    header.AddMutationId();
    header.MergeParameters(NDetail::SerializeParamsForCreate(transactionId, path, type, options));
    return ParseGuidFromResponse(RetryRequest(auth, header));
}

void Remove(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    const TRemoveOptions& options)
{
    THttpHeader header("POST", "remove");
    header.AddMutationId();
    header.MergeParameters(NDetail::SerializeParamsForRemove(transactionId, path, options));
    RetryRequest(auth, header);
}

TNode::TListType List(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    const TListOptions& options)
{
    THttpHeader header("GET", "list");

    TYPath updatedPath = AddPathPrefix(path);
    // Translate "//" to "/"
    // Translate "//some/constom/prefix/from/config/" to "//some/constom/prefix/from/config"
    if (path.empty() && updatedPath.EndsWith('/')) {
        updatedPath.pop_back();
    }
    header.MergeParameters(NDetail::SerializeParamsForList(transactionId, updatedPath, options));
    return NodeFromYsonString(RetryRequest(auth, header)).AsList();
}

TNodeId Link(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& targetPath,
    const TYPath& linkPath,
    const TLinkOptions& options)
{
    THttpHeader header("POST", "link");
    header.AddMutationId();
    header.MergeParameters(NDetail::SerializeParamsForLink(transactionId, targetPath, linkPath, options));
    return ParseGuidFromResponse(RetryRequest(auth, header));
}

TLockId Lock(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    ELockMode mode,
    const TLockOptions& options)
{
    THttpHeader header("POST", "lock");
    header.AddMutationId();
    header.MergeParameters(NDetail::SerializeParamsForLock(transactionId, path, mode, options));
    return ParseGuidFromResponse(RetryRequest(auth, header));
}

TNode GetOperation(
    const TAuth& auth,
    const TOperationId& operationId,
    const TGetOperationOptions& options)
{
    THttpHeader header("GET", "get_operation");
    header.MergeParameters(NDetail::SerializeParamsForGetOperation(operationId, options));
    return NodeFromYsonString(RetryRequest(auth, header));
}

TNode ListJobs(
    const TAuth& auth,
    const TOperationId& operationId,
    const TListJobsOptions& options)
{
    THttpHeader header("GET", "list_jobs");
    header.MergeParameters(NDetail::SerializeParamsForListJobs(operationId, options));
    return NodeFromYsonString(RetryRequest(auth, header));
}

TString GetJobStderr(
    const TAuth& auth,
    const TOperationId& operationId,
    const TJobId& jobId,
    const TGetJobStderrOptions& /* options */)
{
    auto heavyProxy = GetProxyForHeavyRequest(auth);
    TAuth authForHeavyRequest{heavyProxy, auth.Token};

    THttpHeader header("GET", "get_job_stderr");
    header.AddOperationId(operationId);
    header.AddParameter("job_id", GetGuidAsString(jobId));
    return RetryRequest(authForHeavyRequest, header);
}

TMaybe<TYPath> GetFileFromCache(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TString& md5Signature,
    const TYPath& cachePath,
    const TGetFileFromCacheOptions& /* options */,
    IRetryPolicy* retryPolicy)
{
    THttpHeader header("GET", "get_file_from_cache");
    header.AddTransactionId(transactionId);
    header.AddParameter("md5", md5Signature);
    header.AddParameter("cache_path", cachePath);

    auto responseInfo = RetryRequestWithPolicy(auth, header, "", retryPolicy);
    auto path = NodeFromYsonString(responseInfo.Response).AsString();
    return path.Empty() ? Nothing() : TMaybe<TYPath>(path);
}

TYPath PutFileToCache(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& filePath,
    const TString& md5Signature,
    const TYPath& cachePath,
    const TPutFileToCacheOptions& /* options */,
    IRetryPolicy* retryPolicy)
{
    THttpHeader header("POST", "put_file_to_cache");
    header.AddTransactionId(transactionId);
    header.AddPath(filePath);
    header.AddParameter("md5", md5Signature);
    header.AddParameter("cache_path", cachePath);

    auto result = RetryRequestWithPolicy(auth, header, "", retryPolicy);
    return NodeFromYsonString(result.Response).AsString();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail
} // namespace NYT
