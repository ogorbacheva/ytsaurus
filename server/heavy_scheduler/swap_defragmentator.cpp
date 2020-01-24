#include "swap_defragmentator.h"

#include "task.h"
#include "private.h"
#include "config.h"
#include "helpers.h"
#include "resource_vector.h"

#include <yp/server/lib/cluster/allocator.h>
#include <yp/server/lib/cluster/cluster.h>
#include <yp/server/lib/cluster/node.h>
#include <yp/server/lib/cluster/node_segment.h>
#include <yp/server/lib/cluster/pod.h>
#include <yp/server/lib/cluster/pod_disruption_budget.h>
#include <yp/server/lib/cluster/pod_set.h>

#include <yp/server/lib/objects/object_filter.h>

#include <yp/client/api/native/helpers.h>

#include <yt/core/misc/finally.h>

#include <util/random/shuffle.h>

namespace NYP::NServer::NHeavyScheduler {

using namespace NCluster;

using namespace NClient::NApi;
using namespace NClient::NApi::NNative;

using namespace NConcurrency;
using namespace NLogging;

////////////////////////////////////////////////////////////////////////////////

TInstant ParseErrorDatetime(const TError& error)
{
    return TInstant::ParseIso8601(error.Attributes().Get<TString>("datetime"));
}

////////////////////////////////////////////////////////////////////////////////

class TSwapTask
    : public TTaskBase
{
public:
    TSwapTask(
        TGuid id,
        TInstant startTime,
        TObjectCompositeId starvingPodCompositeId,
        TObjectCompositeId victimPodCompositeId)
        : TTaskBase(id, startTime)
        , StarvingPodCompositeId_(std::move(starvingPodCompositeId))
        , VictimPodCompositeId_(std::move(victimPodCompositeId))
    { }

    virtual std::vector<TObjectId> GetInvolvedPodIds() const override
    {
        return {StarvingPodCompositeId_.Id, VictimPodCompositeId_.Id};
    }

    virtual void ReconcileState(const TClusterPtr& cluster) override
    {
        YT_VERIFY(State_ == ETaskState::Active);

        auto* starvingPod = FindPod(cluster, StarvingPodCompositeId_);
        auto* victimPod = FindPod(cluster, VictimPodCompositeId_);

        if (!starvingPod) {
            YT_LOG_DEBUG("Swap task is considered finished; starving pod does not exist");
            State_ = ETaskState::Succeeded;
            return;
        }

        if (starvingPod->GetNode()) {
            YT_LOG_DEBUG("Swap task is considered finished; starving pod is scheduled");
            State_ = ETaskState::Succeeded;
            return;
        }

        if (victimPod && victimPod->Eviction().state() != NProto::EEvictionState::ES_NONE) {
            YT_LOG_DEBUG("Swap task is considered not finished; victim pod is not evicted yet");
            return;
        }

        SchedulingStatusSketchAfterVictimEviction_.Update(starvingPod);

        // Ensure at least one scheduling iteration after victim eviction.
        if (SchedulingStatusSketchAfterVictimEviction_.ErrorIterationCount >= 3) {
            YT_LOG_DEBUG(
                "Swap task is considered finished; "
                "passed at least one scheduling iteration after victim eviction");
            State_ = ETaskState::Failed;
        } else {
            YT_LOG_DEBUG(
                "Swap task is cosidered not finished; "
                "no evidence of passed scheduling iteration after victim eviction");
        }
    }

private:
    struct TSchedulingStatusSketch
    {
        int ErrorIterationCount = 0;
        TInstant LastErrorDatetime = TInstant::Zero();

        void Update(const TPod* pod)
        {
            auto error = pod->ParseSchedulingError();
            if (error.IsOK()) {
                return;
            }

            auto errorDatetime = ParseErrorDatetime(error);
            if (errorDatetime > LastErrorDatetime) {
                ++ErrorIterationCount;
            }
            LastErrorDatetime = errorDatetime;
        }
    };

    const TObjectCompositeId StarvingPodCompositeId_;
    const TObjectCompositeId VictimPodCompositeId_;

    TSchedulingStatusSketch SchedulingStatusSketchAfterVictimEviction_;
};

////////////////////////////////////////////////////////////////////////////////

ITaskPtr CreateSwapTask(const IClientPtr& client, TPod* starvingPod, TPod* victimPod)
{
    auto id = TGuid::Create();
    auto starvingPodCompositeId = GetCompositeId(starvingPod);
    auto victimPodCompositeId = GetCompositeId(victimPod);

    YT_LOG_DEBUG("Creating swap task (TaskId: %v, StarvingPod: %v, VictimPod: %v)",
        id,
        starvingPodCompositeId,
        victimPodCompositeId);

    WaitFor(RequestPodEviction(
        client,
        victimPod->GetId(),
        Format("Heavy Scheduler cluster defragmentation (TaskId: %v)", id),
        /* validateDisruptionBudget */ true))
        .ValueOrThrow();

    return New<TSwapTask>(
        std::move(id),
        TInstant::Now(),
        std::move(starvingPodCompositeId),
        std::move(victimPodCompositeId));
}

////////////////////////////////////////////////////////////////////////////////

class TSwapDefragmentator::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TSwapDefragmentatorConfigPtr config,
        IClientPtr client,
        TObjectId nodeSegment,
        bool verbose)
        : Config_(std::move(config))
        , Client_(std::move(client))
        , NodeSegment_(std::move(nodeSegment))
        , Verbose_(verbose)
    { }

    std::vector<ITaskPtr> CreateTasks(
        const TClusterPtr& cluster,
        const TDisruptionThrottlerPtr& disruptionThrottler,
        const THashSet<TObjectId>& ignorePodIds,
        int maxTaskCount,
        int currentTotalTaskCount)
    {
        auto starvingPods = FindStarvingPods(cluster);
        if (starvingPods.empty()) {
            YT_LOG_DEBUG("There are no starving pods; skipping iteration");
            return {};
        }

        Shuffle(starvingPods.begin(), starvingPods.end());

        auto podItEnd = static_cast<int>(starvingPods.size()) > Config_->StarvingPodsPerIterationLimit
            ? starvingPods.begin() + Config_->StarvingPodsPerIterationLimit
            : starvingPods.end();

        std::vector<ITaskPtr> tasks;

        VictimSearchFailureCounter_ = 0;
        auto finally = Finally([this] () {
            Profiler.Update(Profiling_.VictimSearchFailureCounter, VictimSearchFailureCounter_);
        });

        for (auto podIt = starvingPods.begin(); podIt < podItEnd; ++podIt) {
            auto* pod = *podIt;
            if (ignorePodIds.find(pod->GetId()) != ignorePodIds.end()) {
                continue;
            }

            int minSuitableNodeCount = Config_->SafeSuitableNodeCount
                + currentTotalTaskCount
                + static_cast<int>(tasks.size());
            bool hasTaskSlot = static_cast<int>(tasks.size()) < maxTaskCount;

            auto task = TryCreateSwapTask(
                cluster,
                disruptionThrottler,
                pod,
                minSuitableNodeCount,
                hasTaskSlot);

            if (task) {
                tasks.push_back(task);
            }
        }

        return tasks;
    }

private:
    const TSwapDefragmentatorConfigPtr Config_;
    const IClientPtr Client_;
    const TObjectId NodeSegment_;
    const bool Verbose_;

    int VictimSearchFailureCounter_;

    struct TProfiling
    {
        NProfiling::TSimpleGauge VictimSearchFailureCounter{"/victim_search_failure"};
    };

    TProfiling Profiling_;

    ITaskPtr TryCreateSwapTask(
        const TClusterPtr& cluster,
        const TDisruptionThrottlerPtr& disruptionThrottler,
        TPod* starvingPod,
        int minSuitableNodeCount,
        bool hasTaskSlot)
    {
        const auto& starvingPodFilteredNodesOrError = GetFilteredNodes(starvingPod);
        if (!starvingPodFilteredNodesOrError.IsOK()) {
            YT_LOG_DEBUG(starvingPodFilteredNodesOrError,
                "Error filltering starving pod suitable nodes (StarvingPodId: %v)",
                starvingPod->GetId());
            return nullptr;
        }
        const auto& starvingPodFilteredNodes = starvingPodFilteredNodesOrError.Value();

        auto starvingPodSuitableNodes = FindSuitableNodes(
            starvingPod,
            starvingPodFilteredNodes,
            /* limit */ 1);
        if (starvingPodSuitableNodes.size() > 0) {
            YT_LOG_DEBUG("Found suitable node for starving pod (PodId: %v, NodeId: %v)",
                starvingPod->GetId(),
                starvingPodSuitableNodes[0]->GetId());
            return nullptr;
        }

        auto* victimPod = FindVictimPod(
            cluster,
            disruptionThrottler,
            starvingPod,
            starvingPodFilteredNodes,
            minSuitableNodeCount);
        if (!victimPod) {
            YT_LOG_DEBUG("Could not find victim pod (StarvingPodId: %v)",
                starvingPod->GetId());
            ++VictimSearchFailureCounter_;
            return nullptr;
        }

        YT_LOG_DEBUG("Found victim pod (PodId: %v, StarvingPodId: %v)",
            victimPod->GetId(),
            starvingPod->GetId());

        if (hasTaskSlot) {
            disruptionThrottler->RegisterPodEviction(victimPod);
            return CreateSwapTask(
                Client_,
                starvingPod,
                victimPod);
        } else {
            YT_LOG_DEBUG("Failed to create swap task: concurrent task limit reached for swap defragmentator "
                "(VictimPodId: %v, StarvingPodId: %v)",
                victimPod->GetId(),
                starvingPod->GetId());
            return nullptr;
        }
    }

    std::vector<TPod*> FindStarvingPods(const TClusterPtr& cluster) const
    {
        std::vector<TPod*> result;
        for (auto* pod : GetNodeSegmentSchedulablePods(cluster, NodeSegment_)) {
            if (pod->GetNode()) {
                continue;
            }
            if (!pod->ParseSchedulingError().IsOK()) {
                result.push_back(pod);
            }
        }
        YT_LOG_DEBUG_UNLESS(result.empty(), "Found starving pods (Count: %v)",
            result.size());
        return result;
    }

    TPod* FindVictimPod(
        const TClusterPtr& cluster,
        const TDisruptionThrottlerPtr& disruptionThrottler,
        TPod* starvingPod,
        const std::vector<TNode*>& starvingPodFilteredNodes,
        int minSuitableNodeCount) const
    {
        THashSet<TNode*> starvingPodFilteredNodeSet;
        for (auto* node : starvingPodFilteredNodes) {
            starvingPodFilteredNodeSet.insert(node);
        }

        std::vector<TPod*> victimCandidatePods = GetNodeSegmentSchedulablePods(
            cluster,
            NodeSegment_);
        victimCandidatePods.erase(
            std::remove_if(
                victimCandidatePods.begin(),
                victimCandidatePods.end(),
                [&] (TPod* pod) {
                    return !pod->GetNode() ||
                        starvingPodFilteredNodeSet.find(pod->GetNode()) == starvingPodFilteredNodeSet.end();
                }),
            victimCandidatePods.end());

        if (static_cast<int>(victimCandidatePods.size()) > Config_->VictimCandidatePodCount) {
            YT_LOG_DEBUG("Randomly selecting victim candidates (TotalCount: %v, RandomSelectionCount: %v)",
                victimCandidatePods.size(),
                Config_->VictimCandidatePodCount);
            Shuffle(victimCandidatePods.begin(), victimCandidatePods.end());
            victimCandidatePods.resize(Config_->VictimCandidatePodCount);
        }

        YT_LOG_DEBUG("Selected victim pod candidates (Count: %v)",
            victimCandidatePods.size());

        for (auto* victimPod : victimCandidatePods) {
            auto* node = victimPod->GetNode();

            if (!node->CanAllocateAntiaffinityVacancies(starvingPod)) {
                YT_LOG_DEBUG_IF(Verbose_,
                    "Not enough antiaffinity vacancies (NodeId: %v, StarvingPodId: %v)",
                    node->GetId(),
                    starvingPod->GetId());
                continue;
            }

            auto starvingPodResourceVector = GetResourceRequestVector(starvingPod);
            auto victimPodResourceVector = GetResourceRequestVector(victimPod);
            auto freeNodeResourceVector = GetFreeResourceVector(node);
            if (freeNodeResourceVector + victimPodResourceVector < starvingPodResourceVector) {
                YT_LOG_DEBUG_IF(Verbose_,
                    "Not enough resources according to resource vectors (NodeId: %v, VictimPodId: %v, StarvingPodId: %v)",
                    node->GetId(),
                    victimPod->GetId(),
                    starvingPod->GetId());
                continue;
            }

            YT_LOG_DEBUG_IF(Verbose_,
                "Checking eviction safety (PodId: %v)",
                victimPod->GetId());
            if (disruptionThrottler->ThrottleEviction(victimPod)
                || !HasEnoughSuitableNodes(victimPod, minSuitableNodeCount, Verbose_))
            {
                continue;
            }

            return victimPod;
        }

        return nullptr;
    }
};

////////////////////////////////////////////////////////////////////////////////

TSwapDefragmentator::TSwapDefragmentator(
    TSwapDefragmentatorConfigPtr config,
    IClientPtr client,
    TObjectId nodeSegment,
    bool verbose)
    : Impl_(New<TImpl>(std::move(config), std::move(client), std::move(nodeSegment), verbose))
{ }

std::vector<ITaskPtr> TSwapDefragmentator::CreateTasks(
    const TClusterPtr& cluster,
    const TDisruptionThrottlerPtr& disruptionThrottler,
    const THashSet<TObjectId>& ignorePodIds,
    int maxTaskCount,
    int currentTotalTaskCount)
{
    return Impl_->CreateTasks(
        cluster,
        disruptionThrottler,
        ignorePodIds,
        maxTaskCount,
        currentTotalTaskCount);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NServer::NHeavyScheduler
