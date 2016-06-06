#pragma once

#include "public.h"

#include <yt/server/hydra/entity_map.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/core/actions/future.h>

#include <yt/core/misc/property.h>
#include <yt/core/misc/ref_tracked.h>

#include <yt/core/rpc/public.h>

namespace NYT {
namespace NHiveServer {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ECommitState,
    (Start)
    (Prepare)
    (GenerateCommitTimestamp) // transient only
    (Commit)
    (Abort)
    (Finish)                  // transient only
);

class TCommit
    : public NHydra::TEntityBase
    , public TRefTracked<TCommit>
{
public:
    DEFINE_BYVAL_RO_PROPERTY(TTransactionId, TransactionId);
    DEFINE_BYVAL_RO_PROPERTY(NRpc::TMutationId, MutationId);
    DEFINE_BYREF_RO_PROPERTY(std::vector<TCellId>, ParticipantCellIds);

    DEFINE_BYVAL_RW_PROPERTY(bool, Persistent);
    DEFINE_BYVAL_RW_PROPERTY(TTimestamp, CommitTimestamp);
    DEFINE_BYVAL_RW_PROPERTY(ECommitState, TransientState);
    DEFINE_BYVAL_RW_PROPERTY(ECommitState, PersistentState);
    DEFINE_BYREF_RW_PROPERTY(yhash_set<TCellId>, RespondedCellIds);

public:
    explicit TCommit(const TTransactionId& transactionId);
    TCommit(
        const TTransactionId& transactionId,
        const NRpc::TMutationId& mutationId,
        const std::vector<TCellId>& participantCellIds);

    TFuture<TSharedRefArray> GetAsyncResponseMessage();
    void SetResponseMessage(TSharedRefArray message);

    bool IsDistributed() const;

    void Save(NHydra::TSaveContext& context) const;
    void Load(NHydra::TLoadContext& context);

private:
    TPromise<TSharedRefArray> ResponseMessagePromise_ = NewPromise<TSharedRefArray>();

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NHiveServer
} // namespace NYT
