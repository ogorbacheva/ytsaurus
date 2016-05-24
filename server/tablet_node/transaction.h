#pragma once

#include "public.h"
#include "dynamic_memory_store_bits.h"

#include <yt/server/hydra/entity_map.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/core/actions/future.h>

#include <yt/core/concurrency/public.h>

#include <yt/core/misc/persistent_queue.h>
#include <yt/core/misc/property.h>
#include <yt/core/misc/ref_tracked.h>
#include <yt/core/misc/ring_queue.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

struct TTransactionWriteRecord
{
    TTabletId TabletId;
    TSharedRef Data;

    void Save(TSaveContext& context) const;
    void Load(TLoadContext& context);
};

const size_t TransactionWriteLogChunkSize = 256;
using TTransactionWriteLog = TPersistentQueue<TTransactionWriteRecord, TransactionWriteLogChunkSize>;
using TTransactionWriteLogSnapshot = TPersistentQueueSnapshot<TTransactionWriteRecord, TransactionWriteLogChunkSize>;

////////////////////////////////////////////////////////////////////////////////

class TTransaction
    : public NHydra::TEntityBase
    , public TRefTracked<TTransaction>
{
public:
    DEFINE_BYVAL_RO_PROPERTY(TTransactionId, Id);
    DEFINE_BYVAL_RW_PROPERTY(TLease, Lease);
    DEFINE_BYVAL_RW_PROPERTY(NConcurrency::TDelayedExecutorCookie, TimeoutCookie);
    DEFINE_BYVAL_RW_PROPERTY(TDuration, Timeout);
    DEFINE_BYVAL_RW_PROPERTY(TInstant, RegisterTime);
    DEFINE_BYVAL_RW_PROPERTY(ETransactionState, State);
    DEFINE_BYVAL_RW_PROPERTY(TTimestamp, StartTimestamp);
    DEFINE_BYVAL_RW_PROPERTY(TTimestamp, PrepareTimestamp);
    DEFINE_BYVAL_RW_PROPERTY(TTimestamp, CommitTimestamp);
    DEFINE_BYREF_RW_PROPERTY(std::vector<TDynamicRowRef>, LockedRows);
    DEFINE_BYREF_RW_PROPERTY(TRingQueue<TDynamicRowRef>, PrelockedRows);
    DEFINE_BYREF_RW_PROPERTY(TTransactionWriteLog, WriteLog);

public:
    explicit TTransaction(const TTransactionId& id);

    void Save(TSaveContext& context) const;
    void Load(TLoadContext& context);

    TCallback<void(TSaveContext&)> AsyncSave();
    void AsyncLoad(TLoadContext& context);

    TFuture<void> GetFinished() const;
    void SetFinished();
    void ResetFinished();

    ETransactionState GetPersistentState() const;
    TTimestamp GetPersistentPrepareTimestamp() const;

    void ThrowInvalidState() const;

    TInstant GetStartTime() const;

private:
    TPromise<void> Finished_;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
