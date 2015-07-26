#include "stdafx.h"
#include "private.h"
#include "chunk_owner_node_proxy.h"
#include "chunk.h"
#include "chunk_list.h"
#include "chunk_manager.h"
#include "chunk_tree_traversing.h"
#include "config.h"
#include "helpers.h"

#include <core/concurrency/scheduler.h>

#include <core/ytree/node.h>
#include <core/ytree/fluent.h>
#include <core/ytree/system_attribute_provider.h>
#include <core/ytree/attribute_helpers.h>

#include <core/erasure/codec.h>

#include <ytlib/chunk_client/chunk_meta_extensions.h>
#include <ytlib/chunk_client/chunk_spec.h>

#include <ytlib/object_client/helpers.h>

#include <server/node_tracker_server/node_directory_builder.h>

#include <server/object_server/object.h>

#include <server/cell_master/config.h>

namespace NYT {
namespace NChunkServer {

using namespace NConcurrency;
using namespace NChunkClient;
using namespace NCypressServer;
using namespace NNodeTrackerServer;
using namespace NObjectClient;
using namespace NObjectServer;
using namespace NTransactionServer;
using namespace NYson;
using namespace NYTree;
using namespace NVersionedTableClient;
using namespace NSecurityServer;

using NChunkClient::NProto::TReqFetch;
using NChunkClient::NProto::TRspFetch;
using NChunkClient::NProto::TMiscExt;

////////////////////////////////////////////////////////////////////////////////

class TFetchChunkVisitor
    : public IChunkVisitor
{
public:
    typedef NRpc::TTypedServiceContext<TReqFetch, TRspFetch> TCtxFetch;
    typedef TIntrusivePtr<TCtxFetch> TCtxFetchPtr;

    TFetchChunkVisitor(
        NCellMaster::TBootstrap* bootstrap,
        TChunkManagerConfigPtr config,
        TChunkList* chunkList,
        TCtxFetchPtr context,
        const TChannel& channel,
        bool fetchParityReplicas,
        const std::vector<TReadRange>& ranges)
        : Bootstrap_(bootstrap)
        , Config_(config)
        , ChunkList_(chunkList)
        , Context_(context)
        , Channel_(channel)
        , FetchParityReplicas_(fetchParityReplicas)
        , Ranges_(ranges)
        , NodeDirectoryBuilder_(context->Response().mutable_node_directory())
    {
        if (!Context_->Request().fetch_all_meta_extensions()) {
            for (int tag : Context_->Request().extension_tags()) {
                ExtensionTags_.insert(tag);
            }
        }
    }

    void Run()
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        if (Ranges_.empty()) {
            ReplySuccess();
            return;
        }

        TraverseChunkTree(
            CreatePreemptableChunkTraverserCallbacks(Bootstrap_),
            this,
            ChunkList_,
            Ranges_[CurrentRangeIndex_].LowerLimit(),
            Ranges_[CurrentRangeIndex_].UpperLimit());
    }

private:
    NCellMaster::TBootstrap* Bootstrap_;
    TChunkManagerConfigPtr Config_;
    TChunkList* ChunkList_;
    TCtxFetchPtr Context_;
    TChannel Channel_;
    bool FetchParityReplicas_;

    std::vector<TReadRange> Ranges_;
    int CurrentRangeIndex_ = 0;

    yhash_set<int> ExtensionTags_;
    TNodeDirectoryBuilder NodeDirectoryBuilder_;
    bool Finished_ = false;

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);

    void ReplySuccess()
    {
        YCHECK(!Finished_);
        Finished_ = true;

        try {
            // Update upper limits for all returned journal chunks.
            auto* chunkSpecs = Context_->Response().mutable_chunks();
            auto chunkManager = Bootstrap_->GetChunkManager();
            for (auto& chunkSpec : *chunkSpecs) {
                auto chunkId = FromProto<TChunkId>(chunkSpec.chunk_id());
                if (TypeFromId(chunkId) == EObjectType::JournalChunk) {
                    auto* chunk = chunkManager->FindChunk(chunkId);
                    if (!IsObjectAlive(chunk)) {
                        THROW_ERROR_EXCEPTION(
                            NRpc::EErrorCode::Unavailable,
                            "Optimistic locking failed for chunk %v",
                            chunkId);
                    }

                    auto result = WaitFor(chunkManager->GetChunkQuorumInfo(chunk))
                        .ValueOrThrow();
                    i64 quorumRowCount = result.row_count();

                    auto lowerLimit = FromProto<TReadLimit>(chunkSpec.lower_limit());
                    if (!lowerLimit.HasRowIndex()) {
                        lowerLimit.SetRowIndex(0);
                    }
                    ToProto(chunkSpec.mutable_lower_limit(), lowerLimit);

                    auto upperLimit = FromProto<TReadLimit>(chunkSpec.upper_limit());
                    i64 upperLimitRowIndex = upperLimit.HasRowIndex() ? upperLimit.GetRowIndex() : std::numeric_limits<i64>::max();
                    upperLimit.SetRowIndex(std::min(upperLimitRowIndex, quorumRowCount));
                    ToProto(chunkSpec.mutable_upper_limit(), upperLimit);
                }
            }

            Context_->SetResponseInfo("ChunkCount: %v", chunkSpecs->size());
            Context_->Reply();
        } catch (const std::exception& ex) {
            Context_->Reply(ex);
        }
    }

    void ReplyError(const TError& error)
    {
        if (Finished_)
            return;

        Finished_ = true;

        Context_->Reply(error);
    }

    virtual bool OnChunk(
        TChunk* chunk,
        i64 rowIndex,
        const TReadLimit& lowerLimit,
        const TReadLimit& upperLimit) override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        if (Context_->Response().chunks_size() >= Config_->MaxChunksPerFetch) {
            ReplyError(TError("Attempt to fetch too many chunks in a single request")
                << TErrorAttribute("limit", Config_->MaxChunksPerFetch));
            return false;
        }

        auto chunkManager = Bootstrap_->GetChunkManager();
        const auto& config = Bootstrap_->GetConfig()->ChunkManager;

        if (!chunk->IsConfirmed()) {
            ReplyError(TError("Cannot fetch an object containing an unconfirmed chunk %v",
                chunk->GetId()));
            return false;
        }

        auto* chunkSpec = Context_->Response().add_chunks();

        chunkSpec->set_table_row_index(rowIndex);

        if (!Channel_.IsUniversal()) {
            ToProto(chunkSpec->mutable_channel(), Channel_);
        }

        auto erasureCodecId = chunk->GetErasureCodec();
        int firstInfeasibleReplicaIndex =
            erasureCodecId == NErasure::ECodec::None || FetchParityReplicas_
                ? std::numeric_limits<int>::max() // all replicas are feasible
                : NErasure::GetCodec(erasureCodecId)->GetDataPartCount();

        SmallVector<TNodePtrWithIndex, TypicalReplicaCount> replicas;
        auto addReplica = [&] (TNodePtrWithIndex replica) -> bool {
            if (replica.GetIndex() < firstInfeasibleReplicaIndex) {
                replicas.push_back(replica);
                return true;
            } else {
                return false;
            }
        };

        for (auto replica : chunk->StoredReplicas()) {
            addReplica(replica);
        }

        if (chunk->CachedReplicas()) {
            int cachedReplicaCount = 0;
            for (auto replica : *chunk->CachedReplicas()) {
                if (cachedReplicaCount >= config->MaxCachedReplicasPerFetch) {
                    break;
                }
                if (addReplica(replica)) {
                    ++cachedReplicaCount;
                }
            }
        }

        for (auto replica : replicas) {
            NodeDirectoryBuilder_.Add(replica);
            chunkSpec->add_replicas(NYT::ToProto<ui32>(replica));
        }

        ToProto(chunkSpec->mutable_chunk_id(), chunk->GetId());
        chunkSpec->set_erasure_codec(static_cast<int>(erasureCodecId));

        chunkSpec->mutable_chunk_meta()->set_type(chunk->ChunkMeta().type());
        chunkSpec->mutable_chunk_meta()->set_version(chunk->ChunkMeta().version());

        if (Context_->Request().fetch_all_meta_extensions()) {
            *chunkSpec->mutable_chunk_meta()->mutable_extensions() = chunk->ChunkMeta().extensions();
        } else {
            FilterProtoExtensions(
                chunkSpec->mutable_chunk_meta()->mutable_extensions(),
                chunk->ChunkMeta().extensions(),
                ExtensionTags_);
        }

        // Try to keep responses small -- avoid producing redundant limits.
        if (!IsTrivial(lowerLimit)) {
            ToProto(chunkSpec->mutable_lower_limit(), lowerLimit);
        }
        if (!IsTrivial(upperLimit)) {
            ToProto(chunkSpec->mutable_upper_limit(), upperLimit);
        }

        chunkSpec->set_range_index(CurrentRangeIndex_);

        return true;
    }

    virtual void OnError(const TError& error) override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        ReplyError(error);
    }

    virtual void OnFinish() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        CurrentRangeIndex_ += 1;
        if (CurrentRangeIndex_ == Ranges_.size()) {
            if (CurrentRangeIndex_ == Ranges_.size() && !Finished_) {
                ReplySuccess();
            }
        } else {
            TraverseChunkTree(
                CreatePreemptableChunkTraverserCallbacks(Bootstrap_),
                this,
                ChunkList_,
                Ranges_[CurrentRangeIndex_].LowerLimit(),
                Ranges_[CurrentRangeIndex_].UpperLimit());
        }
    }

};

typedef TIntrusivePtr<TFetchChunkVisitor> TFetchChunkVisitorPtr;

////////////////////////////////////////////////////////////////////////////////

class TChunkVisitorBase
    : public IChunkVisitor
{
public:
    TFuture<void> Run()
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TraverseChunkTree(
            CreatePreemptableChunkTraverserCallbacks(Bootstrap),
            this,
            ChunkList);

        return Promise;
    }

protected:
    NCellMaster::TBootstrap* Bootstrap;
    IYsonConsumer* Consumer;
    TChunkList* ChunkList;
    TPromise<void> Promise;

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);

    TChunkVisitorBase(
        NCellMaster::TBootstrap* bootstrap,
        TChunkList* chunkList,
        IYsonConsumer* consumer)
        : Bootstrap(bootstrap)
        , Consumer(consumer)
        , ChunkList(chunkList)
        , Promise(NewPromise<void>())
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);
    }

    virtual void OnError(const TError& error) override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        Promise.Set(TError("Error traversing chunk tree") << error);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TChunkIdsAttributeVisitor
    : public TChunkVisitorBase
{
public:
    TChunkIdsAttributeVisitor(
        NCellMaster::TBootstrap* bootstrap,
        TChunkList* chunkList,
        IYsonConsumer* consumer)
        : TChunkVisitorBase(bootstrap, chunkList, consumer)
    {
        Consumer->OnBeginList();
    }

private:
    virtual bool OnChunk(
        TChunk* chunk,
        i64 /*rowIndex*/,
        const TReadLimit& /*startLimit*/,
        const TReadLimit& /*endLimit*/) override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        Consumer->OnListItem();
        Consumer->OnStringScalar(ToString(chunk->GetId()));

        return true;
    }

    virtual void OnFinish() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        Consumer->OnEndList();
        Promise.Set(TError());
    }
};

TFuture<void> GetChunkIdsAttribute(
    NCellMaster::TBootstrap* bootstrap,
    TChunkList* chunkList,
    IYsonConsumer* consumer)
{
    auto visitor = New<TChunkIdsAttributeVisitor>(
        bootstrap,
        const_cast<TChunkList*>(chunkList),
        consumer);
    return visitor->Run();
}

////////////////////////////////////////////////////////////////////////////////

template <class TCodecExtractor>
class TCodecStatisticsVisitor
    : public TChunkVisitorBase
{
public:
    TCodecStatisticsVisitor(
        NCellMaster::TBootstrap* bootstrap,
        TChunkList* chunkList,
        IYsonConsumer* consumer)
        : TChunkVisitorBase(bootstrap, chunkList, consumer)
        , CodecExtractor_()
    { }

private:
    virtual bool OnChunk(
        TChunk* chunk,
        i64 /*rowIndex*/,
        const TReadLimit& /*startLimit*/,
        const TReadLimit& /*endLimit*/) override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        CodecInfo_[CodecExtractor_(chunk)].Accumulate(chunk->GetStatistics());
        return true;
    }

    virtual void OnFinish() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        BuildYsonFluently(Consumer)
            .DoMapFor(CodecInfo_, [=] (TFluentMap fluent, const typename TCodecInfoMap::value_type& pair) {
                const auto& statistics = pair.second;
                // TODO(panin): maybe use here the same method as in attributes
                fluent
                    .Item(FormatEnum(pair.first)).BeginMap()
                        .Item("chunk_count").Value(statistics.ChunkCount)
                        .Item("uncompressed_data_size").Value(statistics.UncompressedDataSize)
                        .Item("compressed_data_size").Value(statistics.CompressedDataSize)
                    .EndMap();
            });
        Promise.Set(TError());
    }

    typedef yhash_map<typename TCodecExtractor::TValue, TChunkTreeStatistics> TCodecInfoMap;
    TCodecInfoMap CodecInfo_;

    TCodecExtractor CodecExtractor_;

};

template <class TVisitor>
TFuture<void> ComputeCodecStatistics(
    NCellMaster::TBootstrap* bootstrap,
    TChunkList* chunkList,
    IYsonConsumer* consumer)
{
    auto visitor = New<TVisitor>(
        bootstrap,
        const_cast<TChunkList*>(chunkList),
        consumer);
    return visitor->Run();
}

////////////////////////////////////////////////////////////////////////////////

TChunkOwnerNodeProxy::TChunkOwnerNodeProxy(
    INodeTypeHandlerPtr typeHandler,
    NCellMaster::TBootstrap* bootstrap,
    TTransaction* transaction,
    TChunkOwnerBase* trunkNode)
    : TNontemplateCypressNodeProxyBase(
        typeHandler,
        bootstrap,
        transaction,
        trunkNode)
{ }

bool TChunkOwnerNodeProxy::DoInvoke(NRpc::IServiceContextPtr context)
{
    DISPATCH_YPATH_SERVICE_METHOD(PrepareForUpdate);
    DISPATCH_YPATH_HEAVY_SERVICE_METHOD(Fetch);
    return TNontemplateCypressNodeProxyBase::DoInvoke(context);
}

TClusterResources TChunkOwnerNodeProxy::GetResourceUsage() const
{
    const auto* node = GetThisTypedImpl<TChunkOwnerBase>();
    const auto* chunkList = node->GetChunkList();
    if (chunkList) {
        const auto& statistics = chunkList->Statistics();
        i64 diskSpace =
            statistics.RegularDiskSpace * node->GetReplicationFactor() +
            statistics.ErasureDiskSpace;
        int chunkCount = statistics.ChunkCount;
        return TClusterResources(diskSpace, 1, chunkCount);
    } else {
        return TClusterResources(0, 1, 0);
    }
}

void TChunkOwnerNodeProxy::ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors)
{
    TNontemplateCypressNodeProxyBase::ListSystemAttributes(descriptors);

    const auto* node = GetThisTypedImpl<TChunkOwnerBase>();
    auto isExternal = node->IsExternal();

    descriptors->push_back(TAttributeDescriptor("chunk_list_id")
        .SetPresent(!isExternal));
    descriptors->push_back(TAttributeDescriptor("chunk_ids")
        .SetPresent(!isExternal)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor("compression_statistics")
        .SetPresent(!isExternal)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor("erasure_statistics")
        .SetPresent(!isExternal)
        .SetOpaque(true));
    descriptors->push_back(TAttributeDescriptor("chunk_count")
        .SetPresent(!isExternal));
    descriptors->push_back(TAttributeDescriptor("uncompressed_data_size")
        .SetPresent(!isExternal));
    descriptors->push_back(TAttributeDescriptor("compressed_data_size")
        .SetPresent(!isExternal));
    descriptors->push_back(TAttributeDescriptor("compression_ratio")
        .SetPresent(!isExternal));
    descriptors->push_back(TAttributeDescriptor("compression_codec")
        .SetPresent(!isExternal)
        .SetCustom(true));
    descriptors->push_back(TAttributeDescriptor("erasure_codec")
        .SetPresent(!isExternal)
        .SetCustom(true));
    descriptors->push_back("update_mode");
    descriptors->push_back("replication_factor");
    descriptors->push_back("vital");
}

bool TChunkOwnerNodeProxy::GetBuiltinAttribute(
    const Stroka& key,
    IYsonConsumer* consumer)
{
    auto* node = GetThisTypedImpl<TChunkOwnerBase>();
    const auto* chunkList = node->GetChunkList();
    const auto& statistics = chunkList->Statistics();

    auto cypressManager = Bootstrap_->GetCypressManager();
    auto isExternal = node->IsExternal();

    if (!isExternal) {
        if (key == "chunk_list_id") {
            BuildYsonFluently(consumer)
                .Value(chunkList->GetId());
            return true;
        }

        if (key == "chunk_count") {
            BuildYsonFluently(consumer)
                .Value(statistics.ChunkCount);
            return true;
        }

        if (key == "uncompressed_data_size") {
            BuildYsonFluently(consumer)
                .Value(statistics.UncompressedDataSize);
            return true;
        }

        if (key == "compressed_data_size") {
            BuildYsonFluently(consumer)
                .Value(statistics.CompressedDataSize);
            return true;
        }

        if (key == "compression_ratio") {
            double ratio = statistics.UncompressedDataSize > 0
                ? static_cast<double>(statistics.CompressedDataSize) / statistics.UncompressedDataSize
                : 0;
            BuildYsonFluently(consumer)
                .Value(ratio);
            return true;
        }
    }

    if (key == "update_mode") {
        BuildYsonFluently(consumer)
            .Value(FormatEnum(node->GetUpdateMode()));
        return true;
    }

    if (key == "replication_factor") {
        BuildYsonFluently(consumer)
            .Value(node->GetReplicationFactor());
        return true;
    }

    if (key == "vital") {
        BuildYsonFluently(consumer)
            .Value(node->GetVital());
        return true;
    }

    return TNontemplateCypressNodeProxyBase::GetBuiltinAttribute(key, consumer);
}

TFuture<void> TChunkOwnerNodeProxy::GetBuiltinAttributeAsync(
    const Stroka& key,
    IYsonConsumer* consumer)
{
    auto* node = GetThisTypedImpl<TChunkOwnerBase>();
    const auto* chunkList = node->GetChunkList();

    auto cypressManager = Bootstrap_->GetCypressManager();
    auto isExternal = node->IsExternal();

    if (!isExternal) {
        if (key == "chunk_ids") {
            return GetChunkIdsAttribute(
                Bootstrap_,
                const_cast<TChunkList*>(chunkList),
                consumer);
        }

        if (key == "compression_statistics") {
            struct TExtractCompressionCodec
            {
                typedef NCompression::ECodec TValue;
                TValue operator() (const TChunk* chunk)
                {
                    return TValue(chunk->MiscExt().compression_codec());
                }
            };
            typedef TCodecStatisticsVisitor<TExtractCompressionCodec> TCompressionStatisticsVisitor;

            return ComputeCodecStatistics<TCompressionStatisticsVisitor>(
                Bootstrap_,
                const_cast<TChunkList*>(chunkList),
                consumer);
        }

        if (key == "erasure_statistics") {
            struct TExtractErasureCodec
            {
                typedef NErasure::ECodec TValue;
                TValue operator() (const TChunk* chunk)
                {
                    return chunk->GetErasureCodec();
                }
            };
            typedef TCodecStatisticsVisitor<TExtractErasureCodec> TErasureStatisticsVisitor;

            return ComputeCodecStatistics<TErasureStatisticsVisitor>(
                Bootstrap_,
                const_cast<TChunkList*>(chunkList),
                consumer);
        }
    }

    return TNontemplateCypressNodeProxyBase::GetBuiltinAttributeAsync(key, consumer);
}

void TChunkOwnerNodeProxy::ValidateCustomAttributeUpdate(
    const Stroka& key,
    const TNullable<TYsonString>& /*oldValue*/,
    const TNullable<TYsonString>& newValue)
{
    if (key == "compression_codec") {
        if (!newValue) {
            ThrowCannotRemoveAttribute(key);
        }
        ParseEnum<NCompression::ECodec>(ConvertTo<Stroka>(newValue.Get()));
        return;
    }

    if (key == "erasure_codec") {
        if (!newValue) {
            ThrowCannotRemoveAttribute(key);
        }
        ParseEnum<NErasure::ECodec>(ConvertTo<Stroka>(newValue.Get()));
        return;
    }
}

bool TChunkOwnerNodeProxy::SetBuiltinAttribute(
    const Stroka& key,
    const TYsonString& value)
{
    auto chunkManager = Bootstrap_->GetChunkManager();

    auto* node = GetThisTypedImpl<TChunkOwnerBase>();

    if (key == "replication_factor") {
        ValidateNoTransaction();
        int replicationFactor = ConvertTo<int>(value);
        if (replicationFactor < MinReplicationFactor ||
            replicationFactor > MaxReplicationFactor)
        {
            THROW_ERROR_EXCEPTION("\"replication_factor\" must be in range [%v,%v]",
                MinReplicationFactor,
                MaxReplicationFactor);
        }

        YCHECK(node->IsTrunk());

        if (node->GetReplicationFactor() != replicationFactor) {
            node->SetReplicationFactor(replicationFactor);

            auto securityManager = Bootstrap_->GetSecurityManager();
            securityManager->UpdateAccountNodeUsage(node);

            if (IsLeader() && !node->IsExternal()) {
                chunkManager->ScheduleChunkPropertiesUpdate(node->GetChunkList());
            }
        }
        return true;
    }

    if (key == "vital") {
        ValidateNoTransaction();
        bool vital = ConvertTo<bool>(value);

        YCHECK(node->IsTrunk());

        if (node->GetVital() != vital) {
            node->SetVital(vital);

            if (IsLeader() && !node->IsExternal()) {
                chunkManager->ScheduleChunkPropertiesUpdate(node->GetChunkList());
            }
        }

        return true;
    }

    return TNontemplateCypressNodeProxyBase::SetBuiltinAttribute(key, value);
}

void TChunkOwnerNodeProxy::ValidateFetchParameters(
    const TChannel& /*channel*/,
    const std::vector<TReadRange>& /*ranges*/)
{ }

void TChunkOwnerNodeProxy::Clear()
{ }

void TChunkOwnerNodeProxy::ValidatePrepareForUpdate()
{
    const auto* node = GetThisTypedImpl<TChunkOwnerBase>();
    if (node->GetUpdateMode() != EUpdateMode::None) {
        THROW_ERROR_EXCEPTION("Node is already in %Qlv mode",
            node->GetUpdateMode());
    }
}

void TChunkOwnerNodeProxy::ValidateFetch()
{ }

bool TChunkOwnerNodeProxy::IsSorted()
{
    return false;
}

void TChunkOwnerNodeProxy::ResetSorted()
{ }

DEFINE_YPATH_SERVICE_METHOD(TChunkOwnerNodeProxy, PrepareForUpdate)
{
    DeclareMutating();

    auto updateMode = EUpdateMode(request->update_mode());
    YCHECK(updateMode == EUpdateMode::Append ||
           updateMode == EUpdateMode::Overwrite);

    auto lockMode = ELockMode(request->lock_mode());
    YCHECK(lockMode == ELockMode::Shared ||
           lockMode == ELockMode::Exclusive);

    context->SetRequestInfo("UpdateMode: %v, LockMode: %v",
        updateMode,
        lockMode);

    ValidateTransaction();
    ValidatePermission(
        EPermissionCheckScope::This,
        EPermission::Write);

    auto* node = LockThisTypedImpl<TChunkOwnerBase>(lockMode);
    ValidatePrepareForUpdate();

    auto chunkManager = Bootstrap_->GetChunkManager();
    auto objectManager = Bootstrap_->GetObjectManager();

    TChunkList* resultChunkList = nullptr;
    switch (updateMode) {
        case EUpdateMode::Append: {
            if (node->IsExternal()) {
                LOG_DEBUG_UNLESS(
                    IsRecovery(),
                    "External node is switched to \"append\" mode (NodeId: %v)",
                    node->GetId());

            } else {
                auto* snapshotChunkList = node->GetChunkList();

                auto* newChunkList = chunkManager->CreateChunkList();
                YCHECK(newChunkList->OwningNodes().insert(node).second);

                YCHECK(snapshotChunkList->OwningNodes().erase(node) == 1);
                node->SetChunkList(newChunkList);
                objectManager->RefObject(newChunkList);

                chunkManager->AttachToChunkList(newChunkList, snapshotChunkList);

                auto* deltaChunkList = chunkManager->CreateChunkList();
                chunkManager->AttachToChunkList(newChunkList, deltaChunkList);

                objectManager->UnrefObject(snapshotChunkList);

                resultChunkList = deltaChunkList;

                if (request->fetch_last_key()) {
                    TOwningKey lastKey;
                    if (IsSorted() && !snapshotChunkList->Children().empty()) {
                        lastKey = GetMaxKey(snapshotChunkList);
                    }
                    ToProto(response->mutable_last_key(), lastKey);
                }

                LOG_DEBUG_UNLESS(
                    IsRecovery(),
                    "Node is switched to \"append\" mode (NodeId: %v, NewChunkListId: %v, SnapshotChunkListId: %v, DeltaChunkListId: %v)",
                    node->GetId(),
                    newChunkList->GetId(),
                    snapshotChunkList->GetId(),
                    deltaChunkList->GetId());

            }
            break;
        }

        case EUpdateMode::Overwrite: {
            if (node->IsExternal()) {
                LOG_DEBUG_UNLESS(
                    IsRecovery(),
                    "External node is switched to \"overwrite\" mode (NodeId: %v)",
                    node->GetId());
            } else {
                auto* oldChunkList = node->GetChunkList();
                YCHECK(oldChunkList->OwningNodes().erase(node) == 1);
                objectManager->UnrefObject(oldChunkList);

                auto* newChunkList = chunkManager->CreateChunkList();
                YCHECK(newChunkList->OwningNodes().insert(node).second);
                node->SetChunkList(newChunkList);
                objectManager->RefObject(newChunkList);

                resultChunkList = newChunkList;

                LOG_DEBUG_UNLESS(
                    IsRecovery(),
                    "Node is switched to \"overwrite\" mode (NodeId: %v, NewChunkListId: %v)",
                    node->GetId(),
                    newChunkList->GetId());
            }

            Clear();

            break;
        }

        default:
            YUNREACHABLE();
    }

    node->SetUpdateMode(updateMode);

    ResetSorted();

    SetModified();

    if (!node->IsExternal()) {
        ToProto(response->mutable_chunk_list_id(), resultChunkList->GetId());
        context->SetResponseInfo("ChunkListId: %v",
            resultChunkList->GetId());
    }

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TChunkOwnerNodeProxy, Fetch)
{
    DeclareNonMutating();

    context->SetRequestInfo();

    ValidatePermission(
        EPermissionCheckScope::This,
        EPermission::Read);
    ValidateFetch();

    const auto* node = GetThisTypedImpl<TChunkOwnerBase>();
    if (node->IsExternal()) {
        THROW_ERROR_EXCEPTION("Cannot handle Fetch at an external node");
    }

    auto channel = request->has_channel()
        ? NYT::FromProto<TChannel>(request->channel())
        : TChannel::Universal();
    bool fetchParityReplicas = request->fetch_parity_replicas();

    auto ranges = FromProto<TReadRange>(request->ranges());
    ValidateFetchParameters(channel, ranges);

    auto* chunkList = node->GetChunkList();

    auto visitor = New<TFetchChunkVisitor>(
        Bootstrap_,
        Bootstrap_->GetConfig()->ChunkManager,
        chunkList,
        context,
        channel,
        fetchParityReplicas,
        ranges);

    visitor->Run();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
