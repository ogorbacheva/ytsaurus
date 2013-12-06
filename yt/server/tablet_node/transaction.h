#pragma once

#include "public.h"
#include "dynamic_memory_store_bits.h"

#include <core/misc/property.h>
#include <core/misc/ref_tracked.h>

#include <core/actions/future.h>

#include <ytlib/transaction_client/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(ETransactionState,
    ((Active)               (0))
    ((PersistentlyPrepared) (1))
    ((TransientlyPrepared)  (2))
    ((Committed)            (3))
    ((Aborted)              (4))
);

class TTransaction
    : public TRefTracked<TTransaction>
{
public:
    DEFINE_BYVAL_RO_PROPERTY(NTransactionClient::TTransactionId, Id);
    DEFINE_BYVAL_RW_PROPERTY(TDuration, Timeout);
    DEFINE_BYVAL_RW_PROPERTY(TInstant, StartTime);
    DEFINE_BYVAL_RW_PROPERTY(ETransactionState, State);
    DEFINE_BYVAL_RW_PROPERTY(TTimestamp, StartTimestamp);
    DEFINE_BYVAL_RW_PROPERTY(TTimestamp, PrepareTimestamp);
    DEFINE_BYVAL_RW_PROPERTY(TTimestamp, CommitTimestamp);
    DEFINE_BYREF_RW_PROPERTY(std::vector<TDynamicRowRef>, LockedRows);

public:
    explicit TTransaction(const NTransactionClient::TTransactionId& id);

    void Save(TSaveContext& context) const;
    void Load(TLoadContext& context);

    TFuture<void> GetFinished() const;
    void SetFinished();
    void ResetFinished();

private:
    TPromise<void> Finished_;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
