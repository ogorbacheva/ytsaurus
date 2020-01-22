#pragma once

#include <mapreduce/yt/http/retry_request.h>

#include <mapreduce/yt/client/client.h>

#include <mapreduce/yt/common/wait_proxy.h>
#include <mapreduce/yt/common/retry_lib.h>

#include <mapreduce/yt/interface/logging/log.h>

namespace NYT::NDetail {

template <typename TResult>
TResult RetryTransactionWithPolicy(
    const IClientBasePtr& client,
    std::function<TResult(ITransactionPtr)> func,
    IRequestRetryPolicyPtr retryPolicy)
{
    if (!retryPolicy) {
        retryPolicy = CreateDefaultRequestRetryPolicy();
    }

    while (true) {
        try {
            retryPolicy->NotifyNewAttempt();
            auto transaction = client->StartTransaction(TStartTransactionOptions());
            if constexpr (std::is_same<TResult, void>::value) {
                func(transaction);
                transaction->Commit();
                return;
            } else {
                auto result = func(transaction);
                transaction->Commit();
                return result;
            }
        } catch (const TErrorResponse& e) {
            LOG_ERROR("Retry failed %s - %s", e.GetError().GetMessage().data(),
                retryPolicy->GetAttemptDescription().data());
            if (!IsRetriable(e)) {
                throw;
            }

            auto maybeRetryTimeout = retryPolicy->OnRetriableError(e);
            if (maybeRetryTimeout) {
                TWaitProxy::Get()->Sleep(*maybeRetryTimeout);
            } else {
                throw;
            }
        } catch (const yexception& e) {
            LOG_ERROR("Retry failed %s - %s", e.what(), retryPolicy->GetAttemptDescription().data());
            if (!IsRetriable(e)) {
                throw;
            }

            auto maybeRetryTimeout = retryPolicy->OnGenericError(e);
            if (maybeRetryTimeout) {
                TWaitProxy::Get()->Sleep(*maybeRetryTimeout);
            } else {
                throw;
            }
        }
    }
}

} // namespace NYT::NDetail
