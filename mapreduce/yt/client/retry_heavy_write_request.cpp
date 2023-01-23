#include "retry_heavy_write_request.h"

#include "transaction.h"
#include "transaction_pinger.h"

#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/retry_lib.h>
#include <mapreduce/yt/common/wait_proxy.h>

#include <mapreduce/yt/interface/logging/yt_log.h>

#include <mapreduce/yt/http/requests.h>
#include <mapreduce/yt/http/retry_request.h>

namespace NYT {

using ::ToString;

////////////////////////////////////////////////////////////////////////////////

void RetryHeavyWriteRequest(
    const IClientRetryPolicyPtr& clientRetryPolicy,
    const ITransactionPingerPtr& transactionPinger,
    const TAuth& auth,
    const TTransactionId& parentId,
    THttpHeader& header,
    std::function<THolder<IInputStream>()> streamMaker)
{
    int retryCount = TConfig::Get()->RetryCount;
    header.SetToken(auth.Token);

    for (int attempt = 0; attempt < retryCount; ++attempt) {
        TPingableTransaction attemptTx(clientRetryPolicy, auth, parentId, transactionPinger->GetChildTxPinger(), TStartTransactionOptions());

        auto input = streamMaker();
        TString requestId;

        try {
            auto proxyName = GetProxyForHeavyRequest(auth);
            THttpRequest request;
            requestId = request.GetRequestId();

            header.AddTransactionId(attemptTx.GetId(), /* overwrite = */ true);
            header.SetRequestCompression(ToString(TConfig::Get()->ContentEncoding));

            request.Connect(proxyName);

            IOutputStream* output = request.StartRequest(header);
            TransferData(input.Get(), output);
            request.FinishRequest();
            request.GetResponse();
        } catch (TErrorResponse& e) {
            YT_LOG_ERROR("RSP %v - attempt %v failed",
                requestId,
                attempt);

            if (!IsRetriable(e) || attempt + 1 == retryCount) {
                throw;
            }
            NDetail::TWaitProxy::Get()->Sleep(GetBackoffDuration(e));
            continue;

        } catch (yexception& e) {
            YT_LOG_ERROR("RSP %v - %v - attempt %v failed",
                requestId,
                e.what(),
                attempt);

            if (attempt + 1 == retryCount) {
                throw;
            }
            NDetail::TWaitProxy::Get()->Sleep(GetBackoffDuration(e));
            continue;
        }

        attemptTx.Commit();
        return;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
