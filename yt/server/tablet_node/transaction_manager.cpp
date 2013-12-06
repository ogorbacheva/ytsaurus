#include "stdafx.h"
#include "transaction_manager.h"
#include "transaction.h"
#include "config.h"
#include "automaton.h"
#include "tablet_slot.h"
#include "private.h"

#include <core/misc/lease_manager.h>

#include <core/concurrency/thread_affinity.h>

#include <ytlib/tablet_client/tablet_service.pb.h>

#include <server/hydra/hydra_manager.h>
#include <server/hydra/mutation.h>

#include <server/hive/transaction_supervisor.h>

namespace NYT {
namespace NTabletNode {

using namespace NTransactionClient;
using namespace NHydra;
using namespace NCellNode;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = TabletNodeLogger;

////////////////////////////////////////////////////////////////////////////////

class TTransactionManager::TImpl
    : public TTabletAutomatonPart
{
    DEFINE_SIGNAL(void(TTransaction*), TransactionStarted);
    DEFINE_SIGNAL(void(TTransaction*), TransactionPrepared);
    DEFINE_SIGNAL(void(TTransaction*), TransactionCommitted);
    DEFINE_SIGNAL(void(TTransaction*), TransactionAborted);

public:
    TImpl(
        TTransactionManagerConfigPtr config,
        TTabletSlot* slot,
        NCellNode::TBootstrap* bootstrap)
        : TTabletAutomatonPart(
            slot,
            bootstrap)
        , Config_(config)
    {
        VERIFY_INVOKER_AFFINITY(Slot_->GetAutomatonInvoker(), AutomatonThread);

        Slot_->GetAutomaton()->RegisterPart(this);

        RegisterLoader(
            "TransactionManager.Keys",
            BIND(&TImpl::LoadKeys, MakeStrong(this)));
        RegisterLoader(
            "TransactionManager.Values",
            BIND(&TImpl::LoadValues, MakeStrong(this)));

        RegisterSaver(
            ESerializationPriority::Keys,
            "TransactionManager.Keys",
            BIND(&TImpl::SaveKeys, MakeStrong(this)));
        RegisterSaver(
            ESerializationPriority::Values,
            "TransactionManager.Values",
            BIND(&TImpl::SaveValues, MakeStrong(this)));
    }


    TTransaction* GetTransactionOrThrow(const TTransactionId& id)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);
        auto* transaction = FindTransaction(id);
        if (!transaction) {
            THROW_ERROR_EXCEPTION(
                NYTree::EErrorCode::ResolveError,
                "No such transction %s",
                ~ToString(id));
        }
        return transaction;
    }

    DECLARE_ENTITY_MAP_ACCESSORS(Transaction, TTransaction, TTransactionId);


    // ITransactionManager implementation.
    TTransactionId StartTransaction(
        TTimestamp startTimestamp,
        const NHive::NProto::TReqStartTransaction& request)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        const auto& requestExt = request.GetExtension(NTabletClient::NProto::TReqStartTransactionExt::start_transaction_ext);

        auto transactionId = FromProto<TTransactionId>(requestExt.transaction_id());

        auto timeout =
            requestExt.has_timeout()
            ? TNullable<TDuration>(TDuration::MilliSeconds(requestExt.timeout()))
            : Null;

        auto* transaction = new TTransaction(transactionId);
        TransactionMap_.Insert(transactionId, transaction);

        auto actualTimeout = GetActualTimeout(timeout);
        transaction->SetTimeout(actualTimeout);
        transaction->SetStartTimestamp(startTimestamp);
        transaction->SetState(ETransactionState::Active);

        LOG_DEBUG("Transaction started (TransactionId: %s, StartTimestamp: %" PRIu64 ", Timeout: %" PRIu64 ")",
            ~ToString(transactionId),
            startTimestamp,
            actualTimeout.MilliSeconds());

        if (IsLeader()) {
            CreateLease(transaction, actualTimeout);
        }

        return transactionId;
    }

    void PrepareTransactionCommit(
        const TTransactionId& transactionId,
        bool persistent,
        TTimestamp prepareTimestamp)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = GetTransactionOrThrow(transactionId);

        if (transaction->GetState() != ETransactionState::Active) {
            THROW_ERROR_EXCEPTION("Transaction is not active");
        }

        transaction->SetPrepareTimestamp(prepareTimestamp);
        transaction->SetState(
            persistent
            ? ETransactionState::PersistentlyPrepared
            : ETransactionState::TransientlyPrepared);

        TransactionPrepared_.Fire(transaction);

        LOG_DEBUG("Transaction prepared (TransactionId: %s, Presistent: %s, PrepareTimestamp: %" PRIu64 ")",
            ~ToString(transactionId),
            ~FormatBool(persistent),
            prepareTimestamp);
    }

    void CommitTransaction(
        const TTransactionId& transactionId,
        TTimestamp commitTimestamp)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = GetTransactionOrThrow(transactionId);

        auto state = transaction->GetState();
        if (state != ETransactionState::Active &&
            state != ETransactionState::TransientlyPrepared &&
            state != ETransactionState::PersistentlyPrepared)
        {
            THROW_ERROR_EXCEPTION("Transaction %s is in %s state",
                ~ToString(transaction->GetId()),
                ~FormatEnum(state).Quote());
        }

        if (IsLeader()) {
            CloseLease(transaction);
        }

        transaction->SetCommitTimestamp(commitTimestamp);
        transaction->SetState(ETransactionState::Committed);

        TransactionCommitted_.Fire(transaction);

        FinishTransaction(transaction);

        LOG_INFO_UNLESS(IsRecovery(), "Transaction committed (TransactionId: %s, CommitTimestamp: %" PRIu64 ")",
            ~ToString(transactionId),
            commitTimestamp);
    }

    void AbortTransaction(
        const TTransactionId& transactionId)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = GetTransactionOrThrow(transactionId);

        if (transaction->GetState() == ETransactionState::PersistentlyPrepared) {
            THROW_ERROR_EXCEPTION("Cannot abort a persistently prepared transaction");
        }

        if (IsLeader()) {
            CloseLease(transaction);
        }

        transaction->SetState(ETransactionState::Aborted);

        TransactionAborted_.Fire(transaction);

        FinishTransaction(transaction);

        LOG_INFO_UNLESS(IsRecovery(), "Transaction aborted (TransactionId: %s)",
            ~ToString(transactionId));
    }

    void PingTransaction(
        const TTransactionId& transactionId,
        const NHive::NProto::TReqPingTransaction& request)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = GetTransactionOrThrow(transactionId);
        
        if (transaction->GetState() != ETransactionState::Active) {
            THROW_ERROR_EXCEPTION("Transaction is not active");
        }

        auto it = LeaseMap_.find(transaction->GetId());
        YCHECK(it != LeaseMap_.end());

        auto timeout = transaction->GetTimeout();

        TLeaseManager::RenewLease(it->second, timeout);

        LOG_DEBUG("Transaction pinged (TransactionId: %s, Timeout: %" PRIu64 ")",
            ~ToString(transaction->GetId()),
            timeout.MilliSeconds());
    }

private:
    TTransactionManagerConfigPtr Config_;

    TEntityMap<TTransactionId, TTransaction> TransactionMap_;
    yhash_map<TTransactionId, TLeaseManager::TLease> LeaseMap_;

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);


    TDuration GetActualTimeout(TNullable<TDuration> timeout)
    {
        return std::min(
            timeout.Get(Config_->DefaultTransactionTimeout),
            Config_->MaxTransactionTimeout);
    }
    
    void CreateLease(const TTransaction* transaction, TDuration timeout)
    {
        auto lease = TLeaseManager::CreateLease(
            timeout,
            BIND(&TImpl::OnTransactionExpired, MakeStrong(this), transaction->GetId())
                .Via(Slot_->GetEpochAutomatonInvoker()));
        YCHECK(LeaseMap_.insert(std::make_pair(transaction->GetId(), lease)).second);
    }

    void CloseLease(const TTransaction* transaction)
    {
        auto it = LeaseMap_.find(transaction->GetId());
        YCHECK(it != LeaseMap_.end());
        TLeaseManager::CloseLease(it->second);
        LeaseMap_.erase(it);
    }

    void OnTransactionExpired(const TTransactionId& id)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = FindTransaction(id);
        if (transaction->GetState() != ETransactionState::Active)
            return;

        LOG_INFO("Transaction lease expired (TransactionId: %s)",
            ~ToString(id));

        auto transactionSupervisor = Slot_->GetTransactionSupervisor();

        NHive::NProto::TReqAbortTransaction req;
        ToProto(req.mutable_transaction_id(), transaction->GetId());

        transactionSupervisor
            ->CreateAbortTransactionMutation(req)
            ->OnSuccess(BIND([=] () {
                LOG_INFO("Transaction expiration commit success (TransactionId: %s)",
                    ~ToString(id));
            }))
            ->OnError(BIND([=] (const TError& error) {
                LOG_ERROR(error, "Transaction expiration commit failed (TransactionId: %s)",
                    ~ToString(id));
            }))
            ->Commit();
    }

    void FinishTransaction(TTransaction* transaction)
    {
        transaction->SetFinished();
        TransactionMap_.Remove(transaction->GetId());
    }


    virtual void OnLeaderActive() override
    {
        // Recreate leases for all active transactions.
        for (const auto& pair : TransactionMap_) {
            const auto* transaction = pair.second;
            if (transaction->GetState() == ETransactionState::Active) {
                auto actualTimeout = GetActualTimeout(transaction->GetTimeout());
                CreateLease(transaction, actualTimeout);
            }
        }
    }

    virtual void OnStopLeading() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        // Reset all leases.
        for (const auto& pair : LeaseMap_) {
            TLeaseManager::CloseLease(pair.second);
        }
        LeaseMap_.clear();

        // Reset all transiently prepared transactions back into active state.
        // Mark all transactions are finished to release pending readers.
        for (const auto& pair : TransactionMap_) {
            auto* transaction = pair.second;
            if (transaction->GetState() == ETransactionState::TransientlyPrepared) {
                transaction->SetState(ETransactionState::Active);
            }
            transaction->ResetFinished();
        }
    }


    void SaveKeys(TSaveContext& context)
    {
        TransactionMap_.SaveKeys(context);
    }

    void SaveValues(TSaveContext& context)
    {
        TransactionMap_.SaveValues(context);
    }

    void OnBeforeSnapshotLoaded() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        DoClear();
    }

    void LoadKeys(TLoadContext& context)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TransactionMap_.LoadKeys(context);
    }

    void LoadValues(TLoadContext& context)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TransactionMap_.LoadValues(context);
    }

    void DoClear()
    {
        TransactionMap_.Clear();
    }

    void Clear() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        DoClear();
    }

};

DEFINE_ENTITY_MAP_ACCESSORS(TTransactionManager::TImpl, Transaction, TTransaction, TTransactionId, TransactionMap_)

////////////////////////////////////////////////////////////////////////////////

TTransactionManager::TTransactionManager(
    TTransactionManagerConfigPtr config,
    TTabletSlot* slot,
    TBootstrap* bootstrap)
    : Impl(New<TImpl>(
        config,
        slot,
        bootstrap))
{ }

TTransaction* TTransactionManager::GetTransactionOrThrow(const TTransactionId& id)
{
    return Impl->GetTransactionOrThrow(id);
}

void TTransactionManager::PrepareTransactionCommit(
    const TTransactionId& transactionId,
    bool persistent,
    TTimestamp prepareTimestamp)
{
    Impl->PrepareTransactionCommit(
        transactionId,
        persistent,
        prepareTimestamp);
}

TTransactionId TTransactionManager::StartTransaction(
    TTimestamp startTimestamp,
    const NHive::NProto::TReqStartTransaction& request)
{
    return Impl->StartTransaction(startTimestamp, request);
}

void TTransactionManager::CommitTransaction(
    const TTransactionId& transactionId,
    TTimestamp commitTimestamp)
{
    Impl->CommitTransaction(transactionId, commitTimestamp);
}

void TTransactionManager::AbortTransaction(const TTransactionId& transactionId)
{
    Impl->AbortTransaction(transactionId);
}

void TTransactionManager::PingTransaction(
    const TTransactionId& transactionId,
    const NHive::NProto::TReqPingTransaction& request)
{
    Impl->PingTransaction(transactionId, request);
}

DELEGATE_SIGNAL(TTransactionManager, void(TTransaction*), TransactionStarted, *Impl);
DELEGATE_SIGNAL(TTransactionManager, void(TTransaction*), TransactionPrepared, *Impl);
DELEGATE_SIGNAL(TTransactionManager, void(TTransaction*), TransactionCommitted, *Impl);
DELEGATE_SIGNAL(TTransactionManager, void(TTransaction*), TransactionAborted, *Impl);
DELEGATE_ENTITY_MAP_ACCESSORS(TTransactionManager, Transaction, TTransaction, TTransactionId, *Impl);

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
