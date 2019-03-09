#include "job_metrics.h"

#include <yt/server/lib/scheduler/proto/controller_agent_tracker_service.pb.h>

#include <yt/core/profiling/profiler.h>
#include <yt/core/profiling/metrics_accumulator.h>

#include <yt/core/ytree/fluent.h>

#include <yt/core/misc/protobuf_helpers.h>

namespace NYT::NScheduler {

using namespace NProfiling;
using namespace NJobTrackerClient;
using namespace NPhoenix;
using namespace NYTree;
using NYT::ToProto;
using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

void TCustomJobMetricDescription::Persist(const TStreamPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, StatisticsPath);
    Persist(context, ProfilingName);
}

bool operator==(const TCustomJobMetricDescription& lhs, const TCustomJobMetricDescription& rhs)
{
    return lhs.StatisticsPath == rhs.StatisticsPath &&
        lhs.ProfilingName == rhs.ProfilingName;
}

bool operator<(const TCustomJobMetricDescription& lhs, const TCustomJobMetricDescription& rhs)
{
    if (lhs.StatisticsPath != rhs.StatisticsPath) {
        return lhs.StatisticsPath < rhs.StatisticsPath;
    }
    return lhs.ProfilingName < rhs.ProfilingName;
}

void Serialize(const TCustomJobMetricDescription& customJobMetricDescription, NYson::IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("statisitcs_path").Value(customJobMetricDescription.StatisticsPath)
            .Item("profiling_name").Value(customJobMetricDescription.ProfilingName)
        .EndMap();
}

void Deserialize(TCustomJobMetricDescription& customJobMetricDescription, NYTree::INodePtr node)
{
    customJobMetricDescription.StatisticsPath = node->AsMap()->GetChild("statistics_path")->AsString()->GetValue();
    customJobMetricDescription.ProfilingName = node->AsMap()->GetChild("profiling_name")->AsString()->GetValue();
}

////////////////////////////////////////////////////////////////////////////////

TJobMetrics TJobMetrics::FromJobTrackerStatistics(
    const NJobTrackerClient::TStatistics& statistics,
    EJobState jobState,
    const std::vector<TCustomJobMetricDescription>& customJobMetricDescriptions)
{
    TJobMetrics metrics;

    metrics.Values()[EJobMetricName::UserJobIOReads] =
        FindNumericValue(statistics, "/user_job/block_io/io_read").value_or(0);
    metrics.Values()[EJobMetricName::UserJobIOWrites] =
        FindNumericValue(statistics, "/user_job/block_io/io_write").value_or(0);
    metrics.Values()[EJobMetricName::UserJobIOTotal] =
        FindNumericValue(statistics, "/user_job/block_io/io_total").value_or(0);
    metrics.Values()[EJobMetricName::UserJobBytesRead] =
        FindNumericValue(statistics, "/user_job/block_io/bytes_read").value_or(0);
    metrics.Values()[EJobMetricName::UserJobBytesWritten] =
        FindNumericValue(statistics, "/user_job/block_io/bytes_written").value_or(0);

    metrics.Values()[EJobMetricName::TotalTime] =
        FindNumericValue(statistics, "/time/total").value_or(0);
    metrics.Values()[EJobMetricName::ExecTime] =
        FindNumericValue(statistics, "/time/exec").value_or(0);
    metrics.Values()[EJobMetricName::PrepareTime] =
        FindNumericValue(statistics, "/time/prepare").value_or(0);
    metrics.Values()[EJobMetricName::ArtifactsDownloadTime] =
        FindNumericValue(statistics, "/time/artifacts_download").value_or(0);

    if (jobState == EJobState::Completed) {
        metrics.Values()[EJobMetricName::TotalTimeCompleted] =
            FindNumericValue(statistics, "/time/total").value_or(0);
    } else if (jobState == EJobState::Aborted) {
        metrics.Values()[EJobMetricName::TotalTimeAborted] =
            FindNumericValue(statistics, "/time/total").value_or(0);
    }

    metrics.Values()[EJobMetricName::AggregatedSmoothedCpuUsageX100] =
        FindNumericValue(statistics, "/job_proxy/aggregated_smoothed_cpu_usage_x100").value_or(0);
    metrics.Values()[EJobMetricName::AggregatedMaxCpuUsageX100] =
        FindNumericValue(statistics, "/job_proxy/aggregated_max_cpu_usage_x100").value_or(0);
    metrics.Values()[EJobMetricName::AggregatedPreemptableCpuX100] =
        FindNumericValue(statistics, "/job_proxy/aggregated_preemptable_cpu_x100").value_or(0);
    metrics.Values()[EJobMetricName::AggregatedPreemptedCpuX100] =
        FindNumericValue(statistics, "/job_proxy/aggregated_preempted_cpu_x100").value_or(0);

    for (const auto& jobMetriDescription : customJobMetricDescriptions) {
        metrics.CustomValues()[jobMetriDescription] = FindNumericValue(statistics, jobMetriDescription.StatisticsPath).value_or(0);
    }

    return metrics;
}

bool TJobMetrics::IsEmpty() const
{
    return std::all_of(Values_.begin(), Values_.end(), [] (i64 value) { return value == 0; });
}

void TJobMetrics::Profile(
    TMetricsAccumulator& collector,
    const TString& prefix,
    const NProfiling::TTagIdList& tagIds) const
{
    // NB(renadeen): you cannot use EMetricType::Gauge here.
    for (auto metricName : TEnumTraits<EJobMetricName>::GetDomainValues()) {
        auto profilingName = prefix + "/" + FormatEnum(metricName);
        collector.Add(profilingName, Values_[metricName], EMetricType::Counter, tagIds);
    }
    for (const auto& [jobMetriDescription, value] : CustomValues_) {
        auto profilingName = prefix + "/" + jobMetriDescription.ProfilingName;
        collector.Add(profilingName, value, EMetricType::Counter, tagIds);
    }
}

void TJobMetrics::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Values_);
    Persist(context, CustomValues_);
}

////////////////////////////////////////////////////////////////////////////////

TJobMetrics& operator+=(TJobMetrics& lhs, const TJobMetrics& rhs)
{
    std::transform(lhs.Values_.begin(), lhs.Values_.end(), rhs.Values_.begin(), lhs.Values_.begin(), std::plus<i64>());
    for (const auto& [jobMetriDescription, value] : rhs.CustomValues_) {
        lhs.CustomValues()[jobMetriDescription] += value;
    }
    return lhs;
}

TJobMetrics& operator-=(TJobMetrics& lhs, const TJobMetrics& rhs)
{
    std::transform(lhs.Values_.begin(), lhs.Values_.end(), rhs.Values_.begin(), lhs.Values_.begin(), std::minus<i64>());
    for (const auto& [jobMetriDescription, value] : rhs.CustomValues_) {
        lhs.CustomValues()[jobMetriDescription] -= value;
    }
    return lhs;
}

TJobMetrics operator-(const TJobMetrics& lhs, const TJobMetrics& rhs)
{
    TJobMetrics result = lhs;
    result -= rhs;
    return result;
}

TJobMetrics operator+(const TJobMetrics& lhs, const TJobMetrics& rhs)
{
    TJobMetrics result = lhs;
    result += rhs;
    return result;
}

void ToProto(NScheduler::NProto::TJobMetrics* protoJobMetrics, const NScheduler::TJobMetrics& jobMetrics)
{
    ToProto(protoJobMetrics->mutable_values(), jobMetrics.Values());

    // TODO(ignat): replace with proto map.
    for (const auto& [jobMetriDescription, value] : jobMetrics.CustomValues()) {
        auto* customValueProto = protoJobMetrics->add_custom_values();
        customValueProto->set_statistics_path(jobMetriDescription.StatisticsPath);
        customValueProto->set_profiling_name(jobMetriDescription.ProfilingName);
        customValueProto->set_value(value);
    }
}

void FromProto(NScheduler::TJobMetrics* jobMetrics, const NScheduler::NProto::TJobMetrics& protoJobMetrics)
{
    FromProto(&jobMetrics->Values(), protoJobMetrics.values());

    // TODO(ignat): replace with proto map.
    for (const auto& customValueProto : protoJobMetrics.custom_values()) {
        TCustomJobMetricDescription customJobMetric{customValueProto.statistics_path(), customValueProto.profiling_name()};
        (*jobMetrics).CustomValues().emplace(std::move(customJobMetric), customValueProto.value());
    }
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(
    NScheduler::NProto::TTreeTaggedJobMetrics* protoJobMetrics,
    const NScheduler::TTreeTaggedJobMetrics& jobMetrics)
{
    protoJobMetrics->set_tree_id(jobMetrics.TreeId);
    ToProto(protoJobMetrics->mutable_metrics(), jobMetrics.Metrics);
}

void FromProto(
    NScheduler::TTreeTaggedJobMetrics* jobMetrics,
    const NScheduler::NProto::TTreeTaggedJobMetrics& protoJobMetrics)
{
    jobMetrics->TreeId = protoJobMetrics.tree_id();
    FromProto(&jobMetrics->Metrics, protoJobMetrics.metrics());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler

////////////////////////////////////////////////////////////////////////////////

size_t THash<NYT::NScheduler::TCustomJobMetricDescription>::operator()(const NYT::NScheduler::TCustomJobMetricDescription& jobMetricDescription) const
{
    return THash<TString>()(jobMetricDescription.StatisticsPath) * 71 + THash<TString>()(jobMetricDescription.ProfilingName);
}

////////////////////////////////////////////////////////////////////////////////
