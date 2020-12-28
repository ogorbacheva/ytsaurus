#include "fair_share_tree.h"
#include "fair_share_tree_element.h"
#include "persistent_scheduler_state.h"
#include "public.h"
#include "pools_config_parser.h"
#include "resource_tree.h"
#include "scheduler_strategy.h"
#include "scheduler_tree.h"
#include "scheduling_context.h"
#include "scheduling_segment_manager.h"
#include "fair_share_strategy_operation_controller.h"

#include <yt/server/lib/scheduler/config.h>
#include <yt/server/lib/scheduler/job_metrics.h>
#include <yt/server/lib/scheduler/resource_metering.h>
#include <yt/server/lib/scheduler/structs.h>

#include <yt/ytlib/scheduler/job_resources.h>

#include <yt/core/concurrency/async_rw_lock.h>
#include <yt/core/concurrency/periodic_executor.h>
#include <yt/core/concurrency/thread_pool.h>

#include <yt/core/misc/algorithm_helpers.h>
#include <yt/core/misc/finally.h>

#include <yt/core/profiling/profile_manager.h>
#include <yt/core/profiling/timing.h>
#include <yt/core/profiling/metrics_accumulator.h>

namespace NYT::NScheduler {

using namespace NConcurrency;
using namespace NJobTrackerClient;
using namespace NNodeTrackerClient;
using namespace NObjectClient;
using namespace NYson;
using namespace NYTree;
using namespace NProfiling;
using namespace NControllerAgent;

////////////////////////////////////////////////////////////////////////////////

TFairShareStrategyOperationState::TFairShareStrategyOperationState(
    IOperationStrategyHost* host,
    const TFairShareStrategyOperationControllerConfigPtr& config)
    : Host_(host)
    , Controller_(New<TFairShareStrategyOperationController>(host, config))
{ }

TPoolName TFairShareStrategyOperationState::GetPoolNameByTreeId(const TString& treeId) const
{
    return GetOrCrash(TreeIdToPoolNameMap_, treeId);
}

void TFairShareStrategyOperationState::UpdateConfig(const TFairShareStrategyOperationControllerConfigPtr& config)
{
    Controller_->UpdateConfig(config);
}

////////////////////////////////////////////////////////////////////////////////

THashMap<TString, TPoolName> GetOperationPools(const TOperationRuntimeParametersPtr& runtimeParameters)
{
    THashMap<TString, TPoolName> pools;
    for (const auto& [treeId, options] : runtimeParameters->SchedulingOptionsPerPoolTree) {
        pools.emplace(treeId, options->Pool);
    }
    return pools;
}

////////////////////////////////////////////////////////////////////////////////

//! This class represents fair share tree.
//!
//! We maintain following entities:
//!
//!   * Actual tree, it contains the latest and consistent stucture of pools and operations.
//!     This tree represented by fields #RootElement_, #OperationIdToElement_, #Pools_.
//!     Update of this tree performed in sequentual manner from #Control thread.
//!
//!   * Snapshot of the tree with scheduling attributes (fair share ratios, best leaf descendants et. c).
//!     It is built repeatedly from actual tree by taking snapshot and calculating scheduling attributes.
//!     Clones of this tree are used in heartbeats for scheduling. Also, element attributes from this tree
//!     are used in orchid, for logging and for profiling.
//!     This tree represented by #RootElementSnapshot_.
//!     NB: elements of this tree may be invalidated by #Alive flag in resource tree. In this case element cannot be safely used
//!     (corresponding operation or pool can be already deleted from all other scheduler structures).
//!
//!   * Resource tree, it is thread safe tree that maintain shared attributes of tree elements.
//!     More details can be find at #TResourceTree.
class TFairShareTree
    : public ISchedulerTree
    , public IFairShareTreeHost
{
public:
    using TFairShareTreePtr = TIntrusivePtr<TFairShareTree>;

    struct TJobWithPreemptionInfo
    {
        TJobPtr Job;
        bool IsPreemptable = false;
        TOperationElementPtr OperationElement;
    };

public:
    TFairShareTree(
        TFairShareStrategyTreeConfigPtr config,
        TFairShareStrategyOperationControllerConfigPtr controllerConfig,
        ISchedulerStrategyHost* strategyHost,
        ISchedulerTreeHost* treeHost,
        std::vector<IInvokerPtr> feasibleInvokers,
        TString treeId)
        : Config_(std::move(config))
        , ControllerConfig_(std::move(controllerConfig))
        , ResourceTree_(New<TResourceTree>(config))
        , StrategyHost_(strategyHost)
        , TreeHost_(treeHost)
        , FeasibleInvokers_(std::move(feasibleInvokers))
        , TreeId_(std::move(treeId))
        , TreeProfiler_(SchedulerProfiler.WithRequiredTag("tree", TreeId_))
        , Logger(NLogging::TLogger(SchedulerLogger)
            .AddTag("TreeId: %v", TreeId_))
        , NonPreemptiveSchedulingStage_(
            /* nameInLogs */ "Non preemptive",
            TreeProfiler_.WithPrefix("/non_preemptive"))
        , AggressivelyPreemptiveSchedulingStage_(
            /* nameInLogs */ "Aggressively preemptive",
            TreeProfiler_.WithPrefix("/aggressively_preemptive"))
        , PreemptiveSchedulingStage_(
            /* nameInLogs */ "Preemptive",
            TreeProfiler_.WithPrefix("/preemptive"))
        , PackingFallbackSchedulingStage_(
            /* nameInLogs */ "Packing fallback",
            TreeProfiler_.WithPrefix("/packing_fallback"))
        , FairSharePreUpdateTimer_(TreeProfiler_.Timer("/fair_share_preupdate_time"))
        , FairShareUpdateTimer_(TreeProfiler_.Timer("/fair_share_update_time"))
        , FairShareFluentLogTimer_(TreeProfiler_.Timer("/fair_share_fluent_log_time"))
        , FairShareTextLogTimer_(TreeProfiler_.Timer("/fair_share_text_log_time"))
        , PoolCountGauge_(TreeProfiler_.WithGlobal().Gauge("/pools/pool_count"))
    {
        RootElement_ = New<TRootElement>(StrategyHost_, this, Config_, GetPoolProfilingTag(RootPoolName), TreeId_, Logger);

        DoRegisterPoolProfilingCounters(RootElement_->GetId());
        RootElement_->RegisterProfiler(TreeProfiler_
            .WithGlobal()
            .WithRequiredTag("pool", RootElement_->GetId(), -1));

        YT_LOG_INFO("Fair share tree created");
    }

    virtual TFairShareStrategyTreeConfigPtr GetConfig() const override
    {
        return Config_;
    }

    virtual void UpdateConfig(const TFairShareStrategyTreeConfigPtr& config) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        if (AreNodesEqual(ConvertToNode(config), ConvertToNode(Config_))) {
            return;
        }

        Config_ = config;
        RootElement_->UpdateTreeConfig(Config_);
        ResourceTree_->UpdateConfig(Config_);

        if (!FindPool(Config_->DefaultParentPool) && Config_->DefaultParentPool != RootPoolName) {
            auto error = TError("Default parent pool %Qv in tree %Qv is not registered", Config_->DefaultParentPool, TreeId_);
            StrategyHost_->SetSchedulerAlert(ESchedulerAlertType::UpdatePools, error);
        }
    }

    virtual void UpdateControllerConfig(const TFairShareStrategyOperationControllerConfigPtr& config) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        ControllerConfig_ = config;

        for (const auto& [operationId, element] : OperationIdToElement_) {
            element->UpdateControllerConfig(config);
        }
    }

    virtual const TSchedulingTagFilter& GetNodesFilter() const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        return Config_->NodesFilter;
    }

    // NB: This function is public for scheduler simulator.
    virtual TFuture<std::pair<IFairShareTreeSnapshotPtr, TError>> OnFairShareUpdateAt(TInstant now) override
    {
        return BIND(&TFairShareTree::DoFairShareUpdateAt, MakeStrong(this), now)
            .AsyncVia(GetCurrentInvoker())
            .Run();
    }

    virtual void FinishFairShareUpdate() override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        YT_VERIFY(RootElementSnapshotPrecommit_);
        RootElementSnapshot_ = std::move(RootElementSnapshotPrecommit_);
        RootElementSnapshotPrecommit_.Reset();
    }

    virtual bool HasOperation(TOperationId operationId) const override
    {
        return static_cast<bool>(FindOperationElement(operationId));
    }

    virtual bool HasRunningOperation(TOperationId operationId) const override
    {
        if (auto element = FindOperationElement(operationId)) {
            return element->IsOperationRunningInPool();
        }
        return false;
    }

    virtual int GetOperationCount() const override
    {
        return OperationIdToElement_.size();
    }

    virtual void RegisterOperation(
        const TFairShareStrategyOperationStatePtr& state,
        const TStrategyOperationSpecPtr& spec,
        const TOperationFairShareTreeRuntimeParametersPtr& runtimeParameters) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        auto operationId = state->GetHost()->GetId();

        auto operationElement = New<TOperationElement>(
            Config_,
            spec,
            runtimeParameters,
            state->GetController(),
            ControllerConfig_,
            StrategyHost_,
            this,
            state->GetHost(),
            TreeId_,
            Logger);

        int index = RegisterSchedulingTagFilter(TSchedulingTagFilter(spec->SchedulingTagFilter));
        operationElement->SetSchedulingTagFilterIndex(index);

        YT_VERIFY(OperationIdToElement_.emplace(operationId, operationElement).second);

        auto poolName = state->GetPoolNameByTreeId(TreeId_);
        auto pool = GetOrCreatePool(poolName, state->GetHost()->GetAuthenticatedUser());

        operationElement->AttachParent(pool.Get(), /* enabled */ false);

        bool isRunningInPool = OnOperationAddedToPool(state, operationElement);
        if (isRunningInPool) {
            TreeHost_->OnOperationRunningInTree(operationId, this);
        }

        YT_LOG_INFO("Operation element registered in tree (OperationId: %v, Pool: %v, MarkedAsRunning: %v)",
            operationId,
            poolName.ToString(),
            isRunningInPool);
    }

    virtual void UnregisterOperation(const TFairShareStrategyOperationStatePtr& state) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        auto operationId = state->GetHost()->GetId();
        auto operationElement = GetOperationElement(operationId);

        auto* pool = operationElement->GetMutableParent();

        // Profile finished operation.
        ProfileOperationUnregistration(pool, state->GetHost()->GetState());

        operationElement->Disable(/* markAsNonAlive */ true);
        operationElement->DetachParent();

        OnOperationRemovedFromPool(state, operationElement, pool);

        UnregisterSchedulingTagFilter(operationElement->GetSchedulingTagFilterIndex());

        YT_VERIFY(OperationIdToElement_.erase(operationId) == 1);

        // Operation can be missing in these maps.
        OperationIdToActivationTime_.erase(operationId);
        OperationIdToFirstFoundLimitingAncestorTime_.erase(operationId);
    }

    virtual void EnableOperation(const TFairShareStrategyOperationStatePtr& state) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        auto operationId = state->GetHost()->GetId();
        auto operationElement = GetOperationElement(operationId);

        operationElement->GetMutableParent()->EnableChild(operationElement);

        operationElement->Enable();
    }

    virtual void DisableOperation(const TFairShareStrategyOperationStatePtr& state) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        auto operationElement = GetOperationElement(state->GetHost()->GetId());
        operationElement->Disable(/* markAsNonAlive */ false);
        operationElement->GetMutableParent()->DisableChild(operationElement);
    }

    virtual void ChangeOperationPool(
        TOperationId operationId,
        const TFairShareStrategyOperationStatePtr& state,
        const TPoolName& newPool) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        auto element = FindOperationElement(operationId);
        if (!element) {
            THROW_ERROR_EXCEPTION("Operation element for operation %Qv not found", operationId);
        }
        bool operationWasRunning = element->IsOperationRunningInPool();

        auto oldParent = element->GetMutableParent();
        auto newParent = GetOrCreatePool(newPool, state->GetHost()->GetAuthenticatedUser());
        element->ChangeParent(newParent.Get());

        OnOperationRemovedFromPool(state, element, oldParent);

        YT_VERIFY(OnOperationAddedToPool(state, element));

        if (!operationWasRunning) {
            TreeHost_->OnOperationRunningInTree(operationId, this);
        }
    }

    virtual void UpdateOperationRuntimeParameters(
        TOperationId operationId,
        const TOperationFairShareTreeRuntimeParametersPtr& runtimeParameters) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        if (const auto& element = FindOperationElement(operationId)) {
            element->SetRuntimeParameters(runtimeParameters);
        }
    }

    virtual void RegisterJobsFromRevivedOperation(TOperationId operationId, const std::vector<TJobPtr>& jobs) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        const auto& element = FindOperationElement(operationId);
        for (const auto& job : jobs) {
            element->OnJobStarted(
                job->GetId(),
                job->ResourceUsage(),
                /* precommittedResources */ {},
                /* force */ true);
        }
    }

    virtual TError CheckOperationIsHung(
        TOperationId operationId,
        TDuration safeTimeout,
        int minScheduleJobCallAttempts,
        const THashSet<EDeactivationReason>& deactivationReasons,
        TDuration limitingAncestorSafeTimeout,
        const TJobResources& minNeededResources)
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        // TODO(ignat): Could we guarantee that operation must be in tree?
        auto element = FindRecentOperationElementSnapshot(operationId);
        if (!element) {
            return TError();
        }

        auto now = TInstant::Now();
        TInstant activationTime;
        {
            auto it = OperationIdToActivationTime_.find(operationId);
            if (!element->Attributes().Alive) {
                if (it != OperationIdToActivationTime_.end()) {
                    it->second = TInstant::Max();
                }
                return TError();
            } else {
                if (it == OperationIdToActivationTime_.end()) {
                    activationTime = now;
                    OperationIdToActivationTime_.emplace(operationId, now);
                } else {
                    it->second = std::min(it->second, now);
                    activationTime = it->second;
                }
            }
        }

        // We only want to find the operations that are hanged due to poorly configured resource limits or a custom
        // scheduling tag filter. Node shortage, e.g. due to a bulk restart, shouldn't fail the operation. See: YT-13329.
        bool canFitIntoTotalResources = RootElementSnapshot_ &&
            Dominates(RootElementSnapshot_->RootElement->GetTotalResourceLimits(), minNeededResources);
        bool shouldCheckLimitingAncestor = canFitIntoTotalResources &&
            Config_->EnableLimitingAncestorCheck &&
            element->IsLimitingAncestorCheckEnabled();
        if (shouldCheckLimitingAncestor) {
            auto it = OperationIdToFirstFoundLimitingAncestorTime_.find(operationId);

            // NB(eshcherbin): Here we rely on the fact that |element->ResourceLimits_| is infinite
            // if the element is not in the fair share tree snapshot yet.
            if (auto* limitingAncestor = FindAncestorWithInsufficientResourceLimits(element, minNeededResources)) {
                TInstant firstFoundLimitingAncestorTime;
                if (it == OperationIdToFirstFoundLimitingAncestorTime_.end()) {
                    firstFoundLimitingAncestorTime = now;
                    OperationIdToFirstFoundLimitingAncestorTime_.emplace(operationId, now);
                } else {
                    it->second = std::min(it->second, now);
                    firstFoundLimitingAncestorTime = it->second;
                }

                if (activationTime + limitingAncestorSafeTimeout < now &&
                    firstFoundLimitingAncestorTime + limitingAncestorSafeTimeout < now)
                {
                    return TError("Operation has an ancestor whose resource limits are too small to satisfy operation's minimum job resource demand")
                        << TErrorAttribute("safe_timeout", limitingAncestorSafeTimeout)
                        << TErrorAttribute("limiting_ancestor", limitingAncestor->GetId())
                        << TErrorAttribute("resource_limits", limitingAncestor->ResourceLimits())
                        << TErrorAttribute("min_needed_resources", minNeededResources);
                }
            } else if (it != OperationIdToFirstFoundLimitingAncestorTime_.end()) {
                it->second = TInstant::Max();
            }
        }

        int deactivationCount = 0;
        auto deactivationReasonToCount = element->GetDeactivationReasonsFromLastNonStarvingTime();
        for (auto reason : deactivationReasons) {
            deactivationCount += deactivationReasonToCount[reason];
        }

        if (activationTime + safeTimeout < now &&
            element->GetLastScheduleJobSuccessTime() + safeTimeout < now &&
            element->GetLastNonStarvingTime() + safeTimeout < now &&
            element->GetRunningJobCount() == 0 &&
            deactivationCount > minScheduleJobCallAttempts)
        {
            return TError("Operation has no successful scheduled jobs for a long period")
                << TErrorAttribute("period", safeTimeout)
                << TErrorAttribute("deactivation_count", deactivationCount)
                << TErrorAttribute("last_schedule_job_success_time", element->GetLastScheduleJobSuccessTime())
                << TErrorAttribute("last_non_starving_time", element->GetLastNonStarvingTime());
        }

        return TError();
    }

    virtual void ProcessActivatableOperations() override
    {
        while (!ActivatableOperationIds_.empty()) {
            auto operationId = ActivatableOperationIds_.back();
            ActivatableOperationIds_.pop_back();
            TreeHost_->OnOperationRunningInTree(operationId, this);
        }
    }

    virtual void TryRunAllPendingOperations() override
    {
        std::vector<TOperationId> readyOperationIds;
        std::vector<std::pair<TOperationElementPtr, TCompositeSchedulerElement*>> stillPending;
        for (const auto& [_, pool] : Pools_) {
            for (auto pendingOperationId : pool->PendingOperationIds()) {
                if (auto element = FindOperationElement(pendingOperationId)) {
                    YT_VERIFY(!element->IsOperationRunningInPool());
                    if (auto violatingPool = FindPoolViolatingMaxRunningOperationCount(element->GetMutableParent())) {
                        stillPending.emplace_back(std::move(element), violatingPool);
                    } else {
                        element->MarkOperationRunningInPool();
                        readyOperationIds.push_back(pendingOperationId);
                    }
                }
            }
            pool->PendingOperationIds().clear();
        }

        for (const auto& [operation, pool] : stillPending) {
            operation->MarkPendingBy(pool);
        }

        for (auto operationId : readyOperationIds) {
            TreeHost_->OnOperationRunningInTree(operationId, this);
        }
    }

    virtual TPoolName CreatePoolName(const std::optional<TString>& poolFromSpec, const TString& user) const override
    {
        if (!poolFromSpec) {
            return TPoolName(user, std::nullopt);
        }
        auto pool = FindPool(*poolFromSpec);
        if (pool && pool->GetConfig()->CreateEphemeralSubpools) {
            return TPoolName(user, *poolFromSpec);
        }
        return TPoolName(*poolFromSpec, std::nullopt);
    }

    virtual TPoolsUpdateResult UpdatePools(const INodePtr& poolsNode) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        if (LastPoolsNodeUpdate_ && AreNodesEqual(LastPoolsNodeUpdate_, poolsNode)) {
            YT_LOG_INFO("Pools are not changed, skipping update");
            return {LastPoolsNodeUpdateError_, false};
        }

        LastPoolsNodeUpdate_ = poolsNode;

        THashMap<TString, TString> poolToParentMap;
        THashSet<TString> ephemeralPools;
        for (const auto& [poolId, pool] : Pools_) {
            poolToParentMap[poolId] = pool->GetParent()->GetId();
            if (pool->IsDefaultConfigured()) {
                ephemeralPools.insert(poolId);
            }
        }

        TPoolsConfigParser poolsConfigParser(std::move(poolToParentMap), std::move(ephemeralPools));

        TError parseResult = poolsConfigParser.TryParse(poolsNode);
        if (!parseResult.IsOK()) {
            auto wrappedError = TError("Found pool configuration issues in tree %Qv; update skipped", TreeId_)
                << parseResult;
            LastPoolsNodeUpdateError_ = wrappedError;
            return {wrappedError, false};
        }

        // Parsing is succeeded. Applying new structure.
        for (const auto& updatePoolAction : poolsConfigParser.GetOrderedUpdatePoolActions()) {
            switch (updatePoolAction.Type) {
                case EUpdatePoolActionType::Create: {
                    auto pool = New<TPool>(
                        StrategyHost_,
                        this,
                        updatePoolAction.Name,
                        updatePoolAction.PoolConfig,
                        /* defaultConfigured */ false,
                        Config_,
                        GetPoolProfilingTag(updatePoolAction.Name),
                        TreeId_,
                        Logger);
                    const auto& parent = updatePoolAction.ParentName == RootPoolName
                        ? static_cast<TCompositeSchedulerElementPtr>(RootElement_)
                        : GetPool(updatePoolAction.ParentName);

                    RegisterPool(pool, parent);
                    break;
                }
                case EUpdatePoolActionType::Erase: {
                    auto pool = GetPool(updatePoolAction.Name);
                    if (pool->IsEmpty()) {
                        UnregisterPool(pool);
                    } else {
                        pool->SetDefaultConfig();

                        auto defaultParent = GetDefaultParentPool();
                        if (pool->GetId() == defaultParent->GetId()) {  // Someone is deleting default pool.
                            defaultParent = RootElement_;
                        }
                        if (pool->GetParent()->GetId() != defaultParent->GetId()) {
                            pool->ChangeParent(defaultParent.Get());
                        }
                    }
                    break;
                }
                case EUpdatePoolActionType::Move:
                case EUpdatePoolActionType::Keep: {
                    auto pool = GetPool(updatePoolAction.Name);
                    if (pool->GetUserName()) {
                        const auto& userName = pool->GetUserName().value();
                        if (pool->IsEphemeralInDefaultParentPool()) {
                            YT_VERIFY(UserToEphemeralPoolsInDefaultPool_[userName].erase(pool->GetId()) == 1);
                        }
                        pool->SetUserName(std::nullopt);
                    }
                    ReconfigurePool(pool, updatePoolAction.PoolConfig);
                    if (updatePoolAction.Type == EUpdatePoolActionType::Move) {
                        const auto& parent = updatePoolAction.ParentName == RootPoolName
                            ? static_cast<TCompositeSchedulerElementPtr>(RootElement_)
                            : GetPool(updatePoolAction.ParentName);
                        pool->ChangeParent(parent.Get());
                    }
                    break;
                }
            }
        }

        LastPoolsNodeUpdateError_ = TError();

        return {LastPoolsNodeUpdateError_, true};
    }

    virtual void ValidatePoolLimits(const IOperationStrategyHost* operation, const TPoolName& poolName) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        ValidateOperationCountLimit(operation, poolName);
        ValidateEphemeralPoolLimit(operation, poolName);
    }

    virtual void ValidatePoolLimitsOnPoolChange(const IOperationStrategyHost* operation, const TPoolName& newPoolName) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        ValidateEphemeralPoolLimit(operation, newPoolName);
        ValidateAllOperationsCountsOnPoolChange(operation->GetId(), newPoolName);
    }

    virtual TFuture<void> ValidateOperationPoolsCanBeUsed(const IOperationStrategyHost* operation, const TPoolName& poolName) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        return BIND(&TFairShareTree::DoValidateOperationPoolsCanBeUsed, MakeStrong(this))
            .AsyncVia(GetCurrentInvoker())
            .Run(operation, poolName);
    }

    virtual TPersistentTreeStatePtr BuildPersistentTreeState() const override
    {
        auto result = New<TPersistentTreeState>();
        for (const auto& [poolId, pool] : Pools_) {
            if (pool->GetIntegralGuaranteeType() != EIntegralGuaranteeType::None) {
                auto state = New<TPersistentPoolState>();
                state->AccumulatedResourceVolume = pool->GetAccumulatedResourceVolume();
                result->PoolStates.emplace(poolId, std::move(state));
            }
        }
        return result;
    }

    virtual void InitPersistentTreeState(const TPersistentTreeStatePtr& persistentTreeState) override
    {
        for (const auto& [poolName, poolState] : persistentTreeState->PoolStates) {
            auto poolIt = Pools_.find(poolName);
            if (poolIt != Pools_.end()) {
                if (poolIt->second->GetIntegralGuaranteeType() != EIntegralGuaranteeType::None) {
                    poolIt->second->InitAccumulatedResourceVolume(poolState->AccumulatedResourceVolume);
                } else {
                    YT_LOG_INFO("Pool is not integral and cannot accept integral resource volume (Pool: %v, PoolTree: %v, Volume: %v)",
                        poolName,
                        TreeId_,
                        poolState->AccumulatedResourceVolume);
                }
            } else {
                YT_LOG_INFO("Unknown pool in tree; dropping its integral resource volume (Pool: %v, PoolTree: %v, Volume: %v)",
                    poolName,
                    TreeId_,
                    poolState->AccumulatedResourceVolume);
            }
        }
    }

    virtual void InitOrUpdateOperationSchedulingSegment(TOperationId operationId) override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        GetOperationElement(operationId)->InitOrUpdateSchedulingSegment(Config_->SchedulingSegments->Mode);
    }

    virtual TPoolTreeSchedulingSegmentsInfo GetSchedulingSegmentsInfo() const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        TPoolTreeSchedulingSegmentsInfo result;
        result.Mode = Config_->SchedulingSegments->Mode;
        result.UnsatisfiedSegmentsRebalancingTimeout = Config_->SchedulingSegments->UnsatisfiedSegmentsRebalancingTimeout;

        if (result.Mode == ESegmentedSchedulingMode::Disabled) {
            return result;
        }

        auto keyResource = TSchedulingSegmentManager::GetSegmentBalancingKeyResource(result.Mode);
        result.KeyResource = keyResource;

        if (!RootElementSnapshot_) {
            return result;
        }

        for (const auto& [_, operationElement] : RootElementSnapshot_->OperationIdToElement) {
            // Segment may be unset due to a race, and in this case we silently ignore the operation.
            if (const auto& segment = operationElement->SchedulingSegment()) {
                result.FairSharePerSegment[*segment] += operationElement->Attributes().GetFairShare()[keyResource];
            }
        }

        const auto& totalResourceLimits = RootElementSnapshot_->RootElement->GetTotalResourceLimits();
        result.TotalKeyResourceAmount = GetResource(totalResourceLimits, keyResource);
        for (auto segment : TEnumTraits<ESchedulingSegment>::GetDomainValues()) {
            auto keyResourceFairAmount = result.FairSharePerSegment[segment] * result.TotalKeyResourceAmount;
            auto satisfactionMargin = Config_->SchedulingSegments->SatisfactionMargins[segment];

            result.FairResourceAmountPerSegment[segment] = std::max(keyResourceFairAmount + satisfactionMargin, 0.0);
        }

        return result;
    }

    virtual void BuildOperationAttributes(TOperationId operationId, TFluentMap fluent) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        const auto& element = GetOperationElement(operationId);
        auto serializedParams = ConvertToAttributes(element->GetRuntimeParameters());
        fluent
            .Items(*serializedParams)
            .Item("pool").Value(element->GetParent()->GetId());
    }

    virtual void BuildOperationProgress(TOperationId operationId, TFluentMap fluent) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        auto* element = FindRecentOperationElementSnapshot(operationId);
        if (!element) {
            return;
        }

        DoBuildOperationProgress(element, RootElementSnapshot_, fluent);
    }

    virtual void BuildBriefOperationProgress(TOperationId operationId, TFluentMap fluent) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        const auto& element = FindOperationElement(operationId);
        if (!element) {
            return;
        }

        auto* parent = element->GetParent();
        const auto& attributes = element->Attributes();
        fluent
            .Item("pool").Value(parent->GetId())
            .Item("weight").Value(element->GetWeight())
            .Item("fair_share_ratio").Value(MaxComponent(attributes.FairShare.Total))
            .Item("dominant_fair_share").Value(MaxComponent(attributes.FairShare.Total));
    }


    virtual void BuildUserToEphemeralPoolsInDefaultPool(TFluentAny fluent) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        fluent
            .DoMapFor(UserToEphemeralPoolsInDefaultPool_, [] (TFluentMap fluent, const auto& pair) {
                const auto& [userName, ephemeralPools] = pair;
                fluent
                    .Item(userName).Value(ephemeralPools);
            });
    }

    virtual void BuildStaticPoolsInformation(TFluentAny fluent) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        fluent
            .DoMapFor(Pools_, [&] (TFluentMap fluent, const auto& pair) {
                const auto& [poolName, pool] = pair;
                fluent
                    .Item(poolName).Value(pool->GetConfig());
            });
    }

    virtual void BuildFairShareInfo(TFluentMap fluent) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        Y_UNUSED(WaitFor(BIND(&TFairShareTree::DoBuildFairShareInfo, MakeWeak(this), RootElementSnapshot_, fluent)
            .AsyncVia(StrategyHost_->GetOrchidWorkerInvoker())
            .Run()));
    }

    virtual void BuildOrchid(TFluentMap fluent) const override
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        fluent
            .Item("resource_usage").Value(GetRecentRootSnapshot()->ResourceUsageAtUpdate())
            .Item("config").Value(Config_);
    }

    virtual TResourceTree* GetResourceTree() override
    {
        return ResourceTree_.Get();
    }

private:
    TFairShareStrategyTreeConfigPtr Config_;
    TFairShareStrategyOperationControllerConfigPtr ControllerConfig_;

    TResourceTreePtr ResourceTree_;

    ISchedulerStrategyHost* const StrategyHost_;

    ISchedulerTreeHost* TreeHost_;

    std::vector<IInvokerPtr> FeasibleInvokers_;

    INodePtr LastPoolsNodeUpdate_;
    TError LastPoolsNodeUpdateError_;

    const TString TreeId_;
    const TRegistry TreeProfiler_;

    const NLogging::TLogger Logger;

    TPoolMap Pools_;

    std::optional<TInstant> LastFairShareUpdateTime_;

    THashMap<TString, TTagId> PoolIdToProfilingTagId_;

    struct TUnregisterOperationCounters
    {
        TEnumIndexedVector<EOperationState, TCounter> FinishedCounters;
        TCounter BannedCounter;
    };
    THashMap<TString, TUnregisterOperationCounters> PoolToUnregisterOperationCounters_;

    THashMap<TString, THashSet<TString>> UserToEphemeralPoolsInDefaultPool_;

    THashMap<TString, THashSet<int>> PoolToSpareSlotIndices_;
    THashMap<TString, int> PoolToMinUnusedSlotIndex_;

    TOperationElementMap OperationIdToElement_;

    THashMap<TOperationId, TInstant> OperationIdToActivationTime_;
    THashMap<TOperationId, TInstant> OperationIdToFirstFoundLimitingAncestorTime_;

    std::vector<TOperationId> ActivatableOperationIds_;

    YT_DECLARE_SPINLOCK(TReaderWriterSpinLock, NodeIdToLastPreemptiveSchedulingTimeLock_);
    THashMap<TNodeId, TCpuInstant> NodeIdToLastPreemptiveSchedulingTime_;

    YT_DECLARE_SPINLOCK(TReaderWriterSpinLock, RegisteredSchedulingTagFiltersLock_);
    std::vector<TSchedulingTagFilter> RegisteredSchedulingTagFilters_;
    std::vector<int> FreeSchedulingTagFilterIndexes_;
    struct TSchedulingTagFilterEntry
    {
        int Index;
        int Count;
    };
    THashMap<TSchedulingTagFilter, TSchedulingTagFilterEntry> SchedulingTagFilterToIndexAndCount_;

    TRootElementPtr RootElement_;

    struct TRootElementSnapshot
        : public TRefCounted
    {
        TRootElementPtr RootElement;
        TRawOperationElementMap OperationIdToElement;
        TRawOperationElementMap DisabledOperationIdToElement;
        TRawPoolMap PoolNameToElement;
        THashMap<TString, int> ElementIndexes;
        TFairShareStrategyTreeConfigPtr Config;
        TFairShareStrategyOperationControllerConfigPtr ControllerConfig;
        bool CoreProfilingCompatibilityEnabled;

        TOperationElement* FindOperationElement(TOperationId operationId) const
        {
            auto it = OperationIdToElement.find(operationId);
            return it != OperationIdToElement.end() ? it->second : nullptr;
        }

        TOperationElement* FindDisabledOperationElement(TOperationId operationId) const
        {
            auto it = DisabledOperationIdToElement.find(operationId);
            return it != DisabledOperationIdToElement.end() ? it->second : nullptr;
        }

        TPool* FindPool(const TString& poolName) const
        {
            auto it = PoolNameToElement.find(poolName);
            return it != PoolNameToElement.end() ? it->second : nullptr;
        }
    };

    typedef TIntrusivePtr<TRootElementSnapshot> TRootElementSnapshotPtr;

    class TFairShareTreeSnapshot
        : public IFairShareTreeSnapshot
    {
    public:
        TFairShareTreeSnapshot(
            TFairShareTreePtr tree,
            TRootElementSnapshotPtr rootElementSnapshot,
            TSchedulingTagFilter nodesFilter,
            const TJobResources& totalResourceLimits,
            const NLogging::TLogger& logger)
            : Tree_(std::move(tree))
            , RootElementSnapshot_(std::move(rootElementSnapshot))
            , NodesFilter_(std::move(nodesFilter))
            , TotalResourceLimits_(totalResourceLimits)
            , Logger(logger)
        { }

        virtual TFuture<void> ScheduleJobs(const ISchedulingContextPtr& schedulingContext) override
        {
            return BIND(&TFairShareTree::DoScheduleJobs,
                Tree_,
                schedulingContext,
                RootElementSnapshot_)
                .AsyncVia(GetCurrentInvoker())
                .Run();
        }

        virtual void PreemptJobsGracefully(const ISchedulingContextPtr& schedulingContext) override
        {
            Tree_->DoPreemptJobsGracefully(schedulingContext, RootElementSnapshot_);
        }

        virtual void ProcessUpdatedJob(TOperationId operationId, TJobId jobId, const TJobResources& jobResources) override
        {
            // NB: Should be filtered out on large clusters.
            YT_LOG_DEBUG("Processing updated job (OperationId: %v, JobId: %v, Resources: %v)", operationId, jobId, jobResources);
            auto* operationElement = RootElementSnapshot_->FindOperationElement(operationId);
            if (operationElement) {
                operationElement->SetJobResourceUsage(jobId, jobResources);
            }
        }

        virtual void ProcessFinishedJob(TOperationId operationId, TJobId jobId) override
        {
            // NB: Should be filtered out on large clusters.
            YT_LOG_DEBUG("Processing finished job (OperationId: %v, JobId: %v)", operationId, jobId);
            auto* operationElement = RootElementSnapshot_->FindOperationElement(operationId);
            if (operationElement) {
                operationElement->OnJobFinished(jobId);
            }
        }

        virtual bool HasOperation(TOperationId operationId) const override
        {
            auto* operationElement = RootElementSnapshot_->FindOperationElement(operationId);
            return operationElement != nullptr;
        }

        virtual bool IsOperationRunningInTree(TOperationId operationId) const override
        {
            if (auto* element = RootElementSnapshot_->FindOperationElement(operationId)) {
                auto res = element->IsOperationRunningInPool();
                return res;
            }

            if (auto* element = RootElementSnapshot_->FindDisabledOperationElement(operationId)) {
                auto res = element->IsOperationRunningInPool();
                return res;
            }

            return false;
        }

        virtual bool IsOperationDisabled(TOperationId operationId) const override
        {
            return RootElementSnapshot_->DisabledOperationIdToElement.contains(operationId);
        }

        virtual void ApplyJobMetricsDelta(TOperationId operationId, const TJobMetrics& jobMetricsDelta) override
        {
            auto* operationElement = RootElementSnapshot_->FindOperationElement(operationId);
            if (operationElement) {
                operationElement->ApplyJobMetricsDelta(jobMetricsDelta);
            }
        }

        virtual const TSchedulingTagFilter& GetNodesFilter() const override
        {
            return NodesFilter_;
        }

        virtual TJobResources GetTotalResourceLimits() const override
        {
            return TotalResourceLimits_;
        }

        virtual std::optional<TSchedulerElementStateSnapshot> GetMaybeStateSnapshotForPool(const TString& poolId) const override
        {
            if (auto* element = RootElementSnapshot_->FindPool(poolId)) {
                return TSchedulerElementStateSnapshot{
                    element->Attributes().DemandShare,
                    element->Attributes().PromisedFairShare};
            }

            return std::nullopt;
        }

        virtual void BuildResourceMetering(TMeteringMap* statistics) const override
        {
            auto rootElement = RootElementSnapshot_->RootElement;
            rootElement->BuildResourceMetering(std::nullopt, statistics);
        }

        virtual void ProfileFairShare() const override
        {
            Tree_->DoProfileFairShare(RootElementSnapshot_);
        }

        virtual void LogFairShare(NEventLog::TFluentLogEvent fluent) const override
        {
            Tree_->DoLogFairShare(RootElementSnapshot_, std::move(fluent));
        }

        virtual void EssentialLogFairShare(NEventLog::TFluentLogEvent fluent) const override
        {
            Tree_->DoEssentialLogFairShare(RootElementSnapshot_, std::move(fluent));
        }

    private:
        const TIntrusivePtr<TFairShareTree> Tree_;
        const TRootElementSnapshotPtr RootElementSnapshot_;
        const TSchedulingTagFilter NodesFilter_;
        const TJobResources TotalResourceLimits_;
        const NLogging::TLogger Logger;
    };

    TRootElementSnapshotPtr RootElementSnapshot_;
    TRootElementSnapshotPtr RootElementSnapshotPrecommit_;

    TFairShareSchedulingStage NonPreemptiveSchedulingStage_;
    TFairShareSchedulingStage AggressivelyPreemptiveSchedulingStage_;
    TFairShareSchedulingStage PreemptiveSchedulingStage_;
    TFairShareSchedulingStage PackingFallbackSchedulingStage_;

    TEventTimer FairSharePreUpdateTimer_;
    TEventTimer FairShareUpdateTimer_;
    TEventTimer FairShareFluentLogTimer_;
    TEventTimer FairShareTextLogTimer_;
    TGauge PoolCountGauge_;

    std::atomic<TCpuInstant> LastSchedulingInformationLoggedTime_ = 0;

    std::pair<IFairShareTreeSnapshotPtr, TError> DoFairShareUpdateAt(TInstant now)
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        ResourceTree_->PerformPostponedActions();

        TUpdateFairShareContext updateContext;

        updateContext.Now = now;
        updateContext.PreviousUpdateTime = LastFairShareUpdateTime_;

        auto rootElement = RootElement_->Clone();
        {
            TEventTimer timer(FairSharePreUpdateTimer_);
            rootElement->PreUpdate(&updateContext);
        }

        TRootElementSnapshotPtr rootElementSnapshot;
        auto asyncUpdate = BIND([&]
            {
                {
                    TEventTimer timer(FairShareUpdateTimer_);
                    rootElement->Update(&updateContext);
                }

                rootElementSnapshot = New<TRootElementSnapshot>();
                rootElement->BuildElementMapping(
                    &rootElementSnapshot->OperationIdToElement,
                    &rootElementSnapshot->DisabledOperationIdToElement,
                    &rootElementSnapshot->PoolNameToElement);
                std::swap(rootElementSnapshot->ElementIndexes, updateContext.ElementIndexes);
            })
            .AsyncVia(StrategyHost_->GetFairShareUpdateInvoker())
            .Run();
        WaitFor(asyncUpdate)
            .ThrowOnError();

        YT_VERIFY(rootElementSnapshot);

        YT_LOG_DEBUG("Fair share tree update finished (UnschedulableReasons: %v)",
            updateContext.UnschedulableReasons);

        TError error;
        if (!updateContext.Errors.empty()) {
            error = TError("Found pool configuration issues during fair share update in tree %Qv", TreeId_)
                << TErrorAttribute("pool_tree", TreeId_)
                << std::move(updateContext.Errors);
        }

        // Update starvation flags for operations and pools.
        for (const auto& [operationId, element] : rootElementSnapshot->OperationIdToElement) {
            element->CheckForStarvation(now);
        }
        if (Config_->EnablePoolStarvation) {
            for (const auto& [poolName, element] : rootElementSnapshot->PoolNameToElement) {
                element->CheckForStarvation(now);
            }
        }

        // Copy persistent attributes back to the original tree.
        for (const auto& [operationId, element] : rootElementSnapshot->OperationIdToElement) {
            if (auto originalElement = FindOperationElement(operationId)) {
                originalElement->PersistentAttributes() = element->PersistentAttributes();
            }
        }
        for (const auto& [poolName, element] : rootElementSnapshot->PoolNameToElement) {
            if (auto originalElement = FindPool(poolName)) {
                originalElement->PersistentAttributes() = element->PersistentAttributes();
            }
        }
        RootElement_->PersistentAttributes() = rootElement->PersistentAttributes();

        rootElement->MarkImmutable();

        rootElementSnapshot->RootElement = rootElement;
        rootElementSnapshot->Config = Config_;
        rootElementSnapshot->ControllerConfig = ControllerConfig_;
        rootElementSnapshot->CoreProfilingCompatibilityEnabled = StrategyHost_->IsCoreProfilingCompatibilityEnabled();

        RootElementSnapshotPrecommit_ = rootElementSnapshot;
        LastFairShareUpdateTime_ = now;

        auto treeSnapshot = New<TFairShareTreeSnapshot>(
            this,
            std::move(rootElementSnapshot),
            GetNodesFilter(),
            StrategyHost_->GetResourceLimits(GetNodesFilter()),
            Logger);
        return std::make_pair(treeSnapshot, error);
    }

    void DoScheduleJobs(
        const ISchedulingContextPtr& schedulingContext,
        const TRootElementSnapshotPtr& rootElementSnapshot)
    {
        bool enableSchedulingInfoLogging = false;
        auto now = schedulingContext->GetNow();
        const auto& config = rootElementSnapshot->Config;
        if (LastSchedulingInformationLoggedTime_ + DurationToCpuDuration(config->HeartbeatTreeSchedulingInfoLogBackoff) < now) {
            enableSchedulingInfoLogging = true;
            LastSchedulingInformationLoggedTime_ = now;
        }

        std::vector<TSchedulingTagFilter> registeredSchedulingTagFilters;
        {
            auto guard = ReaderGuard(RegisteredSchedulingTagFiltersLock_);
            registeredSchedulingTagFilters = RegisteredSchedulingTagFilters_;
        }

        TFairShareContext context(
            schedulingContext,
            rootElementSnapshot->RootElement->GetTreeSize(),
            std::move(registeredSchedulingTagFilters),
            enableSchedulingInfoLogging,
            Logger);

        context.SchedulingStatistics().ResourceUsage = schedulingContext->ResourceUsage();
        context.SchedulingStatistics().ResourceLimits = schedulingContext->ResourceLimits();

        bool needPackingFallback;
        {
            context.StartStage(&NonPreemptiveSchedulingStage_);
            DoScheduleJobsWithoutPreemption(rootElementSnapshot, &context, now);
            context.SchedulingStatistics().NonPreemptiveScheduleJobAttempts = context.StageState()->ScheduleJobAttemptCount;
            needPackingFallback = schedulingContext->StartedJobs().empty() && !context.BadPackingOperations().empty();
            ReactivateBadPackingOperations(&context);
            context.FinishStage();
        }

        auto nodeId = schedulingContext->GetNodeDescriptor().Id;

        bool scheduleJobsWithPreemption = false;
        {
            bool nodeIsMissing = false;
            {
                auto guard = ReaderGuard(NodeIdToLastPreemptiveSchedulingTimeLock_);
                auto it = NodeIdToLastPreemptiveSchedulingTime_.find(nodeId);
                if (it == NodeIdToLastPreemptiveSchedulingTime_.end()) {
                    nodeIsMissing = true;
                    scheduleJobsWithPreemption = true;
                } else if (it->second + DurationToCpuDuration(config->PreemptiveSchedulingBackoff) <= now) {
                    scheduleJobsWithPreemption = true;
                    it->second = now;
                }
            }
            if (nodeIsMissing) {
                auto guard = WriterGuard(NodeIdToLastPreemptiveSchedulingTimeLock_);
                NodeIdToLastPreemptiveSchedulingTime_[nodeId] = now;
            }
        }

        context.SchedulingStatistics().ScheduleWithPreemption = scheduleJobsWithPreemption;
        if (scheduleJobsWithPreemption) {
            // First try to schedule a job with aggressive preemption for aggressively starving operations only.
            {
                context.StartStage(&AggressivelyPreemptiveSchedulingStage_);
                DoScheduleJobsWithAggressivePreemption(rootElementSnapshot, &context, now);
                context.SchedulingStatistics().AggressivelyPreemptiveScheduleJobAttempts = context.StageState()->ScheduleJobAttemptCount;
                context.FinishStage();
            }

            // If no jobs were scheduled in the previous stage, try to schedule a job with regular preemption.
            if (context.SchedulingStatistics().ScheduledDuringPreemption == 0) {
                context.StartStage(&PreemptiveSchedulingStage_);
                DoScheduleJobsWithPreemption(rootElementSnapshot, &context, now);
                context.SchedulingStatistics().PreemptiveScheduleJobAttempts = context.StageState()->ScheduleJobAttemptCount;
                context.FinishStage();
            }
        } else {
            YT_LOG_DEBUG("Skip preemptive scheduling");
        }

        if (needPackingFallback) {
            context.StartStage(&PackingFallbackSchedulingStage_);
            DoScheduleJobsPackingFallback(rootElementSnapshot, &context, now);
            context.SchedulingStatistics().PackingFallbackScheduleJobAttempts = context.StageState()->ScheduleJobAttemptCount;
            context.FinishStage();
        }

        // Interrupt some jobs if usage is greater that limit.
        if (schedulingContext->ShouldAbortJobsSinceResourcesOvercommit()) {
            YT_LOG_DEBUG("Interrupting jobs on node since resources are overcommitted (NodeId: %v, Address: %v)",
                schedulingContext->GetNodeDescriptor().Id,
                schedulingContext->GetNodeDescriptor().Address);

            std::vector<TJobWithPreemptionInfo> jobInfos;
            for (const auto& job : schedulingContext->RunningJobs()) {
                auto* operationElement = rootElementSnapshot->FindOperationElement(job->GetOperationId());
                if (!operationElement || !operationElement->IsJobKnown(job->GetId())) {
                    YT_LOG_DEBUG("Dangling running job found (JobId: %v, OperationId: %v)",
                        job->GetId(),
                        job->GetOperationId());
                    continue;
                }
                jobInfos.push_back(TJobWithPreemptionInfo{
                    .Job = job,
                    .IsPreemptable = operationElement->IsJobPreemptable(job->GetId(), /* aggressivePreemptionEnabled */ false),
                    .OperationElement = operationElement,
                });
            }

            auto hasCpuGap = [] (const TJobWithPreemptionInfo& jobWithPreemptionInfo)
            {
                return jobWithPreemptionInfo.Job->ResourceUsage().GetCpu() < jobWithPreemptionInfo.Job->ResourceLimits().GetCpu();
            };

            std::sort(
                jobInfos.begin(),
                jobInfos.end(),
                [&] (const TJobWithPreemptionInfo& lhs, const TJobWithPreemptionInfo& rhs) {
                    if (lhs.IsPreemptable != rhs.IsPreemptable) {
                        return lhs.IsPreemptable < rhs.IsPreemptable;
                    }

                    if (!lhs.IsPreemptable) {
                        // Save jobs without cpu gap.
                        bool lhsHasCpuGap = hasCpuGap(lhs);
                        bool rhsHasCpuGap = hasCpuGap(rhs);
                        if (lhsHasCpuGap != rhsHasCpuGap) {
                            return lhsHasCpuGap > rhsHasCpuGap;
                        }
                    }

                    return lhs.Job->GetStartTime() < rhs.Job->GetStartTime();
                }
            );

            auto currentResources = TJobResources();
            for (const auto& jobInfo : jobInfos) {
                if (!Dominates(schedulingContext->ResourceLimits(), currentResources + jobInfo.Job->ResourceUsage())) {
                    YT_LOG_DEBUG("Interrupt job since node resources are overcommitted (JobId: %v, OperationId: %v)",
                        jobInfo.Job->GetId(),
                        jobInfo.OperationElement->GetId());
                    PreemptJob(jobInfo.Job, jobInfo.OperationElement, rootElementSnapshot, schedulingContext);
                } else {
                    currentResources += jobInfo.Job->ResourceUsage();
                }
            }
        }

        schedulingContext->SetSchedulingStatistics(context.SchedulingStatistics());
    }

    void DoScheduleJobsWithoutPreemption(
        const TRootElementSnapshotPtr& rootElementSnapshot,
        TFairShareContext* context,
        TCpuInstant startTime)
    {
        YT_LOG_TRACE("Scheduling new jobs");

        DoScheduleJobsWithoutPreemptionImpl(
            rootElementSnapshot,
            context,
            startTime,
            /* ignorePacking */ false,
            /* oneJobOnly */ false);
    }

    void DoScheduleJobsPackingFallback(
        const TRootElementSnapshotPtr& rootElementSnapshot,
        TFairShareContext* context,
        TCpuInstant startTime)
    {
        YT_LOG_TRACE("Scheduling jobs with packing ignored");

        // Schedule at most one job with packing ignored in case all operations have rejected the heartbeat.
        DoScheduleJobsWithoutPreemptionImpl(
            rootElementSnapshot,
            context,
            startTime,
            /* ignorePacking */ true,
            /* oneJobOnly */ true);
    }

    void DoScheduleJobsWithoutPreemptionImpl(
        const TRootElementSnapshotPtr& rootElementSnapshot,
        TFairShareContext* context,
        TCpuInstant startTime,
        bool ignorePacking,
        bool oneJobOnly)
    {
        auto& rootElement = rootElementSnapshot->RootElement;
        const auto& controllerConfig = rootElementSnapshot->ControllerConfig;

        {
            bool prescheduleExecuted = false;
            TCpuInstant schedulingDeadline = startTime + DurationToCpuDuration(controllerConfig->ScheduleJobsTimeout);

            TWallTimer scheduleTimer;
            while (context->SchedulingContext()->CanStartMoreJobs() && context->SchedulingContext()->GetNow() < schedulingDeadline)
            {
                if (!prescheduleExecuted) {
                    TWallTimer prescheduleTimer;
                    context->PrepareForScheduling();
                    rootElement->PrescheduleJob(context, EPrescheduleJobOperationCriterion::All, /* aggressiveStarvationEnabled */ false);
                    context->StageState()->PrescheduleDuration = prescheduleTimer.GetElapsedTime();
                    prescheduleExecuted = true;
                }
                ++context->StageState()->ScheduleJobAttemptCount;
                auto scheduleJobResult = rootElement->ScheduleJob(context, ignorePacking);
                if (scheduleJobResult.Scheduled) {
                    ReactivateBadPackingOperations(context);
                }
                if (scheduleJobResult.Finished || (oneJobOnly && scheduleJobResult.Scheduled)) {
                    break;
                }
            }

            context->StageState()->TotalDuration = scheduleTimer.GetElapsedTime();
            context->ProfileStageTimingsAndLogStatistics();
        }
    }

    void DoScheduleJobsWithAggressivePreemption(
        const TRootElementSnapshotPtr& rootElementSnapshot,
        TFairShareContext* context,
        TCpuInstant startTime)
    {
        DoScheduleJobsWithPreemptionImpl(
            rootElementSnapshot,
            context,
            startTime,
            /* isAggressive */ true);
    }

    void DoScheduleJobsWithPreemption(
        const TRootElementSnapshotPtr& rootElementSnapshot,
        TFairShareContext* context,
        TCpuInstant startTime)
    {
        DoScheduleJobsWithPreemptionImpl(
            rootElementSnapshot,
            context,
            startTime,
            /* isAggressive */ false);
    }

    void DoScheduleJobsWithPreemptionImpl(
        const TRootElementSnapshotPtr& rootElementSnapshot,
        TFairShareContext* context,
        TCpuInstant startTime,
        bool isAggressive)
    {
        auto& rootElement = rootElementSnapshot->RootElement;
        const auto& config = rootElementSnapshot->Config;
        const auto& controllerConfig = rootElementSnapshot->ControllerConfig;

        // TODO(ignat): move this logic inside TFairShareContext.
        if (!context->GetHasAggressivelyStarvingElements()) {
            context->SetHasAggressivelyStarvingElements(rootElement->HasAggressivelyStarvingElements(context, false));
        }

        bool hasAggressivelyStarvingElements = *context->GetHasAggressivelyStarvingElements();

        context->SchedulingStatistics().HasAggressivelyStarvingElements = hasAggressivelyStarvingElements;
        if (isAggressive && !hasAggressivelyStarvingElements) {
            return;
        }

        // Compute discount to node usage.
        YT_LOG_TRACE("Looking for %v jobs",
            isAggressive ? "aggressively preemptable" : "preemptable");
        THashSet<const TCompositeSchedulerElement *> discountedPools;
        std::vector<TJobPtr> preemptableJobs;
        {
            NProfiling::TWallTimer timer;

            // We need to initialize dynamic attributes list to update
            // resource usage discounts.
            if (!context->SchedulingContext()->RunningJobs().empty()) {
                context->PrepareForScheduling();
            }

            for (const auto& job : context->SchedulingContext()->RunningJobs()) {
                auto* operationElement = rootElementSnapshot->FindOperationElement(job->GetOperationId());
                if (!operationElement || !operationElement->IsJobKnown(job->GetId())) {
                    YT_LOG_DEBUG("Dangling running job found (JobId: %v, OperationId: %v)",
                        job->GetId(),
                        job->GetOperationId());
                    continue;
                }

                bool isAggressivePreemptionEnabled = isAggressive &&
                    operationElement->IsAggressiveStarvationPreemptionAllowed();
                bool isJobPreemptable = operationElement->IsPreemptionAllowed(isAggressive, config) &&
                    operationElement->IsJobPreemptable(job->GetId(), isAggressivePreemptionEnabled);

                bool forceJobPreemptable = config->SchedulingSegments->Mode != ESegmentedSchedulingMode::Disabled &&
                    context->SchedulingContext()->GetSchedulingSegment() != operationElement->SchedulingSegment();
                YT_LOG_TRACE_IF(forceJobPreemptable,
                    "Job is preemptable because it is running on a node in a different scheduling segment "
                    "(JobId: %v, OperationId: %v, OperationSegment: %v, NodeSegment: %v, Address: %v)",
                    job->GetId(),
                    operationElement->GetId(),
                    operationElement->SchedulingSegment(),
                    context->SchedulingContext()->GetSchedulingSegment(),
                    context->SchedulingContext()->GetNodeDescriptor().Address);

                if (isJobPreemptable || forceJobPreemptable) {
                    const auto* parent = operationElement->GetParent();
                    while (parent) {
                        discountedPools.insert(parent);
                        context->DynamicAttributesFor(parent).ResourceUsageDiscount += job->ResourceUsage();
                        parent = parent->GetParent();
                    }
                    context->SchedulingContext()->ResourceUsageDiscount() += job->ResourceUsage();
                    preemptableJobs.push_back(job);
                }
            }

            context->StageState()->AnalyzeJobsDuration += timer.GetElapsedTime();
        }

        context->SchedulingStatistics().PreemptableJobCount = preemptableJobs.size();
        context->SchedulingStatistics().ResourceUsageDiscount = context->SchedulingContext()->ResourceUsageDiscount();

        int startedBeforePreemption = context->SchedulingContext()->StartedJobs().size();

        // NB: Schedule at most one job with preemption.
        TJobPtr jobStartedUsingPreemption;
        {
            YT_LOG_TRACE(
                "Scheduling new jobs with preemption (PreemptableJobs: %v, ResourceUsageDiscount: %v, IsAggressive: %v)",
                preemptableJobs.size(),
                FormatResources(context->SchedulingContext()->ResourceUsageDiscount()),
                isAggressive);

            bool prescheduleExecuted = false;
            TCpuInstant schedulingDeadline = startTime + DurationToCpuDuration(controllerConfig->ScheduleJobsTimeout);

            TWallTimer timer;
            while (context->SchedulingContext()->CanStartMoreJobs() && context->SchedulingContext()->GetNow() < schedulingDeadline)
            {
                if (!prescheduleExecuted) {
                    TWallTimer prescheduleTimer;
                    context->PrepareForScheduling();
                    rootElement->PrescheduleJob(
                        context,
                        isAggressive
                            ? EPrescheduleJobOperationCriterion::AggressivelyStarvingOnly
                            : EPrescheduleJobOperationCriterion::StarvingOnly,
                        /* aggressiveStarvationEnabled */ false);
                    context->StageState()->PrescheduleDuration = prescheduleTimer.GetElapsedTime();
                    prescheduleExecuted = true;
                }

                ++context->StageState()->ScheduleJobAttemptCount;
                auto scheduleJobResult = rootElement->ScheduleJob(context, /* ignorePacking */ true);
                if (scheduleJobResult.Scheduled) {
                    jobStartedUsingPreemption = context->SchedulingContext()->StartedJobs().back();
                    break;
                }
                if (scheduleJobResult.Finished) {
                    break;
                }
            }

            context->StageState()->TotalDuration = timer.GetElapsedTime();
            context->ProfileStageTimingsAndLogStatistics();
        }

        int startedAfterPreemption = context->SchedulingContext()->StartedJobs().size();

        context->SchedulingStatistics().ScheduledDuringPreemption = startedAfterPreemption - startedBeforePreemption;

        // Reset discounts.
        context->SchedulingContext()->ResourceUsageDiscount() = {};
        for (const auto& pool : discountedPools) {
            context->DynamicAttributesFor(pool).ResourceUsageDiscount = {};
        }

        // Preempt jobs if needed.
        std::sort(
            preemptableJobs.begin(),
            preemptableJobs.end(),
            [] (const TJobPtr& lhs, const TJobPtr& rhs) {
                return lhs->GetStartTime() > rhs->GetStartTime();
            });

        auto findPoolWithViolatedLimitsForJob = [&] (const TJobPtr& job) -> const TCompositeSchedulerElement* {
            auto* operationElement = rootElementSnapshot->FindOperationElement(job->GetOperationId());
            if (!operationElement) {
                return nullptr;
            }

            auto* parent = operationElement->GetParent();
            while (parent) {
                if (!Dominates(parent->GetSpecifiedResourceLimits(), parent->GetInstantResourceUsage())) {
                    return parent;
                }
                parent = parent->GetParent();
            }
            return nullptr;
        };

        auto findOperationElementForJob = [&] (const TJobPtr& job) -> TOperationElement* {
            auto operationElement = rootElementSnapshot->FindOperationElement(job->GetOperationId());
            if (!operationElement || !operationElement->IsJobKnown(job->GetId())) {
                YT_LOG_DEBUG("Dangling preemptable job found (JobId: %v, OperationId: %v)",
                    job->GetId(),
                    job->GetOperationId());

                return nullptr;
            }

            return operationElement;
        };

        int currentJobIndex = 0;
        for (; currentJobIndex < preemptableJobs.size(); ++currentJobIndex) {
            if (Dominates(context->SchedulingContext()->ResourceLimits(), context->SchedulingContext()->ResourceUsage())) {
                break;
            }

            const auto& job = preemptableJobs[currentJobIndex];
            auto operationElement = findOperationElementForJob(job);
            if (!operationElement) {
                continue;
            }

            if (jobStartedUsingPreemption) {
                job->SetPreemptionReason(Format("Preempted to start job %v of operation %v",
                    jobStartedUsingPreemption->GetId(),
                    jobStartedUsingPreemption->GetOperationId()));

                job->SetPreemptedFor(TPreemptedFor{
                    .JobId = jobStartedUsingPreemption->GetId(),
                    .OperationId = jobStartedUsingPreemption->GetOperationId(),
                });
            } else {
                job->SetPreemptionReason(Format("Node resource limits violated"));
            }
            PreemptJob(job, operationElement, rootElementSnapshot, context->SchedulingContext());
        }

        for (; currentJobIndex < preemptableJobs.size(); ++currentJobIndex) {
            const auto& job = preemptableJobs[currentJobIndex];

            auto operationElement = findOperationElementForJob(job);
            if (!operationElement) {
                continue;
            }

            if (!Dominates(operationElement->ResourceLimits(), operationElement->GetInstantResourceUsage())) {
                job->SetPreemptionReason(Format("Preempted due to violation of resource limits of operation %v",
                    operationElement->GetId()));
                PreemptJob(job, operationElement, rootElementSnapshot, context->SchedulingContext());
                continue;
            }

            auto violatedPool = findPoolWithViolatedLimitsForJob(job);
            if (violatedPool) {
                job->SetPreemptionReason(Format("Preempted due to violation of limits on pool %v",
                    violatedPool->GetId()));
                PreemptJob(job, operationElement, rootElementSnapshot, context->SchedulingContext());
            }
        }

        if (!Dominates(context->SchedulingContext()->ResourceLimits(), context->SchedulingContext()->ResourceUsage())) {
            YT_LOG_INFO("Resource usage exceeds node resource limits even after preemption (ResourceLimits: %v, ResourceUsage: %v, NodeId: %v, Address: %v)",
                FormatResources(context->SchedulingContext()->ResourceLimits()),
                FormatResources(context->SchedulingContext()->ResourceUsage()),
                context->SchedulingContext()->GetNodeDescriptor().Id,
                context->SchedulingContext()->GetNodeDescriptor().Address);
        }
    }

    void DoPreemptJobsGracefully(
        const ISchedulingContextPtr& schedulingContext,
        const TRootElementSnapshotPtr& rootElementSnapshot)
    {
        const auto& config = rootElementSnapshot->Config;

        YT_LOG_TRACE("Looking for gracefully preemptable jobs");
        for (const auto& job : schedulingContext->RunningJobs()) {
            if (job->GetPreemptionMode() != EPreemptionMode::Graceful || job->GetPreempted()) {
                continue;
            }

            auto* operationElement = rootElementSnapshot->FindOperationElement(job->GetOperationId());

            if (!operationElement || !operationElement->IsJobKnown(job->GetId())) {
                YT_LOG_DEBUG("Dangling running job found (JobId: %v, OperationId: %v)",
                    job->GetId(),
                    job->GetOperationId());
                continue;
            }

            if (operationElement->IsJobPreemptable(job->GetId(), /* aggressivePreemptionEnabled */ false)) {
                schedulingContext->PreemptJob(job, config->JobGracefulInterruptTimeout);
            }
        }
    }

    void PreemptJob(
        const TJobPtr& job,
        const TOperationElementPtr& operationElement,
        const TRootElementSnapshotPtr& rootElementSnapshot,
        const ISchedulingContextPtr& schedulingContext) const
    {
        const auto& config = rootElementSnapshot->Config;

        schedulingContext->ResourceUsage() -= job->ResourceUsage();
        operationElement->SetJobResourceUsage(job->GetId(), TJobResources());
        job->ResourceUsage() = {};

        schedulingContext->PreemptJob(job, config->JobInterruptTimeout);
    }

    void DoRegisterPool(const TPoolPtr& pool)
    {
        int index = RegisterSchedulingTagFilter(pool->GetSchedulingTagFilter());
        pool->SetSchedulingTagFilterIndex(index);
        YT_VERIFY(Pools_.emplace(pool->GetId(), pool).second);
        YT_VERIFY(PoolToMinUnusedSlotIndex_.emplace(pool->GetId(), 0).second);

        DoRegisterPoolProfilingCounters(pool->GetId());
        pool->RegisterProfiler(TreeProfiler_
            .WithGlobal()
            .WithRequiredTag("pool", pool->GetId(), -1));
    }

    void RegisterPool(const TPoolPtr& pool, const TCompositeSchedulerElementPtr& parent)
    {
        DoRegisterPool(pool);

        pool->AttachParent(parent.Get());

        YT_LOG_INFO("Pool registered (Pool: %v, Parent: %v)",
            pool->GetId(),
            parent->GetId());
    }

    void ReconfigurePool(const TPoolPtr& pool, const TPoolConfigPtr& config)
    {
        auto oldSchedulingTagFilter = pool->GetSchedulingTagFilter();
        pool->SetConfig(config);
        auto newSchedulingTagFilter = pool->GetSchedulingTagFilter();
        if (oldSchedulingTagFilter != newSchedulingTagFilter) {
            UnregisterSchedulingTagFilter(oldSchedulingTagFilter);
            int index = RegisterSchedulingTagFilter(newSchedulingTagFilter);
            pool->SetSchedulingTagFilterIndex(index);
        }
    }

    void UnregisterPool(const TPoolPtr& pool)
    {
        auto userName = pool->GetUserName();
        if (userName && pool->IsEphemeralInDefaultParentPool()) {
            YT_VERIFY(UserToEphemeralPoolsInDefaultPool_[*userName].erase(pool->GetId()) == 1);
        }

        UnregisterSchedulingTagFilter(pool->GetSchedulingTagFilterIndex());

        YT_VERIFY(PoolToMinUnusedSlotIndex_.erase(pool->GetId()) == 1);

        YT_VERIFY(PoolToSpareSlotIndices_.erase(pool->GetId()) <= 1);

        YT_VERIFY(PoolToUnregisterOperationCounters_.erase(pool->GetId()) == 1);

        // We cannot use pool after erase because Pools may contain last alive reference to it.
        auto extractedPool = std::move(Pools_[pool->GetId()]);
        YT_VERIFY(Pools_.erase(pool->GetId()) == 1);

        extractedPool->SetNonAlive();
        auto parent = extractedPool->GetParent();
        extractedPool->DetachParent();

        YT_LOG_INFO("Pool unregistered (Pool: %v, Parent: %v)",
            extractedPool->GetId(),
            parent->GetId());
    }

    TPoolPtr GetOrCreatePool(const TPoolName& poolName, TString userName)
    {
        auto pool = FindPool(poolName.GetPool());
        if (pool) {
            return pool;
        }

        // Create ephemeral pool.
        auto poolConfig = New<TPoolConfig>();
        if (poolName.GetParentPool()) {
            auto parentPoolConfig = GetPool(*poolName.GetParentPool())->GetConfig();
            poolConfig->Mode = parentPoolConfig->EphemeralSubpoolConfig->Mode;
            poolConfig->MaxOperationCount = parentPoolConfig->EphemeralSubpoolConfig->MaxOperationCount;
            poolConfig->MaxRunningOperationCount = parentPoolConfig->EphemeralSubpoolConfig->MaxRunningOperationCount;
            poolConfig->ResourceLimits = parentPoolConfig->EphemeralSubpoolConfig->ResourceLimits;
        }
        pool = New<TPool>(
            StrategyHost_,
            this,
            poolName.GetPool(),
            poolConfig,
            /* defaultConfigured */ true,
            Config_,
            GetPoolProfilingTag(poolName.GetPool()),
            TreeId_,
            Logger);

        pool->SetUserName(userName);

        TCompositeSchedulerElement* parent;
        if (poolName.GetParentPool()) {
            parent = GetPool(*poolName.GetParentPool()).Get();
        } else {
            parent = GetDefaultParentPool().Get();
            pool->SetEphemeralInDefaultParentPool();
            UserToEphemeralPoolsInDefaultPool_[userName].insert(poolName.GetPool());
        }

        RegisterPool(pool, parent);
        return pool;
    }

    void DoRegisterPoolProfilingCounters(const TString& poolName)
    {
        auto poolProfiler = TreeProfiler_
            .WithTag("pool", poolName, -1)
            .WithGlobal();

        TUnregisterOperationCounters counters;
        counters.BannedCounter = poolProfiler.Counter("/pools/banned_operation_count");

        for (auto state : TEnumTraits<EOperationState>::GetDomainValues()) {
            counters.FinishedCounters[state] = poolProfiler
                .WithTag("state", FormatEnum(state), -1)
                .Counter("/pools/finished_operation_count");
        }
        PoolToUnregisterOperationCounters_.emplace(poolName, std::move(counters));
    }

    bool TryAllocatePoolSlotIndex(const TString& poolName, int slotIndex)
    {
        auto& minUnusedIndex = GetOrCrash(PoolToMinUnusedSlotIndex_, poolName);
        auto& spareSlotIndices = PoolToSpareSlotIndices_[poolName];

        if (slotIndex >= minUnusedIndex) {
            // Mark all indices as spare except #slotIndex.
            for (int index = minUnusedIndex; index < slotIndex; ++index) {
                YT_VERIFY(spareSlotIndices.insert(index).second);
            }

            minUnusedIndex = slotIndex + 1;

            return true;
        } else {
            return spareSlotIndices.erase(slotIndex) == 1;
        }
    }

    std::optional<int> AllocateOperationSlotIndex(const TFairShareStrategyOperationStatePtr& state, const TString& poolName)
    {
        auto slotIndex = state->GetHost()->FindSlotIndex(TreeId_);

        if (slotIndex) {
            // Revive case
            if (TryAllocatePoolSlotIndex(poolName, *slotIndex)) {
                YT_LOG_DEBUG("Operation slot index reused (OperationId: %v, Pool: %v, SlotIndex: %v)",
                    state->GetHost()->GetId(),
                    poolName,
                    *slotIndex);
                return slotIndex;
            }
            YT_LOG_ERROR("Failed to reuse slot index during revive (OperationId: %v, Pool: %v, SlotIndex: %v)",
                state->GetHost()->GetId(),
                poolName,
                *slotIndex);
        }

        auto it = PoolToSpareSlotIndices_.find(poolName);
        if (it == PoolToSpareSlotIndices_.end() || it->second.empty()) {
            auto& minUnusedIndex = GetOrCrash(PoolToMinUnusedSlotIndex_, poolName);
            slotIndex = minUnusedIndex;
            ++minUnusedIndex;
        } else {
            auto spareIndexIt = it->second.begin();
            slotIndex = *spareIndexIt;
            it->second.erase(spareIndexIt);
        }

        state->GetHost()->SetSlotIndex(TreeId_, *slotIndex);
        YT_LOG_DEBUG("Operation slot index allocated (OperationId: %v, Pool: %v, SlotIndex: %v)",
            state->GetHost()->GetId(),
            poolName,
            *slotIndex);
        return slotIndex;
    }

    void ReleaseOperationSlotIndex(const TFairShareStrategyOperationStatePtr& state, const TString& poolName)
    {
        auto slotIndex = state->GetHost()->FindSlotIndex(TreeId_);
        YT_VERIFY(slotIndex);

        auto it = PoolToSpareSlotIndices_.find(poolName);
        if (it == PoolToSpareSlotIndices_.end()) {
            YT_VERIFY(PoolToSpareSlotIndices_.emplace(poolName, THashSet<int>{*slotIndex}).second);
        } else {
            it->second.insert(*slotIndex);
        }

        YT_LOG_DEBUG("Operation slot index released (OperationId: %v, Pool: %v, SlotIndex: %v)",
            state->GetHost()->GetId(),
            poolName,
            *slotIndex);
    }

    int RegisterSchedulingTagFilter(const TSchedulingTagFilter& filter)
    {
        if (filter.IsEmpty()) {
            return EmptySchedulingTagFilterIndex;
        }
        auto it = SchedulingTagFilterToIndexAndCount_.find(filter);
        if (it == SchedulingTagFilterToIndexAndCount_.end()) {
            int index;
            if (FreeSchedulingTagFilterIndexes_.empty()) {
                auto guard = WriterGuard(RegisteredSchedulingTagFiltersLock_);

                index = RegisteredSchedulingTagFilters_.size();
                RegisteredSchedulingTagFilters_.push_back(filter);
            } else {
                index = FreeSchedulingTagFilterIndexes_.back();
                FreeSchedulingTagFilterIndexes_.pop_back();

                {
                    auto guard = WriterGuard(RegisteredSchedulingTagFiltersLock_);
                    RegisteredSchedulingTagFilters_[index] = filter;
                }
            }
            SchedulingTagFilterToIndexAndCount_.emplace(filter, TSchedulingTagFilterEntry({index, 1}));
            return index;
        } else {
            ++it->second.Count;
            return it->second.Index;
        }
    }

    void UnregisterSchedulingTagFilter(int index)
    {
        if (index == EmptySchedulingTagFilterIndex) {
            return;
        }

        TSchedulingTagFilter filter;
        {
            auto guard = ReaderGuard(RegisteredSchedulingTagFiltersLock_);
            filter = RegisteredSchedulingTagFilters_[index];
        }

        UnregisterSchedulingTagFilter(filter);
    }

    void UnregisterSchedulingTagFilter(const TSchedulingTagFilter& filter)
    {
        if (filter.IsEmpty()) {
            return;
        }
        auto it = SchedulingTagFilterToIndexAndCount_.find(filter);
        YT_VERIFY(it != SchedulingTagFilterToIndexAndCount_.end());
        --it->second.Count;
        if (it->second.Count == 0) {
            {
                auto guard = WriterGuard(RegisteredSchedulingTagFiltersLock_);
                RegisteredSchedulingTagFilters_[it->second.Index] = EmptySchedulingTagFilter;
            }

            FreeSchedulingTagFilterIndexes_.push_back(it->second.Index);
            SchedulingTagFilterToIndexAndCount_.erase(it);
        }
    }

    TTagId GetPoolProfilingTag(const TString& id)
    {
        auto it = PoolIdToProfilingTagId_.find(id);
        if (it == PoolIdToProfilingTagId_.end()) {
            it = PoolIdToProfilingTagId_.emplace(
                id,
                TProfileManager::Get()->RegisterTag("pool", id)
            ).first;
        }
        return it->second;
    }

    void ProfileOperationUnregistration(TCompositeSchedulerElement* pool, EOperationState state)
    {
        const TCompositeSchedulerElement* currentPool = pool;
        while (currentPool) {
            auto& counters = GetOrCrash(PoolToUnregisterOperationCounters_, currentPool->GetId());
            if (IsOperationFinished(state)) {
                counters.FinishedCounters[state].Increment();
            } else {
                // Unregistration for running operation is considered as ban.
                counters.BannedCounter.Increment();
            }
            currentPool = currentPool->GetParent();
        }
    }

    void OnOperationRemovedFromPool(
        const TFairShareStrategyOperationStatePtr& state,
        const TOperationElementPtr& element,
        const TCompositeSchedulerElementPtr& parent)
    {
        auto operationId = state->GetHost()->GetId();
        ReleaseOperationSlotIndex(state, parent->GetId());

        if (element->IsOperationRunningInPool()) {
            CheckOperationsPendingByPool(parent.Get());
        } else if (auto blockedPoolName = element->PendingByPool()) {
            if (auto blockedPool = FindPool(*blockedPoolName)) {
                blockedPool->PendingOperationIds().remove(operationId);
            }
        }

        // We must do this recursively cause when ephemeral pool parent is deleted, it also become ephemeral.
        RemoveEmptyEphemeralPoolsRecursive(parent.Get());
    }

    // Returns true if all pool constraints are satisfied.
    bool OnOperationAddedToPool(
        const TFairShareStrategyOperationStatePtr& state,
        const TOperationElementPtr& operationElement)
    {
        auto slotIndex = AllocateOperationSlotIndex(state, operationElement->GetParent()->GetId());
        operationElement->RegisterProfiler(slotIndex, TreeProfiler_.WithGlobal());

        auto violatedPool = FindPoolViolatingMaxRunningOperationCount(operationElement->GetMutableParent());
        if (!violatedPool) {
            operationElement->MarkOperationRunningInPool();
            return true;
        }
        operationElement->MarkPendingBy(violatedPool);

        StrategyHost_->SetOperationAlert(
            state->GetHost()->GetId(),
            EOperationAlertType::OperationPending,
            TError("Max running operation count violated")
                << TErrorAttribute("pool", violatedPool->GetId())
                << TErrorAttribute("limit", violatedPool->GetMaxRunningOperationCount())
                << TErrorAttribute("tree", TreeId_)
        );

        return false;
    }

    void RemoveEmptyEphemeralPoolsRecursive(TCompositeSchedulerElement* compositeElement)
    {
        if (!compositeElement->IsRoot() && compositeElement->IsEmpty()) {
            TPoolPtr parentPool = static_cast<TPool*>(compositeElement);
            if (parentPool->IsDefaultConfigured()) {
                UnregisterPool(parentPool);
                RemoveEmptyEphemeralPoolsRecursive(parentPool->GetMutableParent());
            }
        }
    }

    void CheckOperationsPendingByPool(TCompositeSchedulerElement* pool)
    {
        auto* current = pool;
        while (current) {
            int availableOperationCount = current->GetAvailableRunningOperationCount();
            auto& pendingOperationIds = current->PendingOperationIds();
            auto it = pendingOperationIds.begin();
            while (it != pendingOperationIds.end() && availableOperationCount > 0) {
                auto pendingOperationId = *it;
                if (auto element = FindOperationElement(pendingOperationId)) {
                    YT_VERIFY(!element->IsOperationRunningInPool());
                    if (auto violatingPool = FindPoolViolatingMaxRunningOperationCount(element->GetMutableParent())) {
                        YT_VERIFY(current != violatingPool);
                        element->MarkPendingBy(violatingPool);
                    } else {
                        element->MarkOperationRunningInPool();
                        ActivatableOperationIds_.push_back(pendingOperationId);
                        --availableOperationCount;
                    }
                }
                auto toRemove = it++;
                pendingOperationIds.erase(toRemove);
            }

            current = current->GetMutableParent();
        }
    }

    TCompositeSchedulerElement* FindPoolViolatingMaxRunningOperationCount(TCompositeSchedulerElement* pool) const
    {
        VERIFY_INVOKERS_AFFINITY(FeasibleInvokers_);

        while (pool) {
            if (pool->RunningOperationCount() >= pool->GetMaxRunningOperationCount()) {
                return pool;
            }
            pool = pool->GetMutableParent();
        }
        return nullptr;
    }

    const TCompositeSchedulerElement* FindPoolWithViolatedOperationCountLimit(const TCompositeSchedulerElementPtr& element) const
    {
        const TCompositeSchedulerElement* current = element.Get();
        while (current) {
            if (current->OperationCount() >= current->GetMaxOperationCount()) {
                return current;
            }
            current = current->GetParent();
        }
        return nullptr;
    }

    // Finds the lowest ancestor of |element| whose resource limits are too small to satisfy |neededResources|.
    const TSchedulerElement* FindAncestorWithInsufficientResourceLimits(const TSchedulerElement* element, const TJobResources& neededResources) const
    {
        const TSchedulerElement* current = element;
        while (current) {
            if (!Dominates(current->ResourceLimits(), neededResources)) {
                return current;
            }
            current = current->GetParent();
        }

        return nullptr;
    }

    TYPath GetPoolPath(const TCompositeSchedulerElementPtr& element) const
    {
        std::vector<TString> tokens;
        const auto* current = element.Get();
        while (!current->IsRoot()) {
            if (current->IsExplicit()) {
                tokens.push_back(current->GetId());
            }
            current = current->GetParent();
        }

        std::reverse(tokens.begin(), tokens.end());

        TYPath path = "/" + NYPath::ToYPathLiteral(TreeId_);
        for (const auto& token : tokens) {
            path.append('/');
            path.append(NYPath::ToYPathLiteral(token));
        }
        return path;
    }

    TCompositeSchedulerElementPtr GetDefaultParentPool() const
    {
        auto defaultPool = FindPool(Config_->DefaultParentPool);
        if (!defaultPool) {
            if (Config_->DefaultParentPool != RootPoolName) {
                auto error = TError("Default parent pool %Qv in tree %Qv is not registered", Config_->DefaultParentPool, TreeId_);
                StrategyHost_->SetSchedulerAlert(ESchedulerAlertType::UpdatePools, error);
            }
            return RootElement_;
        }

        return defaultPool;
    }

    TCompositeSchedulerElementPtr GetPoolOrParent(const TPoolName& poolName) const
    {
        TCompositeSchedulerElementPtr pool = FindPool(poolName.GetPool());
        if (pool) {
            return pool;
        }
        if (!poolName.GetParentPool()) {
            return GetDefaultParentPool();
        }
        pool = FindPool(*poolName.GetParentPool());
        if (!pool) {
            THROW_ERROR_EXCEPTION("Parent pool %Qv does not exist", poolName.GetParentPool());
        }
        return pool;
    }

    void ValidateAllOperationsCountsOnPoolChange(TOperationId operationId, const TPoolName& newPoolName) const
    {
        for (const auto* currentPool : GetPoolsToValidateOperationCountsOnPoolChange(operationId, newPoolName)) {
            if (currentPool->OperationCount() >= currentPool->GetMaxOperationCount()) {
                THROW_ERROR_EXCEPTION("Max operation count of pool %Qv violated", currentPool->GetId());
            }
            if (currentPool->RunningOperationCount() >= currentPool->GetMaxRunningOperationCount()) {
                THROW_ERROR_EXCEPTION("Max running operation count of pool %Qv violated", currentPool->GetId());
            }
        }
    }

    std::vector<const TCompositeSchedulerElement*> GetPoolsToValidateOperationCountsOnPoolChange(TOperationId operationId, const TPoolName& newPoolName) const
    {
        auto operationElement = GetOperationElement(operationId);

        std::vector<const TCompositeSchedulerElement*> poolsToValidate;
        const auto* pool = GetPoolOrParent(newPoolName).Get();
        while (pool) {
            poolsToValidate.push_back(pool);
            pool = pool->GetParent();
        }

        if (!operationElement->IsOperationRunningInPool()) {
            // Operation is pending, we must validate all pools.
            return poolsToValidate;
        }

        // Operation is running, we can validate only tail of new pools.
        std::vector<const TCompositeSchedulerElement*> oldPools;
        pool = operationElement->GetParent();
        while (pool) {
            oldPools.push_back(pool);
            pool = pool->GetParent();
        }

        while (!poolsToValidate.empty() && !oldPools.empty() && poolsToValidate.back() == oldPools.back()) {
            poolsToValidate.pop_back();
            oldPools.pop_back();
        }

        return poolsToValidate;
    }

    void ValidateOperationCountLimit(const IOperationStrategyHost* operation, const TPoolName& poolName) const
    {
        auto poolWithViolatedLimit = FindPoolWithViolatedOperationCountLimit(GetPoolOrParent(poolName));
        if (poolWithViolatedLimit) {
            THROW_ERROR_EXCEPTION(
                EErrorCode::TooManyOperations,
                "Limit for the number of concurrent operations %v for pool %Qv in tree %Qv has been reached",
                poolWithViolatedLimit->GetMaxOperationCount(),
                poolWithViolatedLimit->GetId(),
                TreeId_);
        }
    }

    void ValidateEphemeralPoolLimit(const IOperationStrategyHost* operation, const TPoolName& poolName) const
    {
        auto pool = FindPool(poolName.GetPool());
        if (pool) {
            return;
        }

        const auto& userName = operation->GetAuthenticatedUser();

        if (!poolName.GetParentPool()) {
            auto it = UserToEphemeralPoolsInDefaultPool_.find(userName);
            if (it == UserToEphemeralPoolsInDefaultPool_.end()) {
                return;
            }

            if (it->second.size() + 1 > Config_->MaxEphemeralPoolsPerUser) {
                THROW_ERROR_EXCEPTION("Limit for number of ephemeral pools %v for user %Qv in tree %Qv has been reached",
                    Config_->MaxEphemeralPoolsPerUser,
                    userName,
                    TreeId_);
            }
        }
    }

    void DoValidateOperationPoolsCanBeUsed(const IOperationStrategyHost* operation, const TPoolName& poolName) const
    {
        TCompositeSchedulerElementPtr pool = FindPool(poolName.GetPool());
        // NB: Check is not performed if operation is started in default or unknown pool.
        if (pool && pool->AreImmediateOperationsForbidden()) {
            THROW_ERROR_EXCEPTION("Starting operations immediately in pool %Qv is forbidden", poolName.GetPool());
        }

        if (!pool) {
            pool = GetPoolOrParent(poolName);
        }

        StrategyHost_->ValidatePoolPermission(GetPoolPath(pool), operation->GetAuthenticatedUser(), EPermission::Use);
    }

    int GetPoolCount() const
    {
        return Pools_.size();
    }

    TPoolPtr FindPool(const TString& id) const
    {
        auto it = Pools_.find(id);
        return it == Pools_.end() ? nullptr : it->second;
    }

    TPoolPtr GetPool(const TString& id) const
    {
        auto pool = FindPool(id);
        YT_VERIFY(pool);
        return pool;
    }

    TPool* FindRecentPoolSnapshot(const TString& poolId) const
    {
        if (RootElementSnapshot_) {
            if (auto elementFromSnapshot = RootElementSnapshot_->FindPool(poolId)) {
                return elementFromSnapshot;
            }
        }
        return FindPool(poolId).Get();
    }

    TCompositeSchedulerElement* GetRecentRootSnapshot() const
    {
        if (RootElementSnapshot_) {
            return RootElementSnapshot_->RootElement.Get();
        }
        return RootElement_.Get();
    }

    TOperationElementPtr FindOperationElement(TOperationId operationId) const
    {
        auto it = OperationIdToElement_.find(operationId);
        return it == OperationIdToElement_.end() ? nullptr : it->second;
    }

    TOperationElementPtr GetOperationElement(TOperationId operationId) const
    {
        auto element = FindOperationElement(operationId);
        YT_VERIFY(element);
        return element;
    }

    TOperationElement* FindRecentOperationElementSnapshot(TOperationId operationId) const
    {
        if (RootElementSnapshot_) {
            if (auto elementFromSnapshot = RootElementSnapshot_->FindOperationElement(operationId)) {
                return elementFromSnapshot;
            }
        }
        return FindOperationElement(operationId).Get();
    }

    void ReactivateBadPackingOperations(TFairShareContext* context)
    {
        for (const auto& operation : context->BadPackingOperations()) {
            // TODO(antonkikh): multiple activations can be implemented more efficiently.
            operation->ActivateOperation(context);
        }
        context->BadPackingOperations().clear();
    }

    void DoProfileFairShare(const TRootElementSnapshotPtr& rootElementSnapshot) const
    {
        PoolCountGauge_.Update(rootElementSnapshot->PoolNameToElement.size());

        for (const auto& [poolName, pool] : rootElementSnapshot->PoolNameToElement) {
            pool->ProfileFull(rootElementSnapshot->CoreProfilingCompatibilityEnabled);
        }
        rootElementSnapshot->RootElement.Get()->ProfileFull(rootElementSnapshot->CoreProfilingCompatibilityEnabled);

        if (Config_->EnableOperationsProfiling) {
            for (const auto& [operationId, element] : rootElementSnapshot->OperationIdToElement) {
                element->ProfileFull(rootElementSnapshot->CoreProfilingCompatibilityEnabled);
            }
        }
    }

    void DoLogFairShare(const TRootElementSnapshotPtr& rootElementSnapshot, NEventLog::TFluentLogEvent fluent) const
    {
        {
            TEventTimer timer(FairShareFluentLogTimer_);
            fluent
                .Item("tree_id").Value(TreeId_)
                .Do(BIND(&TFairShareTree::DoBuildFairShareInfo, Unretained(this), rootElementSnapshot));
        }

        {
            TEventTimer timer(FairShareTextLogTimer_);
            LogPoolsInfo(rootElementSnapshot);
            LogOperationsInfo(rootElementSnapshot);
        }
    }

    void DoEssentialLogFairShare(const TRootElementSnapshotPtr& rootElementSnapshot, NEventLog::TFluentLogEvent fluent) const
    {
        {
            TEventTimer timer(FairShareFluentLogTimer_);
            fluent
                .Item("tree_id").Value(TreeId_)
                .Do(BIND(&TFairShareTree::DoBuildEssentialFairShareInfo, Unretained(this), rootElementSnapshot));
        }

        {
            TEventTimer timer(FairShareTextLogTimer_);
            LogPoolsInfo(rootElementSnapshot);
            LogOperationsInfo(rootElementSnapshot);
        }
    }

    void LogOperationsInfo(const TRootElementSnapshotPtr& rootElementSnapshot) const
    {
        auto doLogOperationsInfo = [&] (const auto& operationIdToElement) {
            // Using structured bindings directly in the for-statement causes an ICE in GCC build.
            for (const auto& [operationId, element] : operationIdToElement) {
                YT_LOG_DEBUG("FairShareInfo: %v (OperationId: %v)",
                    element->GetLoggingString(),
                    operationId);
            }
        };

        doLogOperationsInfo(rootElementSnapshot->OperationIdToElement);
        doLogOperationsInfo(rootElementSnapshot->DisabledOperationIdToElement);
    }

    void LogPoolsInfo(const TRootElementSnapshotPtr& rootElementSnapshot) const
    {
        for (const auto& [poolName, element] : rootElementSnapshot->PoolNameToElement) {
            YT_LOG_DEBUG("FairShareInfo: %v (Pool: %v)",
                element->GetLoggingString(),
                poolName);
        }
    }

    void DoBuildFairShareInfo(const TRootElementSnapshotPtr& rootElementSnapshot, TFluentMap fluent) const
    {
        if (!rootElementSnapshot) {
            YT_LOG_DEBUG("Skipping construction of fair share info: no root element snapshot");
            return;
        }

        YT_LOG_DEBUG("Constructing fair share info for orchid");

        auto buildOperationsInfo = [&] (TFluentMap fluent, const TRawOperationElementMap::value_type& pair) {
            const auto& [operationId, element] = pair;
            fluent
                .Item(ToString(operationId)).BeginMap()
                    .Do(BIND(&TFairShareTree::DoBuildOperationProgress, Unretained(this), Unretained(element), rootElementSnapshot))
                .EndMap();
        };

        fluent
            .Do(BIND(&TFairShareTree::DoBuildPoolsInformation, Unretained(this), rootElementSnapshot))
            .Item("resource_distribution_info").BeginMap()
                .Do(BIND(&TRootElement::BuildResourceDistributionInfo, rootElementSnapshot->RootElement))
            .EndMap()
            .Item("operations").BeginMap()
                .DoFor(rootElementSnapshot->OperationIdToElement, buildOperationsInfo)
                .DoFor(rootElementSnapshot->DisabledOperationIdToElement, buildOperationsInfo)
            .EndMap();
    }

    void DoBuildPoolsInformation(const TRootElementSnapshotPtr& rootElementSnapshot, TFluentMap fluent) const
    {
        auto buildPoolInfo = [&] (const TCompositeSchedulerElement* pool, TFluentMap fluent) {
            const auto& id = pool->GetId();
            const auto& attributes = pool->Attributes();
            fluent
                .Item(id).BeginMap()
                    .Item("mode").Value(pool->GetMode())
                    .Item("running_operation_count").Value(pool->RunningOperationCount())
                    .Item("operation_count").Value(pool->OperationCount())
                    .Item("max_running_operation_count").Value(pool->GetMaxRunningOperationCount())
                    .Item("max_operation_count").Value(pool->GetMaxOperationCount())
                    .Item("aggressive_starvation_enabled").Value(pool->IsAggressiveStarvationEnabled())
                    .Item("forbid_immediate_operations").Value(pool->AreImmediateOperationsForbidden())
                    .Item("is_ephemeral").Value(pool->IsDefaultConfigured())
                    .Item("integral_guarantee_type").Value(pool->GetIntegralGuaranteeType())
                    .Item("total_resource_flow_ratio").Value(attributes.TotalResourceFlowRatio)
                    .Item("total_burst_ratio").Value(attributes.TotalBurstRatio)
                    .DoIf(pool->GetIntegralGuaranteeType() != EIntegralGuaranteeType::None, [&] (TFluentMap fluent) {
                        auto burstRatio = pool->GetSpecifiedBurstRatio();
                        auto resourceFlowRatio = pool->GetSpecifiedResourceFlowRatio();
                        fluent
                            .Item("integral_pool_capacity").Value(pool->GetIntegralPoolCapacity())
                            .Item("specified_burst_ratio").Value(burstRatio)
                            .Item("specified_burst_guarantee_resources").Value(pool->GetTotalResourceLimits() * burstRatio)
                            .Item("specified_resource_flow_ratio").Value(resourceFlowRatio)
                            .Item("specified_resource_flow").Value(pool->GetTotalResourceLimits() * resourceFlowRatio)
                            .Item("accumulated_resource_ratio_volume").Value(pool->GetAccumulatedResourceRatioVolume())
                            .Item("accumulated_resource_volume").Value(pool->GetAccumulatedResourceVolume());
                        if (burstRatio > resourceFlowRatio + RatioComparisonPrecision) {
                            fluent.Item("estimated_burst_usage_duration_sec").Value(pool->GetAccumulatedResourceRatioVolume() / (burstRatio - resourceFlowRatio));
                        }
                    })
                    .DoIf(pool->GetMode() == ESchedulingMode::Fifo, [&] (TFluentMap fluent) {
                        fluent
                            .Item("fifo_sort_parameters").Value(pool->GetFifoSortParameters());
                    })
                    .DoIf(pool->GetParent(), [&] (TFluentMap fluent) {
                        fluent
                            .Item("parent").Value(pool->GetParent()->GetId());
                    })
                    .Do(BIND(&TFairShareTree::DoBuildElementYson, Unretained(this), Unretained(pool), rootElementSnapshot))
                .EndMap();
        };

        fluent
            .Item("pool_count").Value(GetPoolCount())
            .Item("pools").BeginMap()
                .DoFor(rootElementSnapshot->PoolNameToElement, [&] (TFluentMap fluent, const TRawPoolMap::value_type& pair) {
                    buildPoolInfo(pair.second, fluent);
                })
                .Do(BIND(buildPoolInfo, Unretained(rootElementSnapshot->RootElement.Get())))
            .EndMap();
    }

    void DoBuildOperationProgress(const TOperationElement* element, const TRootElementSnapshotPtr& rootElementSnapshot, TFluentMap fluent) const
    {
        auto* parent = element->GetParent();
        fluent
            .Item("pool").Value(parent->GetId())
            .Item("slot_index").Value(element->GetMaybeSlotIndex())
            .Item("scheduling_segment").Value(element->SchedulingSegment())
            .Item("start_time").Value(element->GetStartTime())
            .Item("preemptable_job_count").Value(element->GetPreemptableJobCount())
            .Item("aggressively_preemptable_job_count").Value(element->GetAggressivelyPreemptableJobCount())
            .Item("fifo_index").Value(element->Attributes().FifoIndex)
            .Item("deactivation_reasons").Value(element->GetDeactivationReasons())
            .Item("min_needed_resources_unsatisfied_count").Value(element->GetMinNeededResourcesUnsatisfiedCount())
            .Item("tentative").Value(element->GetRuntimeParameters()->Tentative)
            .Item("starving_since").Value(element->GetStarving()
                ? std::make_optional(element->GetLastNonStarvingTime())
                : std::nullopt)
            .Do(BIND(&TFairShareTree::DoBuildElementYson, Unretained(this), Unretained(element), rootElementSnapshot));
    }

    void DoBuildElementYson(const TSchedulerElement* element, const TRootElementSnapshotPtr& rootElementSnapshot, TFluentMap fluent) const
    {
        const auto& attributes = element->Attributes();
        const auto& persistentAttributes = element->PersistentAttributes();

        auto promisedFairShareResources = element->GetTotalResourceLimits() * attributes.PromisedFairShare;

        // TODO(eshcherbin): Rethink which fields should be here and which should in in |TSchedulerElement::BuildYson|.
        // Also rethink which scalar fields should be exported to Orchid.
        fluent
            .Item("scheduling_status").Value(element->GetStatus(/* atUpdate */ true))
            .Item("starving").Value(element->GetStarving())
            .Item("fair_share_starvation_tolerance").Value(element->GetFairShareStarvationTolerance())
            .Item("fair_share_preemption_timeout").Value(element->GetFairSharePreemptionTimeout())
            .Item("adjusted_fair_share_starvation_tolerance").Value(attributes.AdjustedFairShareStarvationTolerance)
            .Item("adjusted_fair_share_preemption_timeout").Value(attributes.AdjustedFairSharePreemptionTimeout)
            .Item("weight").Value(element->GetWeight())
            .Item("max_share_ratio").Value(element->GetMaxShareRatio())
            .Item("dominant_resource").Value(attributes.DominantResource)

            .Item("resource_usage").Value(element->ResourceUsageAtUpdate())
            .Item("usage_share").Value(attributes.UsageShare)
            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("usage_ratio").Value(element->GetResourceDominantUsageShareAtUpdate())
            .Item("dominant_usage_share").Value(element->GetResourceDominantUsageShareAtUpdate())

            .Item("resource_demand").Value(element->ResourceDemand())
            .Item("demand_share").Value(attributes.DemandShare)
            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("demand_ratio").Value(MaxComponent(attributes.DemandShare))
            .Item("dominant_demand_share").Value(MaxComponent(attributes.DemandShare))

            .Item("resource_limits").Value(element->ResourceLimits())
            .Item("limits_share").Value(attributes.LimitsShare)

            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("min_share").Value(attributes.StrongGuaranteeShare)
            .Item("strong_guarantee_share").Value(attributes.StrongGuaranteeShare)
            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("min_share_resources").Value(element->GetStrongGuaranteeResources())
            .Item("strong_guarantee_resources").Value(attributes.StrongGuaranteeShare)
            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("min_share_ratio").Value(MaxComponent(attributes.StrongGuaranteeShare))

            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("fair_share_ratio").Value(MaxComponent(attributes.FairShare.Total))
            .Item("detailed_fair_share").Value(attributes.FairShare)
            .Item("detailed_dominant_fair_share").Do(std::bind(&SerializeDominant, attributes.FairShare, std::placeholders::_1))

            .Item("promised_fair_share").Value(attributes.PromisedFairShare)
            .Item("promised_dominant_fair_share").Value(MaxComponent(attributes.PromisedFairShare))
            .Item("promised_fair_share_resources").Value(promisedFairShareResources)

            .Item("proposed_integral_share").Value(attributes.ProposedIntegralShare)
            .Item("best_allocation_share").Value(persistentAttributes.BestAllocationShare)

            .Item("satisfaction_ratio").Value(attributes.SatisfactionRatio)
            .Item("local_satisfaction_ratio").Value(attributes.LocalSatisfactionRatio);
    }

    void DoBuildEssentialFairShareInfo(const TRootElementSnapshotPtr& rootElementSnapshot, TFluentMap fluent) const
    {
        auto buildOperationsInfo = [&] (TFluentMap fluent, const TRawOperationElementMap::value_type& pair) {
            const auto& [operationId, element] = pair;
            fluent
                .Item(ToString(operationId)).BeginMap()
                    .Do(BIND(&TFairShareTree::DoBuildEssentialOperationProgress, Unretained(this), Unretained(element), rootElementSnapshot))
                .EndMap();
        };

        fluent
            .Do(BIND(&TFairShareTree::DoBuildEssentialPoolsInformation, Unretained(this), rootElementSnapshot))
            .Item("operations").BeginMap()
                .DoFor(rootElementSnapshot->OperationIdToElement, buildOperationsInfo)
                .DoFor(rootElementSnapshot->DisabledOperationIdToElement, buildOperationsInfo)
            .EndMap();
    }

    void DoBuildEssentialPoolsInformation(const TRootElementSnapshotPtr& rootElementSnapshot, TFluentMap fluent) const
    {
        const auto& poolMap = rootElementSnapshot->PoolNameToElement;
        fluent
            .Item("pool_count").Value(poolMap.size())
            .Item("pools").DoMapFor(poolMap, [&] (TFluentMap fluent, const TRawPoolMap::value_type& pair) {
                const auto& [poolName, pool] = pair;
                fluent
                    .Item(poolName).BeginMap()
                        .Do(BIND(&TFairShareTree::DoBuildEssentialElementYson, Unretained(this), Unretained(pool), rootElementSnapshot))
                    .EndMap();
            });
    }

    void DoBuildEssentialOperationProgress(const TOperationElement* element, const TRootElementSnapshotPtr& rootElementSnapshot, TFluentMap fluent) const
    {
        fluent
            .Do(BIND(&TFairShareTree::DoBuildEssentialElementYson, Unretained(this), Unretained(element), rootElementSnapshot));
    }

    void DoBuildEssentialElementYson(const TSchedulerElement* element, const TRootElementSnapshotPtr& rootElementSnapshot, TFluentMap fluent) const
    {
        const auto& attributes = element->Attributes();

        fluent
            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("usage_ratio").Value(element->GetResourceDominantUsageShareAtUpdate())
            .Item("dominant_usage_share").Value(element->GetResourceDominantUsageShareAtUpdate())
            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("demand_ratio").Value(MaxComponent(attributes.DemandShare))
            .Item("dominant_demand_share").Value(MaxComponent(attributes.DemandShare))
            // COMPAT(ignat): remove it after UI and other tools migration.
            .Item("fair_share_ratio").Value(MaxComponent(attributes.FairShare.Total))
            .Item("dominant_fair_share").Value(MaxComponent(attributes.FairShare.Total))
            .Item("satisfaction_ratio").Value(attributes.SatisfactionRatio)
            .Item("dominant_resource").Value(attributes.DominantResource)
            .DoIf(element->IsOperation(), [&] (TFluentMap fluent) {
                fluent
                    .Item("resource_usage").Value(element->ResourceUsageAtUpdate());
            });
    }
};

////////////////////////////////////////////////////////////////////////////////

ISchedulerTreePtr CreateFairShareTree(
    TFairShareStrategyTreeConfigPtr config,
    TFairShareStrategyOperationControllerConfigPtr controllerConfig,
    ISchedulerStrategyHost* strategyHost,
    ISchedulerTreeHost* treeHost,
    std::vector<IInvokerPtr> feasibleInvokers,
    TString treeId)
{
    return New<TFairShareTree>(
        std::move(config),
        std::move(controllerConfig),
        strategyHost,
        treeHost,
        std::move(feasibleInvokers),
        std::move(treeId));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
