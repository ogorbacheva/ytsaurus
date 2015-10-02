#include "stdafx.h"
#include "chunk_list_pool.h"
#include "config.h"
#include "private.h"

#include <ytlib/object_client/master_ypath_proxy.h>

namespace NYT {
namespace NScheduler {

using namespace NCypressClient;
using namespace NObjectClient;
using namespace NTransactionClient;
using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

TChunkListPool::TChunkListPool(
    TSchedulerConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    IInvokerPtr controlInvoker,
    const TOperationId& operationId,
    const TTransactionId& transactionId)
    : Config(config)
    , MasterChannel(masterChannel)
    , ControlInvoker(controlInvoker)
    , OperationId(operationId)
    , TransactionId(transactionId)
    , Logger(OperationLogger)
    , RequestInProgress(false)
    , LastSuccessCount(-1)
{
    YCHECK(config);
    YCHECK(masterChannel);
    YCHECK(controlInvoker);

    Logger.AddTag("OperationId: %v", operationId);

    AllocateMore();
}

bool TChunkListPool::HasEnough(int requestedCount)
{
    int currentSize = static_cast<int>(Ids.size());
    if (currentSize >= requestedCount + Config->ChunkListWatermarkCount) {
        // Enough chunk lists. Above the watermark even after extraction.
        return true;
    } else {
        // Additional chunk lists are definitely needed but still could be a success.
        AllocateMore();
        return currentSize >= requestedCount;
    }
}

TChunkListId TChunkListPool::Extract()
{
    YCHECK(!Ids.empty());
    auto id = Ids.back();
    Ids.pop_back();

    LOG_DEBUG("Extracted chunk list %v from the pool, %v remaining",
        id,
        static_cast<int>(Ids.size()));

    return id;
}

void TChunkListPool::Reinstall(const TChunkListId& id)
{
    Ids.push_back(id);
    LOG_DEBUG("Reinstalled chunk list %v into the pool, %v remaining",
        id,
        static_cast<int>(Ids.size()));
}

void TChunkListPool::Release(const std::vector<TChunkListId>& ids)
{
    TObjectServiceProxy objectProxy(MasterChannel);
    auto batchReq = objectProxy.ExecuteBatch();
    for (const auto& id : ids) {
        auto req = TMasterYPathProxy::UnstageObject();
        ToProto(req->mutable_object_id(), id);
        req->set_recursive(true);
        batchReq->AddRequest(req);
    }

    // Fire-and-forget.
    // The subscriber is only needed to log the outcome.
    batchReq->Invoke().Subscribe(
        BIND(&TChunkListPool::OnChunkListsReleased, MakeStrong(this)));
}

void TChunkListPool::AllocateMore()
{
    int count = LastSuccessCount < 0
        ? Config->ChunkListPreallocationCount
        : static_cast<int>(LastSuccessCount * Config->ChunkListAllocationMultiplier);

    count = std::min(count, Config->MaxChunkListAllocationCount);

    if (RequestInProgress) {
        LOG_DEBUG("Cannot allocate more chunk lists, another request is in progress");
        return;
    }

    LOG_INFO("Allocating %v chunk lists for pool", count);

    TObjectServiceProxy objectProxy(MasterChannel);
    auto req = TMasterYPathProxy::CreateObjects();
    ToProto(req->mutable_transaction_id(), TransactionId);
    req->set_type(static_cast<int>(EObjectType::ChunkList));
    req->set_object_count(count);

    objectProxy.Execute(req).Subscribe(
        BIND(&TChunkListPool::OnChunkListsCreated, MakeWeak(this))
            .Via(ControlInvoker));

    RequestInProgress = true;
}

void TChunkListPool::OnChunkListsCreated(const TMasterYPathProxy::TErrorOrRspCreateObjectsPtr& rspOrError)
{
    YCHECK(RequestInProgress);
    RequestInProgress = false;

    if (!rspOrError.IsOK()) {
        LOG_ERROR(rspOrError, "Error allocating chunk lists");
        return;
    }

    LOG_INFO("Chunk lists allocated");

    const auto& rsp = rspOrError.Value();
    for (const auto& id : rsp->object_ids()) {
        Ids.push_back(FromProto<TChunkListId>(id));
    }

    LastSuccessCount = rsp->object_ids_size();
}

void TChunkListPool::OnChunkListsReleased(const TObjectServiceProxy::TErrorOrRspExecuteBatchPtr& batchRspOrError)
{
    auto error = GetCumulativeError(batchRspOrError);
    if (!error.IsOK()) {
        LOG_WARNING(error, "Error releasing chunk lists");
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
