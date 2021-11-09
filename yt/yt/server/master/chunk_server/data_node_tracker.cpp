#include "data_node_tracker.h"
#include "private.h"

#include <yt/yt/server/master/cell_master/automaton.h>
#include <yt/yt/server/master/cell_master/config.h>
#include <yt/yt/server/master/cell_master/config_manager.h>
#include <yt/yt/server/master/cell_master/hydra_facade.h>

#include <yt/yt/server/master/chunk_server/chunk_manager.h>
#include <yt/yt/server/master/chunk_server/proto/chunk_manager.pb.h>

#include <yt/yt/server/master/node_tracker_server/node.h>
#include <yt/yt/server/master/node_tracker_server/node_tracker.h>

#include <yt/yt/ytlib/node_tracker_client/public.h>

namespace NYT::NChunkServer {

using namespace NCellMaster;
using namespace NConcurrency;
using namespace NDataNodeTrackerClient::NProto;
using namespace NHydra;
using namespace NNodeTrackerClient;
using namespace NNodeTrackerServer;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = ChunkServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TDataNodeTracker
    : public IDataNodeTracker
    , public TMasterAutomatonPart
{
public:
    DEFINE_SIGNAL_OVERRIDE(void(
        TNode* node,
        TReqFullHeartbeat* request,
        TRspFullHeartbeat* response),
        FullHeartbeat);
    DEFINE_SIGNAL_OVERRIDE(void(
        TNode* node,
        TReqIncrementalHeartbeat* request,
        TRspIncrementalHeartbeat* response),
        IncrementalHeartbeat);
    DEFINE_SIGNAL_OVERRIDE(void(
        TNode* node,
        int mediumIndex,
        i64 oldTokenCount,
        i64 newTokenCount),
        NodeConsistentReplicaPlacementTokensRedistributed);

public:
    explicit TDataNodeTracker(TBootstrap* bootstrap)
        : TMasterAutomatonPart(bootstrap, EAutomatonThreadQueue::DataNodeTracker)
    {
        RegisterMethod(BIND(&TDataNodeTracker::HydraIncrementalDataNodeHeartbeat, Unretained(this)));
        RegisterMethod(BIND(&TDataNodeTracker::HydraFullDataNodeHeartbeat, Unretained(this)));
        RegisterMethod(BIND(&TDataNodeTracker::HydraRedistributeConsistentReplicaPlacementTokens, Unretained(this)));
    }

    void Initialize() override
    {
        const auto& configManager = Bootstrap_->GetConfigManager();
        configManager->SubscribeConfigChanged(BIND(&TDataNodeTracker::OnDynamicConfigChanged, MakeWeak(this)));

        RedistributeConsistentReplicaPlacementTokensExecutor_ =
            New<TPeriodicExecutor>(
                Bootstrap_->GetHydraFacade()->GetAutomatonInvoker(EAutomatonThreadQueue::DataNodeTracker),
                BIND(&TDataNodeTracker::OnRedistributeConsistentReplicaPlacementTokens, MakeWeak(this)));
        RedistributeConsistentReplicaPlacementTokensExecutor_->Start();
    }

    void ProcessFullHeartbeat(TCtxFullHeartbeatPtr context) override
    {
        auto mutation = CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            context,
            &TDataNodeTracker::HydraFullDataNodeHeartbeat,
            this);
        CommitMutationWithSemaphore(std::move(mutation), std::move(context), FullHeartbeatSemaphore_);
    }

    void ProcessFullHeartbeat(
        TNode* node,
        TReqFullHeartbeat* request,
        TRspFullHeartbeat* response) override
    {
        YT_VERIFY(node->IsDataNode() || node->IsExecNode());

        const auto& chunkManager = Bootstrap_->GetChunkManager();
        auto& statistics = *request->mutable_statistics();
        node->SetDataNodeStatistics(std::move(statistics), chunkManager);

        // Calculating the exact CRP token count for a node is hard because it
        // requires analyzing total space distribution for all nodes. This is
        // done periodically. In the meantime, use an estimate based on the
        // distribution generated by recent recalculation.
        node->ConsistentReplicaPlacementTokenCount().clear();
        for (auto [mediumIndex, totalSpace] : node->TotalSpace()) {
            if (totalSpace == 0) {
                continue;
            }
            auto tokenCount = EstimateNodeConsistentReplicaPlacementTokenCount(node, mediumIndex);
            YT_VERIFY(tokenCount > 0);
            node->ConsistentReplicaPlacementTokenCount()[mediumIndex] = tokenCount;
        }

        const auto& nodeTracker = Bootstrap_->GetNodeTracker();
        nodeTracker->OnNodeHeartbeat(node, ENodeHeartbeatType::Data);

        FullHeartbeat_.Fire(node, request, response);
    }

    void ProcessIncrementalHeartbeat(TCtxIncrementalHeartbeatPtr context) override
    {
        auto mutation = CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            context,
            &TDataNodeTracker::HydraIncrementalDataNodeHeartbeat,
            this);
        CommitMutationWithSemaphore(std::move(mutation), std::move(context), IncrementalHeartbeatSemaphore_);
    }

    void ProcessIncrementalHeartbeat(
        TNode* node,
        TReqIncrementalHeartbeat* request,
        TRspIncrementalHeartbeat* response) override
    {
        YT_VERIFY(node->IsDataNode() || node->IsExecNode());

        const auto& chunkManager = Bootstrap_->GetChunkManager();
        auto& statistics = *request->mutable_statistics();
        node->SetDataNodeStatistics(std::move(statistics), chunkManager);
        const auto& nodeTracker = Bootstrap_->GetNodeTracker();
        nodeTracker->OnNodeHeartbeat(node, ENodeHeartbeatType::Data);

        const auto& multicellManager = Bootstrap_->GetMulticellManager();
        if (multicellManager->IsPrimaryMaster()) {
            node->SetDisableWriteSessionsReportedByNode(request->write_sessions_disabled());

            response->set_disable_write_sessions(node->GetDisableWriteSessions());
            node->SetDisableWriteSessionsSentToNode(node->GetDisableWriteSessions());
        }

        IncrementalHeartbeat_.Fire(node, request, response);
    }

private:
    const TAsyncSemaphorePtr FullHeartbeatSemaphore_ = New<TAsyncSemaphore>(0);
    const TAsyncSemaphorePtr IncrementalHeartbeatSemaphore_ = New<TAsyncSemaphore>(0);

    TPeriodicExecutorPtr RedistributeConsistentReplicaPlacementTokensExecutor_;
    TMediumMap<std::vector<i64>> ConsistentReplicaPlacementTokenDistribution_;

    void HydraIncrementalDataNodeHeartbeat(
        const TCtxIncrementalHeartbeatPtr& /*context*/,
        TReqIncrementalHeartbeat* request,
        TRspIncrementalHeartbeat* response)
    {
        auto nodeId = request->node_id();
        auto& statistics = *request->mutable_statistics();

        const auto& nodeTracker = Bootstrap_->GetNodeTracker();
        auto* node = nodeTracker->GetNodeOrThrow(nodeId);

        node->ValidateRegistered();

        if (!node->ReportedDataNodeHeartbeat()) {
            THROW_ERROR_EXCEPTION(
                NNodeTrackerClient::EErrorCode::InvalidState,
                "Cannot process an incremental data node heartbeat until full data node heartbeat is sent");
        }

        YT_PROFILE_TIMING("/node_tracker/incremental_data_node_heartbeat_time") {
            YT_LOG_DEBUG_IF(IsMutationLoggingEnabled(), "Processing incremental data node heartbeat (NodeId: %v, Address: %v, State: %v, %v",
                nodeId,
                node->GetDefaultAddress(),
                node->GetLocalState(),
                statistics);

            nodeTracker->UpdateLastSeenTime(node);

            ProcessIncrementalHeartbeat(node, request, response);
        }
    }

    void HydraFullDataNodeHeartbeat(
        const TCtxFullHeartbeatPtr& /*context*/,
        TReqFullHeartbeat* request,
        TRspFullHeartbeat* response)
    {
        auto nodeId = request->node_id();
        auto& statistics = *request->mutable_statistics();

        const auto& nodeTracker = Bootstrap_->GetNodeTracker();
        auto* node = nodeTracker->GetNodeOrThrow(nodeId);

        node->ValidateRegistered();

        if (node->ReportedDataNodeHeartbeat()) {
            THROW_ERROR_EXCEPTION(
                NNodeTrackerClient::EErrorCode::InvalidState,
                "Full data node heartbeat is already sent");
        }

        YT_PROFILE_TIMING("/node_tracker/full_data_node_heartbeat_time") {
            YT_LOG_DEBUG_IF(IsMutationLoggingEnabled(), "Processing full data node heartbeat (NodeId: %v, Address: %v, State: %v, %v",
                nodeId,
                node->GetDefaultAddress(),
                node->GetLocalState(),
                statistics);

            nodeTracker->UpdateLastSeenTime(node);

            ProcessFullHeartbeat(node, request, response);
        }
    }

    void OnRedistributeConsistentReplicaPlacementTokens()
    {
        if (!IsLeader()) {
            return;
        }

        NProto::TReqRedistributeConsistentReplicaPlacementTokens request;
        auto mutation = CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            request,
            &TDataNodeTracker::HydraRedistributeConsistentReplicaPlacementTokens,
            this);
        mutation->Commit();
    }

    void HydraRedistributeConsistentReplicaPlacementTokens(
        NProto::TReqRedistributeConsistentReplicaPlacementTokens* /*request*/)
    {
        auto setNodeTokenCount = [&] (TNode* node, int mediumIndex, int newTokenCount) {
            auto& currentTokenCount = node->ConsistentReplicaPlacementTokenCount()[mediumIndex];
            if (currentTokenCount == newTokenCount) {
                return;
            }

            auto oldTokenCount = currentTokenCount;
            currentTokenCount = newTokenCount;

            YT_LOG_DEBUG("Node CRP token count changed (NodeId: %v, Address: %v, MediumIndex: %v, OldTokenCount: %v, NewTokenCount: %v)",
                node->GetId(),
                node->GetDefaultAddress(),
                mediumIndex,
                oldTokenCount,
                newTokenCount);

            NodeConsistentReplicaPlacementTokensRedistributed_.Fire(node, mediumIndex, oldTokenCount, newTokenCount);
        };

        const auto& nodeTracker = Bootstrap_->GetNodeTracker();
        const auto& chunkManager = Bootstrap_->GetChunkManager();

        for (auto& [_, mediumDistribution] : ConsistentReplicaPlacementTokenDistribution_) {
            mediumDistribution.clear();
        }

        std::vector<std::pair<i64, TNode*>> nodesByTotalSpace;
        nodesByTotalSpace.reserve(nodeTracker->Nodes().size());

        for (auto [_, medium] : chunkManager->Media()) {
            if (!IsObjectAlive(medium)) {
                continue;
            }

            if (medium->GetCache()) {
                continue;
            }

            auto mediumIndex = medium->GetIndex();
            auto& mediumDistribution = ConsistentReplicaPlacementTokenDistribution_[mediumIndex];

            for (auto [_, node] : nodeTracker->Nodes()) {
                if (!IsObjectAlive(node)) {
                    continue;
                }

                if (!node->IsValidWriteTarget()) {
                    // Ignore: the node has already been removed from the ring.
                    continue;
                }

                auto it = node->TotalSpace().find(mediumIndex);
                if (it == node->TotalSpace().end() || it->second == 0) {
                    setNodeTokenCount(node, mediumIndex, 0);
                    continue;
                }

                nodesByTotalSpace.emplace_back(it->second, node);
            }

            std::sort(nodesByTotalSpace.begin(), nodesByTotalSpace.end(), std::greater<>());

            const auto bucketCount = GetDynamicConsistentReplicaPlacementConfig()->TokenDistributionBucketCount;
            auto nodesPerBucket = std::ssize(nodesByTotalSpace) / bucketCount;

            for (auto i = 0; i < bucketCount; ++i) {
                auto bucketBeginIndex = std::min(i * nodesPerBucket, ssize(nodesByTotalSpace));
                auto bucketEndIndex = std::min(bucketBeginIndex + nodesPerBucket, ssize(nodesByTotalSpace));

                auto bucketBegin = nodesByTotalSpace.begin();
                std::advance(bucketBegin, bucketBeginIndex);
                auto bucketEnd = nodesByTotalSpace.begin();
                std::advance(bucketEnd, bucketEndIndex);

                for (auto it = bucketBegin; it != bucketEnd; ++it) {
                    if (it == bucketBegin) {
                        mediumDistribution.push_back(it->first);
                    }

                    auto* node = it->second;
                    auto newTokenCount = GetTokenCountFromBucketNumber(bucketCount - i - 1);

                    setNodeTokenCount(node, mediumIndex, newTokenCount);
                }
            }

            nodesByTotalSpace.clear();
        }
    }

    int EstimateNodeConsistentReplicaPlacementTokenCount(TNode* node, int mediumIndex)
    {
        auto it = ConsistentReplicaPlacementTokenDistribution_.find(mediumIndex);
        if (it == ConsistentReplicaPlacementTokenDistribution_.end() || it->second.empty()) {
            // Either this is a first node to be placed with this medium or the
            // distribution has not been recomputed yet (which happens periodically).
            // In any case, it's too early to bother with any balancing.
            auto bucket = GetDynamicConsistentReplicaPlacementConfig()->TokenDistributionBucketCount / 2;
            return GetTokenCountFromBucketNumber(bucket);
        }

        auto& mediumDistribution = it->second;

        auto nodeTotalSpace = GetOrCrash(node->TotalSpace(), mediumIndex);
        YT_VERIFY(nodeTotalSpace != 0);

        auto bucket = 0;
        // NB: binary search could've been used here, but the distribution is very small.
        for (auto it = mediumDistribution.rbegin(); it != mediumDistribution.rend(); ++it) {
            if (nodeTotalSpace <= *it) {
                break;
            }
            ++bucket;
        }
        return GetTokenCountFromBucketNumber(bucket);
    }

    int GetTokenCountFromBucketNumber(int bucket) const
    {
        const auto& config = GetDynamicConsistentReplicaPlacementConfig();
        return std::max<int>(1, (bucket + 1) * config->TokensPerNode);
    }

    void CommitMutationWithSemaphore(
        std::unique_ptr<TMutation> mutation,
        NRpc::IServiceContextPtr context,
        const TAsyncSemaphorePtr& semaphore)
    {
        auto handler = BIND([mutation = std::move(mutation), context = std::move(context)] (TAsyncSemaphoreGuard) {
            Y_UNUSED(WaitFor(mutation->CommitAndReply(context)));
        });

        semaphore->AsyncAcquire(handler, EpochAutomatonInvoker_);
    }

    const TDynamicDataNodeTrackerConfigPtr& GetDynamicConfig() const
    {
        return Bootstrap_->GetConfigManager()->GetConfig()->ChunkManager->DataNodeTracker;
    }

    const TDynamicConsistentReplicaPlacementConfigPtr& GetDynamicConsistentReplicaPlacementConfig() const
    {
        return Bootstrap_->GetConfigManager()->GetConfig()->ChunkManager->ConsistentReplicaPlacement;
    }

    void OnDynamicConfigChanged(TDynamicClusterConfigPtr /*oldConfig*/ = nullptr)
    {
        FullHeartbeatSemaphore_->SetTotal(GetDynamicConfig()->MaxConcurrentFullHeartbeats);
        IncrementalHeartbeatSemaphore_->SetTotal(GetDynamicConfig()->MaxConcurrentIncrementalHeartbeats);

        RedistributeConsistentReplicaPlacementTokensExecutor_->SetPeriod(
            GetDynamicConsistentReplicaPlacementConfig()->TokenRedistributionPeriod);
        // NB: no need to immediately handle bucket count or token-per-node count
        // changes: this will be done in due time by the periodic.
    }
};

////////////////////////////////////////////////////////////////////////////////

IDataNodeTrackerPtr CreateDataNodeTracker(TBootstrap* bootstrap)
{
    return New<TDataNodeTracker>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkServer
