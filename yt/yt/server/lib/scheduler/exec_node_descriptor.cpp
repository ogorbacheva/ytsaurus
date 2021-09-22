#include "exec_node_descriptor.h"

#include <yt/yt/ytlib/node_tracker_client/helpers.h>

#include <yt/yt/ytlib/scheduler/job_resources_helpers.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NScheduler {

using namespace NNodeTrackerClient;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TRunningJobStatistics& statistics, TStringBuf /* format */)
{
    builder->AppendFormat("{TotalCpuTime: %v, PreemptableCpuTime: %v, TotalGpuTime: %v, PreemptableGpuTime: %v}",
        statistics.TotalCpuTime,
        statistics.PreemptableCpuTime,
        statistics.TotalGpuTime,
        statistics.PreemptableGpuTime);
}

TString ToString(const TRunningJobStatistics& statistics)
{
    return ToStringViaBuilder(statistics);
}

TString FormatRunningJobStatisticsCompact(const TRunningJobStatistics& statistics)
{
    return Format("{TCT: %v, PCT: %v, TGT: %v, PGT: %v}",
        statistics.TotalCpuTime,
        statistics.PreemptableCpuTime,
        statistics.TotalGpuTime,
        statistics.PreemptableGpuTime);
}

void Serialize(const TRunningJobStatistics& statistics, NYson::IYsonConsumer* consumer)
{
    NYTree::BuildYsonFluently(consumer)
        .BeginMap()
            .Item("total_cpu_time").Value(statistics.TotalCpuTime)
            .Item("preemptable_cpu_time").Value(statistics.PreemptableCpuTime)
            .Item("total_gpu_time").Value(statistics.TotalGpuTime)
            .Item("preemptable_gpu_time").Value(statistics.PreemptableGpuTime)
        .EndMap();
}

////////////////////////////////////////////////////////////////////////////////

TExecNodeDescriptor::TExecNodeDescriptor(
    NNodeTrackerClient::TNodeId id,
    TString address,
    std::optional<TString> dataCenter,
    double ioWeight,
    bool online,
    const TJobResources& resourceUsage,
    const TJobResources& resourceLimits,
    const TBooleanFormulaTags& tags,
    const TRunningJobStatistics& runningJobStatistics,
    ESchedulingSegment schedulingSegment,
    bool schedulingSegmentFrozen)
    : Id(id)
    , Address(std::move(address))
    , DataCenter(std::move(dataCenter))
    , IOWeight(ioWeight)
    , Online(online)
    , ResourceUsage(resourceUsage)
    , ResourceLimits(resourceLimits)
    , Tags(tags)
    , RunningJobStatistics(runningJobStatistics)
    , SchedulingSegment(schedulingSegment)
    , SchedulingSegmentFrozen(schedulingSegmentFrozen)
{ }

bool TExecNodeDescriptor::CanSchedule(const TSchedulingTagFilter& filter) const
{
    return Online && (filter.IsEmpty() || filter.CanSchedule(Tags));
}

void TExecNodeDescriptor::Persist(const TStreamPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Id);
    Persist(context, Address);
    Persist(context, IOWeight);
    Persist(context, Online);
    Persist(context, ResourceLimits);
    Persist(context, Tags);
}

void ToProto(NScheduler::NProto::TExecNodeDescriptor* protoDescriptor, const NScheduler::TExecNodeDescriptor& descriptor)
{
    protoDescriptor->set_node_id(descriptor.Id);
    protoDescriptor->set_address(descriptor.Address);
    protoDescriptor->set_io_weight(descriptor.IOWeight);
    protoDescriptor->set_online(descriptor.Online);
    ToProto(protoDescriptor->mutable_resource_limits(), descriptor.ResourceLimits);
    for (const auto& tag : descriptor.Tags.GetSourceTags()) {
        protoDescriptor->add_tags(tag);
    }
}

void FromProto(NScheduler::TExecNodeDescriptor* descriptor, const NScheduler::NProto::TExecNodeDescriptor& protoDescriptor)
{
    descriptor->Id = protoDescriptor.node_id();
    descriptor->Address = protoDescriptor.address();
    descriptor->IOWeight = protoDescriptor.io_weight();
    descriptor->Online = protoDescriptor.online();
    FromProto(&descriptor->ResourceLimits, protoDescriptor.resource_limits());
    THashSet<TString> tags;
    for (const auto& tag : protoDescriptor.tags()) {
        tags.insert(tag);
    }
    descriptor->Tags = TBooleanFormulaTags(std::move(tags));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler

