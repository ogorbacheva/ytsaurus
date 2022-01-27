#include "election_manager.h"

#include "private.h"
#include "config.h"

#include <yt/yt/client/api/transaction.h>

#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/action_queue.h>

#include <yt/yt/core/misc/atomic_object.h>

namespace NYT::NCypressElection {

using namespace NApi;
using namespace NTransactionClient;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NLogging;
using namespace NObjectClient;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

class TCypressElectionManager
    : public ICypressElectionManager
{
public:
    TCypressElectionManager(
        IClientPtr client,
        IInvokerPtr invoker,
        TCypressElectionManagerConfigPtr config,
        TCypressElectionManagerOptionsPtr options)
        : Config_(std::move(config))
        , Options_(std::move(options))
        , Client_(std::move(client))
        , Invoker_(CreateSerializedInvoker(std::move(invoker)))
        , Logger(CypressElectionLogger.WithTag("GroupName: %v, Path: %v", Options_->GroupName, Config_->LockPath))
        , LockAquisitionExecutor_(New<TPeriodicExecutor>(
            Invoker_,
            BIND(&TCypressElectionManager::TryAcquireLock, MakeWeak(this)),
            Config_->LockAcquisitionPeriod))
    { }

    void Start() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        YT_LOG_DEBUG("Starting cypress election manager");

        LockAquisitionExecutor_->Start();
    }

    TFuture<void> Stop() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        YT_LOG_DEBUG("Stopping cypress election manager");

        return BIND(&TCypressElectionManager::DoStop, MakeWeak(this))
            .AsyncVia(Invoker_)
            .Run();
    }

    TFuture<void> StopLeading() override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        YT_LOG_DEBUG("Stopping leading");

        return BIND(&TCypressElectionManager::DoStopLeading, MakeWeak(this))
            .AsyncVia(Invoker_)
            .Run();
    }

    TTransactionId GetPrerequisiteTransactionId() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return PrerequisiteTransactionId_.Load();
    }

    bool IsLeader() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return GetPrerequisiteTransactionId() != NullTransactionId;
    }

    DEFINE_SIGNAL_OVERRIDE(void(), LeadingStarted);
    DEFINE_SIGNAL_OVERRIDE(void(), LeadingEnded);

private:
    const TCypressElectionManagerConfigPtr Config_;
    const TCypressElectionManagerOptionsPtr Options_;
    const IClientPtr Client_;
    const IInvokerPtr Invoker_;
    const TLogger Logger;

    const TPeriodicExecutorPtr LockAquisitionExecutor_;

    TObjectId LockNodeId_ = NullObjectId;

    TAtomicObject<TTransactionId> PrerequisiteTransactionId_ = NullTransactionId;

    ITransactionPtr Transaction_;
    TObjectId LockId_ = NullObjectId;

    void TryAcquireLock()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);

        if (IsLeader()) {
            return;
        }

        try {
            if (!LockNodeId_) {
                YT_LOG_DEBUG("Creating lock node");

                CreateLockNode();

                YT_LOG_DEBUG("Lock node created (LockNodeId: %v)",
                    LockNodeId_);
            }

            if (!Transaction_) {
                YT_LOG_DEBUG("Starting transaction");

                StartTransaction();

                YT_LOG_DEBUG("Transaction started (TransactionId: %v)",
                    Transaction_->GetId());
            }

            if (!LockId_) {
                YT_LOG_DEBUG("Creating lock (TransactionId: %v)",
                    Transaction_->GetId());

                CreateLock();

                YT_LOG_DEBUG("Lock created (TransactionId: %v, LockId: %v)",
                    Transaction_->GetId(),
                    LockId_);
            }

            if (CheckLockAcquired()) {
                YT_LOG_DEBUG("Lock is acquired, starting leading (LockId: %v)",
                    LockId_);

                OnLeadingStarted();
            } else {
                YT_LOG_DEBUG("Lock is not acquired yet, skipping (LockId: %v)",
                    LockId_);
            }
        } catch (const std::exception& ex) {
            YT_LOG_INFO(ex, "Lock acquisition iteration failed");
        }
    }

    void StartTransaction()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);
        YT_VERIFY(!Transaction_);
        YT_VERIFY(!IsLeader());

        auto attributes = Options_->TransactionAttributes
            ? Options_->TransactionAttributes->Clone()
            : CreateEphemeralAttributes();
        auto title = Format("Lock tranaction for %v:%v",
            Options_->GroupName,
            Options_->MemberName);
        attributes->Set("title", std::move(title));
        TTransactionStartOptions options {
            .Timeout = Config_->TransactionTimeout,
            .PingPeriod = Config_->TransactionPingPeriod,
            .Attributes = std::move(attributes),
        };
        Transaction_ = WaitFor(
            Client_->StartTransaction(ETransactionType::Master, std::move(options)))
            .ValueOrThrow();

        auto transactionId = Transaction_->GetId();
        Transaction_->SubscribeAborted(
            BIND(&TCypressElectionManager::OnTransactionAborted, MakeWeak(this), transactionId)
                .Via(Invoker_));
        Transaction_->SubscribeCommitted(
            BIND(&TCypressElectionManager::OnTransactionCommitted, MakeWeak(this), transactionId)
                .Via(Invoker_));
    }

    void CreateLock()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);
        YT_VERIFY(!LockId_);
        YT_VERIFY(!IsLeader());

        TLockNodeOptions options;
        options.TransactionId = Transaction_->GetId(),
        options.Waitable = true;
        auto rspOrError = WaitFor(
            Client_->LockNode(FromObjectId(LockNodeId_), ELockMode::Exclusive, std::move(options)));
        if (rspOrError.IsOK()) {
            LockId_ = rspOrError.Value().LockId;
        } else {
            // NB: If transaction has created lock, but response was lost creating a new lock
            // will end up with a conflict, so it's safer to create a new transaction in case
            // of any errors.
            YT_LOG_DEBUG(rspOrError, "Failed to create lock (TransactionId: %v)",
                Transaction_->GetId());
            LockNodeId_ = NullObjectId;
            Transaction_.Reset();
            rspOrError.ThrowOnError();
        }
    }

    bool CheckLockAcquired()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);
        YT_VERIFY(!IsLeader());

        TGetNodeOptions options{
            .Attributes = std::vector<TString>({"state"})
        };
        auto rspOrError = WaitFor(Client_->GetNode(FromObjectId(LockId_), std::move(options)));
        if (rspOrError.IsOK()) {
            auto response = ConvertTo<INodePtr>(rspOrError.Value());
            auto lockState = response->Attributes().Get<ELockState>("state");
            return lockState == ELockState::Acquired;
        } else if (rspOrError.FindMatching(NYTree::EErrorCode::ResolveError)) {
            YT_LOG_DEBUG(rspOrError, "Lock does not exist (LockId: %v)",
                LockId_);
            Transaction_.Reset();
            LockId_ = NullObjectId;
            return false;
        } else {
            rspOrError.ThrowOnError();
        }

        YT_ABORT();
    }

    void OnTransactionAborted(TTransactionId transactionId, const TError& error)
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);

        YT_LOG_DEBUG(error, "Transaction aborted (TransactionId: %v)",
            transactionId);

        OnTransactionFinished(transactionId);
    }

    void OnTransactionCommitted(TTransactionId transactionId)
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);

        YT_LOG_DEBUG("Transacton committed (TransactionId: %v)",
            transactionId);

        OnTransactionFinished(transactionId);
    }

    void OnTransactionFinished(TTransactionId transactionId)
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);

        // NB: Stale callbacks are possible.
        if (!Transaction_ || Transaction_->GetId() != transactionId) {
            return;
        }

        Reset();
    }

    void OnLeadingStarted()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);
        YT_VERIFY(!IsLeader());

        PrerequisiteTransactionId_.Store(Transaction_->GetId());

        YT_LOG_DEBUG("Leading started");

        try {
            TForbidContextSwitchGuard guard;
            LeadingStarted_.Fire();
        } catch (const std::exception& ex) {
            YT_LOG_ALERT(ex, "Unexpected error occured during leading start");
        }
    }

    void DoStop()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);

        WaitFor(LockAquisitionExecutor_->Stop())
            .ThrowOnError();

        Reset();

        YT_LOG_DEBUG("Election manager stopped");
    }

    void DoStopLeading()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);

        if (IsLeader()) {
            Reset();
        }
    }

    void Reset()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);

        if (IsLeader()) {
            YT_LOG_DEBUG("Leading ended");

            PrerequisiteTransactionId_.Store(NullTransactionId);

            try {
                TForbidContextSwitchGuard guard;
                LeadingEnded_.Fire();
            } catch (const std::exception& ex) {
                YT_LOG_ALERT(ex, "Unexpected error occured during leading end");
            }
        }

        Transaction_.Reset();
        LockId_ = NullObjectId;
    }

    void CreateLockNode()
    {
        VERIFY_INVOKER_AFFINITY(Invoker_);
        YT_VERIFY(!LockNodeId_);

        TCreateNodeOptions options;
        options.IgnoreExisting = true;
        options.IgnoreTypeMismatch = true;
        LockNodeId_ = WaitFor(
            Client_->CreateNode(Config_->LockPath, EObjectType::MapNode, std::move(options)))
            .ValueOrThrow();
    }
};

////////////////////////////////////////////////////////////////////////////////

ICypressElectionManagerPtr CreateCypressElectionManager(
    IClientPtr client,
    IInvokerPtr invoker,
    TCypressElectionManagerConfigPtr config,
    TCypressElectionManagerOptionsPtr options)
{
    return New<TCypressElectionManager>(
        std::move(client),
        std::move(invoker),
        std::move(config),
        std::move(options));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressElection
