#pragma once

#include "private.h"
#include "fair_share_tree_element.h"

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

using TTreeSnapshotId = TGuid;

////////////////////////////////////////////////////////////////////////////////

class TFairShareTreeSnapshotImpl
    : public TRefCounted
{
    DEFINE_BYVAL_RO_PROPERTY(TTreeSnapshotId, Id)

    DEFINE_BYREF_RO_PROPERTY(TSchedulerRootElementPtr, RootElement)
    DEFINE_BYREF_RO_PROPERTY(TNonOwningOperationElementMap, EnabledOperationMap)
    DEFINE_BYREF_RO_PROPERTY(TNonOwningOperationElementMap, DisabledOperationMap)
    DEFINE_BYREF_RO_PROPERTY(TNonOwningPoolElementMap, PoolMap)
    DEFINE_BYREF_RO_PROPERTY(TFairShareStrategyTreeConfigPtr, TreeConfig)
    DEFINE_BYREF_RO_PROPERTY(TFairShareStrategyOperationControllerConfigPtr, ControllerConfig)
    DEFINE_BYREF_RO_PROPERTY(TTreeSchedulingSegmentsState, SchedulingSegmentsState)
    DEFINE_BYREF_RO_PROPERTY(TCachedJobPreemptionStatuses, CachedJobPreemptionStatuses)

public:
    TFairShareTreeSnapshotImpl(
        TTreeSnapshotId id,
        TSchedulerRootElementPtr rootElement,
        TNonOwningOperationElementMap enabledOperationIdToElement,
        TNonOwningOperationElementMap disabledOperationIdToElement,
        TNonOwningPoolElementMap poolNameToElement,
        const TCachedJobPreemptionStatuses& cachedJobPreemptionStatuses,
        TFairShareStrategyTreeConfigPtr treeConfig,
        TFairShareStrategyOperationControllerConfigPtr controllerConfig,
        TTreeSchedulingSegmentsState schedulingSegmentsState);

    TSchedulerPoolElement* FindPool(const TString& poolName) const;
    TSchedulerOperationElement* FindEnabledOperationElement(TOperationId operationId) const;
    TSchedulerOperationElement* FindDisabledOperationElement(TOperationId operationId) const;
};

DEFINE_REFCOUNTED_TYPE(TFairShareTreeSnapshotImpl)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
