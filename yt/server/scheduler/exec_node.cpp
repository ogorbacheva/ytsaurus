#include "exec_node.h"
#include "job.h"
#include "operation.h"
#include "operation_controller.h"

#include <yt/ytlib/node_tracker_client/helpers.h>

namespace NYT {
namespace NScheduler {

using namespace NNodeTrackerClient;
using namespace NNodeTrackerServer;

////////////////////////////////////////////////////////////////////

TExecNode::TExecNode(
    TNodeId id,
    const TNodeDescriptor& descriptor)
    : Id_(id)
    , Descriptor_(descriptor)
    , ResourceLimits_(ZeroJobResources())
    , ResourceUsage_(ZeroJobResources())
    , MasterState_(ENodeState::Offline)
    , HasOngoingHeartbeat_(false)
    , IOWeight_(0)
{ }

bool TExecNode::HasEnoughResources(const TJobResources& neededResources) const
{
    return Dominates(
        ResourceLimits_,
        ResourceUsage_ + neededResources);
}

bool TExecNode::HasSpareResources(const TJobResources& resourceDiscount) const
{
    return HasEnoughResources(MinSpareNodeResources() - resourceDiscount);
}

const Stroka& TExecNode::GetDefaultAddress() const
{
    return Descriptor_.GetDefaultAddress();
}

const Stroka& TExecNode::GetInterconnectAddress() const
{
    return Descriptor_.GetInterconnectAddress();
}

bool TExecNode::CanSchedule(const TNullable<Stroka>& tag) const
{
    return !tag || SchedulingTags_.find(*tag) != SchedulingTags_.end();
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

