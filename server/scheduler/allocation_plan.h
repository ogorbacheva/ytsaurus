#pragma once

#include "public.h"

#include <yt/core/misc/enum.h>
#include <yt/core/misc/error.h>

#include <optional>
#include <variant>

namespace NYP::NServer::NScheduler {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EAllocationPlanPodRequestType,
    ((AssignPodToNode)             (  0))
    ((RevokePodFromNode)           (100))
);

DEFINE_ENUM(EAllocationPlanNodeRequestType,
    ((RemoveOrphanedAllocations)   (0))
);

////////////////////////////////////////////////////////////////////////////////

class TAllocationPlan
{
public:
    void Clear();
    void AssignPodToNode(NCluster::TPod* pod, NCluster::TNode* node);
    void RevokePodFromNode(NCluster::TPod* pod);
    void RemoveOrphanedAllocations(NCluster::TNode* node);
    void RecordComputeAllocationFailure(NCluster::TPod* pod, const TError& error);
    void RecordAssignPodToNodeFailure(NCluster::TPod* pod, const TError& error);

    struct TPodRequest
    {
        NCluster::TPod* Pod;
        EAllocationPlanPodRequestType Type;
    };

    struct TNodeRequest
    {
        EAllocationPlanNodeRequestType Type;
    };

    using TRequest = std::variant<TPodRequest, TNodeRequest>;

    struct TPerNodePlan
    {
        NCluster::TNode* Node;
        std::vector<TRequest> Requests;
    };

    std::optional<TPerNodePlan> TryExtractPerNodePlan();

    struct TFailure
    {
        NCluster::TPod* Pod;
        TError Error;
    };

    const std::vector<TFailure>& GetFailures() const;

    int GetPodCount() const;
    int GetNodeCount() const;
    int GetAssignPodToNodeCount() const;
    int GetRevokePodFromNodeCount() const;
    int GetRemoveOrphanedAllocationsCount() const;
    int GetComputeAllocationFailureCount() const;
    int GetAssignPodToNodeFailureCount() const;
    int GetFailureCount() const;

private:
    THashMultiMap<NCluster::TNode*, TRequest> NodeToRequests_;
    std::vector<TFailure> Failures_;

    int NodeCount_ = 0;
    int AssignPodToNodeCount_ = 0;
    int RevokePodFromNodeCount_ = 0;
    int RemoveOrphanedAllocationsCount_ = 0;
    int ComputeAllocationFailureCount_ = 0;
    int AssignPodToNodeFailureCount_ = 0;

    void EmplaceRequest(NCluster::TNode* node, const TRequest& request);
    void RecordFailure(NCluster::TPod* pod, const TError& error);
};

////////////////////////////////////////////////////////////////////////////////

void FormatValue(
    TStringBuilderBase* builder,
    const TAllocationPlan::TPodRequest& podRequest,
    TStringBuf format);

void FormatValue(
    TStringBuilderBase* builder,
    const TAllocationPlan::TNodeRequest& nodeRequest,
    TStringBuf format);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NServer::NScheduler
