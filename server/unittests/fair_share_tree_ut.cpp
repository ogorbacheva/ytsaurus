#include <yt/core/test_framework/framework.h>

#include <yt/server/scheduler/fair_share_tree_element.h>
#include <yt/server/scheduler/operation_controller.h>

#include <yt/core/profiling/profile_manager.h>

#include <yt/core/yson/null_consumer.h>

namespace NYT {
namespace NScheduler {
namespace {

using namespace NControllerAgent;

////////////////////////////////////////////////////////////////////////////////

struct TSchedulerStrategyHostMock
    : public TRefCounted
    , public ISchedulerStrategyHost
    , public TEventLogHostBase
{
    explicit TSchedulerStrategyHostMock(const TJobResourcesWithQuotaList& nodeResourceLimitsList)
        : NodeResourceLimitsList(nodeResourceLimitsList)
    { }

    TSchedulerStrategyHostMock()
        : TSchedulerStrategyHostMock(TJobResourcesWithQuotaList{})
    { }

    virtual TJobResources GetTotalResourceLimits() override
    {
        TJobResources totalResources;
        for (const auto& resources : NodeResourceLimitsList) {
            totalResources += resources.ToJobResources();
        }
        return totalResources;
    }

    virtual TJobResources GetResourceLimits(const TSchedulingTagFilter& filter)
    {
        if (!filter.IsEmpty()) {
            return ZeroJobResources();
        }
        return GetTotalResourceLimits();
    }

    virtual TInstant GetConnectionTime() const override
    {
        return TInstant();
    }

    virtual void ActivateOperation(const TOperationId& operationId) override
    { }

    virtual void AbortOperation(const TOperationId& /* operationId */, const TError& /* error */) override
    { }

    virtual TMemoryDistribution GetExecNodeMemoryDistribution(const TSchedulingTagFilter& filter) const override
    {
        TMemoryDistribution result;
        for (const auto& resources : NodeResourceLimitsList) {
            ++result[resources.GetMemory()];
        }
        return result;
    }

    virtual TRefCountedExecNodeDescriptorMapPtr CalculateExecNodeDescriptors(
        const TSchedulingTagFilter& /* filter */) const override
    {
        Y_UNREACHABLE();
    }

    virtual std::vector<NNodeTrackerClient::TNodeId> GetExecNodeIds(
        const TSchedulingTagFilter& /* filter */) const override
    {
        return {};
    }

    virtual void ValidatePoolPermission(
        const NYPath::TYPath& path,
        const TString& user,
        NYTree::EPermission permission) const override
    { }

    virtual void SetSchedulerAlert(ESchedulerAlertType alertType, const TError& alert) override
    { }

    virtual TFuture<void> SetOperationAlert(
        const TOperationId& operationId,
        EOperationAlertType alertType,
        const TError& alert,
        TNullable<TDuration> timeout) override
    {
        return VoidFuture;
    }

    virtual NYson::IYsonConsumer* GetEventLogConsumer() override
    {
        return NYson::GetNullYsonConsumer();
    }

    TJobResourcesWithQuotaList NodeResourceLimitsList;
};

DEFINE_REFCOUNTED_TYPE(TSchedulerStrategyHostMock)


class TOperationControllerStrategyHostMock
    : public IOperationControllerStrategyHost
{
public:
    explicit TOperationControllerStrategyHostMock(const TJobResourcesWithQuotaList& jobResourcesList)
        : JobResourcesList(jobResourcesList)
    { }

    virtual TFuture<TScheduleJobResultPtr> ScheduleJob(
        const ISchedulingContextPtr& context,
        const TJobResourcesWithQuota& jobLimits,
        const TString& /* treeId */) override
    {
        Y_UNREACHABLE();
    }

    virtual void OnNonscheduledJobAborted(const TJobId&, EAbortReason) override
    {
        Y_UNREACHABLE();
    }

    virtual TJobResources GetNeededResources() const override
    {
        TJobResources totalResources;
        for (const auto& resources : JobResourcesList) {
            totalResources += resources.ToJobResources();
        }
        return totalResources;
    }

    virtual void UpdateMinNeededJobResources() override
    { }

    virtual TJobResourcesWithQuotaList GetMinNeededJobResources() const override
    {
        TJobResourcesWithQuotaList minNeededResourcesList;
        for (const auto& resources : JobResourcesList) {
            bool dominated = false;
            for (const auto& minNeededResourcesElement : minNeededResourcesList) {
                if (Dominates(resources.ToJobResources(), minNeededResourcesElement.ToJobResources())) {
                    dominated = true;
                    break;
                }
            }
            if (!dominated) {
                minNeededResourcesList.push_back(resources);
            }
        }
        return minNeededResourcesList;
    }

    virtual int GetPendingJobCount() const override
    {
        return JobResourcesList.size();
    }

private:
    TJobResourcesWithQuotaList JobResourcesList;
};

DEFINE_REFCOUNTED_TYPE(TOperationControllerStrategyHostMock)


class TOperationStrategyHostMock
    : public TRefCounted
    , public IOperationStrategyHost
{
public:
    explicit TOperationStrategyHostMock(const TJobResourcesWithQuotaList& jobResourcesList)
        : StartTime_(TInstant::Now())
        , Id_(TGuid::Create())
        , Controller_(New<TOperationControllerStrategyHostMock>(jobResourcesList))
    { }

    virtual EOperationType GetType() const override
    {
        Y_UNREACHABLE();
    }

    virtual bool IsSchedulable() const override
    {
        return true;
    }

    virtual TInstant GetStartTime() const override
    {
        return StartTime_;
    }

    virtual TNullable<int> FindSlotIndex(const TString& /* treeId */) const override
    {
        return 0;
    }

    virtual int GetSlotIndex(const TString& treeId) const override
    {
        return 0;
    }

    virtual void SetSlotIndex(const TString& /* treeId */, int /* slotIndex */) override
    { }

    virtual TString GetAuthenticatedUser() const
    {
        return "root";
    }

    virtual const TOperationId& GetId() const
    {
        return Id_;
    }

    virtual IOperationControllerStrategyHostPtr GetControllerStrategyHost() const override
    {
        return Controller_;
    }

    virtual NYTree::IMapNodePtr GetSpec() const override
    {
        Y_UNREACHABLE();
    }

    virtual TOperationRuntimeParametersPtr GetRuntimeParameters() const override
    {
        Y_UNREACHABLE();
    }

private:
    TInstant StartTime_;
    TOperationId Id_;
    IOperationControllerStrategyHostPtr Controller_;
};

DEFINE_REFCOUNTED_TYPE(TOperationStrategyHostMock)

////////////////////////////////////////////////////////////////////////////////

TEST(FairShareTree, TestAttributes)
{
    TJobResourcesWithQuota nodeResources;
    nodeResources.SetUserSlots(10);
    nodeResources.SetCpu(10);
    nodeResources.SetMemory(100);

    TJobResourcesWithQuota jobResources;
    jobResources.SetUserSlots(1);
    jobResources.SetCpu(1);
    jobResources.SetMemory(10);

    auto operationOptions = New<TOperationFairShareTreeRuntimeParameters>();
    operationOptions->Weight = 1.0;

    auto config = New<TFairShareStrategyConfig>();
    auto treeConfig = New<TFairShareStrategyTreeConfig>();
    auto host = New<TSchedulerStrategyHostMock>(TJobResourcesWithQuotaList(10, nodeResources));

    auto rootElement = New<TRootElement>(
        host.Get(),
        treeConfig,
        // TODO(ignat): eliminate profiling from test.
        NProfiling::TProfileManager::Get()->RegisterTag("pool", RootPoolName),
        "default");

    auto poolA = New<TPool>(
        host.Get(),
        "A",
        New<TPoolConfig>(),
        true,
        treeConfig,
        NProfiling::TProfileManager::Get()->RegisterTag("pool", "A"),
        "default");

    auto poolB = New<TPool>(
        host.Get(),
        "B",
        New<TPoolConfig>(),
        true,
        treeConfig,
        NProfiling::TProfileManager::Get()->RegisterTag("pool", "B"),
        "default");

    rootElement->AddChild(poolA);
    poolA->SetParent(rootElement.Get());

    rootElement->AddChild(poolB);
    poolB->SetParent(rootElement.Get());

    auto operationX = New<TOperationStrategyHostMock>(TJobResourcesWithQuotaList(10, jobResources));
    auto operationControllerX = New<TFairShareStrategyOperationController>(operationX.Get());
    auto operationElementX = New<TOperationElement>(
        treeConfig,
        New<TStrategyOperationSpec>(),
        operationOptions,
        operationControllerX,
        config,
        host.Get(),
        operationX.Get(),
        "default");

    poolA->AddChild(operationElementX);
    operationElementX->SetParent(poolA.Get());
    operationElementX->Enable();

    auto dynamicAttributes = TDynamicAttributesList(4);
    rootElement->Update(dynamicAttributes);

    EXPECT_EQ(0.1, rootElement->Attributes().DemandRatio);
    EXPECT_EQ(0.1, poolA->Attributes().DemandRatio);
    EXPECT_EQ(0.0, poolB->Attributes().DemandRatio);
    EXPECT_EQ(0.1, operationElementX->Attributes().DemandRatio);

    EXPECT_EQ(1.0, rootElement->Attributes().FairShareRatio);
    EXPECT_EQ(0.1, rootElement->Attributes().DemandRatio);
    EXPECT_EQ(0.0, poolB->Attributes().FairShareRatio);
    EXPECT_EQ(0.1, operationElementX->Attributes().FairShareRatio);
}

TEST(FairShareTree, TestUpdatePreemptableJobsList)
{
    TJobResourcesWithQuota nodeResources;
    nodeResources.SetUserSlots(10);
    nodeResources.SetCpu(10);
    nodeResources.SetMemory(100);

    TJobResourcesWithQuota jobResources;
    jobResources.SetUserSlots(1);
    jobResources.SetCpu(1);
    jobResources.SetMemory(10);

    auto operationOptions = New<TOperationFairShareTreeRuntimeParameters>();
    operationOptions->Weight = 1.0;

    auto config = New<TFairShareStrategyConfig>();
    auto treeConfig = New<TFairShareStrategyTreeConfig>();
    auto host = New<TSchedulerStrategyHostMock>(TJobResourcesWithQuotaList(10, nodeResources));

    auto rootElement = New<TRootElement>(
        host.Get(),
        treeConfig,
        // TODO(ignat): eliminate profiling from test.
        NProfiling::TProfileManager::Get()->RegisterTag("pool", RootPoolName),
        "default");

    auto operationX = New<TOperationStrategyHostMock>(TJobResourcesWithQuotaList(10, jobResources));
    auto operationControllerX = New<TFairShareStrategyOperationController>(operationX.Get());
    auto operationElementX = New<TOperationElement>(
        treeConfig,
        New<TStrategyOperationSpec>(),
        operationOptions,
        operationControllerX,
        config,
        host.Get(),
        operationX.Get(),
        "default");

    rootElement->AddChild(operationElementX);
    operationElementX->SetParent(rootElement.Get());
    operationElementX->Enable();

    std::vector<TJobId> jobIds;
    for (int i = 0; i < 150; ++i) {
        auto jobId = TGuid::Create();
        jobIds.push_back(jobId);
        operationElementX->OnJobStarted(jobId, jobResources.ToJobResources());
    }

    auto dynamicAttributes = TDynamicAttributesList(2);
    rootElement->Update(dynamicAttributes);

    EXPECT_EQ(1.6, operationElementX->Attributes().DemandRatio);
    EXPECT_EQ(1.0, operationElementX->Attributes().FairShareRatio);

    for (int i = 0; i < 50; ++i) {
        EXPECT_FALSE(operationElementX->IsJobPreemptable(jobIds[i], true));
    }
    for (int i = 50; i < 100; ++i) {
        EXPECT_FALSE(operationElementX->IsJobPreemptable(jobIds[i], false));
        EXPECT_TRUE(operationElementX->IsJobPreemptable(jobIds[i], true));
    }
    for (int i = 100; i < 150; ++i) {
        EXPECT_TRUE(operationElementX->IsJobPreemptable(jobIds[i], false));
    }
}

TEST(FairShareTree, TestBestAllocationRatio)
{
    TJobResourcesWithQuota nodeResourcesA;
    nodeResourcesA.SetUserSlots(10);
    nodeResourcesA.SetCpu(10);
    nodeResourcesA.SetMemory(100);

    TJobResourcesWithQuota nodeResourcesB;
    nodeResourcesB.SetUserSlots(10);
    nodeResourcesB.SetCpu(10);
    nodeResourcesB.SetMemory(200);

    TJobResourcesWithQuota jobResources;
    jobResources.SetUserSlots(1);
    jobResources.SetCpu(1);
    jobResources.SetMemory(150);

    auto operationOptions = New<TOperationFairShareTreeRuntimeParameters>();
    operationOptions->Weight = 1.0;

    auto config = New<TFairShareStrategyConfig>();
    auto treeConfig = New<TFairShareStrategyTreeConfig>();
    auto host = New<TSchedulerStrategyHostMock>(TJobResourcesWithQuotaList({nodeResourcesA, nodeResourcesA, nodeResourcesB}));

    auto rootElement = New<TRootElement>(
        host.Get(),
        treeConfig,
        // TODO(ignat): eliminate profiling from test.
        NProfiling::TProfileManager::Get()->RegisterTag("pool", RootPoolName),
        "default");

    auto operationX = New<TOperationStrategyHostMock>(TJobResourcesWithQuotaList(3, jobResources));
    auto operationControllerX = New<TFairShareStrategyOperationController>(operationX.Get());
    auto operationElementX = New<TOperationElement>(
        treeConfig,
        New<TStrategyOperationSpec>(),
        operationOptions,
        operationControllerX,
        config,
        host.Get(),
        operationX.Get(),
        "default");

    rootElement->AddChild(operationElementX);
    operationElementX->SetParent(rootElement.Get());
    operationElementX->Enable();

    auto dynamicAttributes = TDynamicAttributesList(4);
    rootElement->Update(dynamicAttributes);

    EXPECT_EQ(1.125, operationElementX->Attributes().DemandRatio);
    EXPECT_EQ(0.375, operationElementX->Attributes().BestAllocationRatio);
    EXPECT_EQ(0.375, operationElementX->Attributes().FairShareRatio);
}

TEST(FairShareTree, TestOperationCountLimits)
{
    auto host = New<TSchedulerStrategyHostMock>();
    auto poolConfig = New<TPoolConfig>();
    auto treeConfig = New<TFairShareStrategyTreeConfig>();

    auto rootElement = New<TRootElement>(
        host.Get(),
        treeConfig,
        // TODO(ignat): eliminate profiling from test.
        NProfiling::TProfileManager::Get()->RegisterTag("pool", RootPoolName),
        "default");

    TPoolPtr pools[3];
    for (int i = 0; i < 3; ++i) {
        pools[i] = New<TPool>(
            host.Get(),
            "pool" + ToString(i),
            poolConfig,
            true, /* defaultConfigured */
            treeConfig,
            NProfiling::TProfileManager::Get()->RegisterTag("pool", "pool" + ToString(i)),
            "default");
    }

    rootElement->AddChild(pools[0], /* enabled */ true);
    rootElement->AddChild(pools[1], /* enabled */ true);
    pools[0]->SetParent(rootElement.Get());
    pools[1]->SetParent(rootElement.Get());

    pools[1]->AddChild(pools[2], /* enabled */ true);
    pools[2]->SetParent(pools[1].Get());

    pools[2]->IncreaseOperationCount(1);
    pools[2]->IncreaseRunningOperationCount(1);

    EXPECT_EQ(1, rootElement->OperationCount());
    EXPECT_EQ(1, rootElement->RunningOperationCount());

    EXPECT_EQ(1, pools[1]->OperationCount());
    EXPECT_EQ(1, pools[1]->RunningOperationCount());

    pools[1]->IncreaseOperationCount(5);
    EXPECT_EQ(6, rootElement->OperationCount());
    for (int i = 0; i < 5; ++i) {
        pools[1]->IncreaseOperationCount(-1);
    }
    EXPECT_EQ(1, rootElement->OperationCount());

    pools[2]->IncreaseOperationCount(-1);
    pools[2]->IncreaseRunningOperationCount(-1);

    EXPECT_EQ(0, rootElement->OperationCount());
    EXPECT_EQ(0, rootElement->RunningOperationCount());
}

TEST(FairShareTree, TestMaxPossibleUsageRatioWithoutLimit)
{
    auto config = New<TFairShareStrategyConfig>();
    auto treeConfig = New<TFairShareStrategyTreeConfig>();

    auto operationOptions = New<TOperationFairShareTreeRuntimeParameters>();
    operationOptions->Weight = 1.0;

    // Total resource vector is <100, 100>.
    TJobResourcesWithQuota nodeResources;
    nodeResources.SetCpu(100);
    nodeResources.SetMemory(100);
    auto host = New<TSchedulerStrategyHostMock>(TJobResourcesWithQuotaList({nodeResources}));

    // First operation with demand <5, 5>.
    TJobResourcesWithQuota firstOperationJobResources;
    firstOperationJobResources.SetCpu(5);
    firstOperationJobResources.SetMemory(5);

    auto firstOperation = New<TOperationStrategyHostMock>(TJobResourcesWithQuotaList(1, firstOperationJobResources));
    auto firstOperationController = New<TFairShareStrategyOperationController>(firstOperation.Get());
    auto firstOperationElement = New<TOperationElement>(
            treeConfig,
            New<TStrategyOperationSpec>(),
            operationOptions,
            firstOperationController,
            config,
            host.Get(),
            firstOperation.Get(),
            "default");

    // Second operation with demand <5, 10>.
    TJobResourcesWithQuota secondOperationJobResources;
    secondOperationJobResources.SetCpu(5);
    secondOperationJobResources.SetMemory(10);

    auto secondOperation = New<TOperationStrategyHostMock>(TJobResourcesWithQuotaList(1, secondOperationJobResources));
    auto secondOperationController = New<TFairShareStrategyOperationController>(secondOperation.Get());
    auto secondOperationElement = New<TOperationElement>(
            treeConfig,
            New<TStrategyOperationSpec>(),
            operationOptions,
            secondOperationController,
            config,
            host.Get(),
            secondOperation.Get(),
            "default");

    // Pool with total demand <10, 15>.
    auto pool = New<TPool>(
            host.Get(),
            "A",
            New<TPoolConfig>(),
            true,
            treeConfig,
            NProfiling::TProfileManager::Get()->RegisterTag("pool", "A"),
            "default");

    pool->AddChild(firstOperationElement, true);
    firstOperationElement->SetParent(pool.Get());
    pool->AddChild(secondOperationElement);
    secondOperationElement->SetParent(pool.Get());

    // Root element.
    auto rootElement = New<TRootElement>(
            host.Get(),
            treeConfig,
            // TODO(ignat): eliminate profiling from test.
            NProfiling::TProfileManager::Get()->RegisterTag("pool", RootPoolName),
            "default");

    rootElement->AddChild(pool, true);
    pool->SetParent(rootElement.Get());

    // Сheck MaxPossibleUsageRatio computation.
    auto dynamicAttributes = TDynamicAttributesList(4);
    rootElement->Update(dynamicAttributes);
    EXPECT_EQ(0.15, pool->Attributes().MaxPossibleUsageRatio);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NScheduler
} // namespace NYT
