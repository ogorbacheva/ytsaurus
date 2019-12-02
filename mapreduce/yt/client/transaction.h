#pragma once

#include <mapreduce/yt/common/abortable_registry.h>
#include <mapreduce/yt/http/requests.h>
#include <mapreduce/yt/http/retry_request.h>

#include <util/datetime/base.h>
#include <util/generic/maybe.h>
#include <util/generic/ptr.h>
#include <util/system/thread.h>

#include <atomic>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TPingableTransaction
{
public:
    //
    // Start a new transaction.
    TPingableTransaction(
        const TAuth& auth,
        const TTransactionId& parentId,
        const TStartTransactionOptions& options);

    //
    // Attach to an existing transaction.
    TPingableTransaction(
        const TAuth& auth,
        const TTransactionId& transactionId,
        const TAttachTransactionOptions& options);

    ~TPingableTransaction();

    const TTransactionId GetId() const;

    void Commit();
    void Abort();
    void Detach();

private:
    enum class EStopAction
    {
        Detach,
        Abort,
        Commit,
    };

private:
    TAuth Auth_;
    TTransactionId TransactionId_;
    TDuration MinPingInterval_;
    TDuration MaxPingInterval_;

    // We have to own an IntrusivePtr to registry to prevent use-after-free.
    ::TIntrusivePtr<NDetail::TAbortableRegistry> AbortableRegistry_;

    bool AbortOnTermination_;

    std::atomic<bool> Running_{false};
    THolder<TThread> Thread_;

private:
    void Init(
        const TAuth& auth,
        const TTransactionId& transactionId,
        TDuration timeout,
        bool autoPingable);

    void Stop(EStopAction action);

    void Pinger();
    static void* Pinger(void* opaque);
};

////////////////////////////////////////////////////////////////////////////////

class TPingRetryPolicy
    :   public IRequestRetryPolicy
{
public:
    TPingRetryPolicy(ui32 attemptCount = 0);

    void NotifyNewAttempt() override;
    TMaybe<TDuration> OnGenericError(const yexception& e) override;
    TMaybe<TDuration> OnRetriableError(const TErrorResponse& e) override;
    void OnIgnoredError(const TErrorResponse& e) override;
    TString GetAttemptDescription() const override;

private:
    ui32 AttemptCount_;
    ui32 Attempt_ = 0;
};

////////////////////////////////////////////////////////////////////////////////

TYPath Snapshot(
    const IClientRetryPolicyPtr& clientRetryPolicy,
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
