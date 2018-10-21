#include "job_metrics.h"

#include <yt/server/scheduler/proto/controller_agent_tracker_service.pb.h>

#include <yt/core/profiling/profiler.h>

#include <yt/core/misc/protobuf_helpers.h>

namespace NYT {
namespace NScheduler {

using namespace NProfiling;
using namespace NJobTrackerClient;
using namespace NPhoenix;

////////////////////////////////////////////////////////////////////////////////

TJobMetrics TJobMetrics::FromJobTrackerStatistics(const NJobTrackerClient::TStatistics& statistics, EJobState jobState)
{
    TJobMetrics metrics;
    metrics.DiskReads_ = FindNumericValue(statistics, "/user_job/block_io/io_read").Get(0);
    metrics.DiskWrites_ = FindNumericValue(statistics, "/user_job/block_io/io_write").Get(0);
    if (jobState == EJobState::Completed) {
        metrics.TimeCompleted_ = FindNumericValue(statistics, "/time/total").Get(0);
    } else if (jobState == EJobState::Aborted) {
        metrics.TimeAborted_ = FindNumericValue(statistics, "/time/total").Get(0);
    }
    metrics.SmoothedCpuUsage_ = FindNumericValue(statistics, "/job_proxy/smoothed_cpu_usage_x100").Get(0);
    metrics.PreemptableCpu_ = FindNumericValue(statistics, "/job_proxy/preemptable_cpu_x100").Get(0);
    return metrics;
}

bool TJobMetrics::IsEmpty() const
{
    return DiskReads_ == 0 &&
        DiskWrites_ == 0 &&
        TimeCompleted_ == 0 &&
        TimeAborted_ == 0 &&
        SmoothedCpuUsage_ == 0 &&
        PreemptableCpu_ == 0;
}

void TJobMetrics::Profile(
    TProfileCollector& collector,
    const TString& prefix,
    const NProfiling::TTagIdList& tagIds) const
{
    collector.Add(prefix + "/disk_reads", DiskReads_, EMetricType::Counter, tagIds);
    collector.Add(prefix + "/disk_writes", DiskWrites_, EMetricType::Counter, tagIds);
    collector.Add(prefix + "/time_aborted", TimeAborted_, EMetricType::Counter, tagIds);
    collector.Add(prefix + "/time_completed", TimeCompleted_, EMetricType::Counter, tagIds);
    collector.Add(prefix + "/smoothed_cpu_usage_x100", SmoothedCpuUsage_, EMetricType::Gauge, tagIds);
    collector.Add(prefix + "/preemptable_cpu_x100", PreemptableCpu_, EMetricType::Gauge, tagIds);
}

void TJobMetrics::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, DiskReads_);
    Persist(context, DiskWrites_);
    Persist(context, TimeCompleted_);
    Persist(context, TimeAborted_);
    Persist(context, SmoothedCpuUsage_);
    Persist(context, PreemptableCpu_);
}

////////////////////////////////////////////////////////////////////////////////

TJobMetrics& operator+=(TJobMetrics& lhs, const TJobMetrics& rhs)
{
    lhs.DiskReads_ += rhs.DiskReads_;
    lhs.DiskWrites_ += rhs.DiskWrites_;
    lhs.TimeAborted_ += rhs.TimeAborted_;
    lhs.TimeCompleted_ += rhs.TimeCompleted_;
    lhs.SmoothedCpuUsage_ += rhs.SmoothedCpuUsage_;
    lhs.PreemptableCpu_ += rhs.PreemptableCpu_;
    return lhs;
}

TJobMetrics& operator-=(TJobMetrics& lhs, const TJobMetrics& rhs)
{
    lhs.DiskReads_ -= rhs.DiskReads_;
    lhs.DiskWrites_ -= rhs.DiskWrites_;
    lhs.TimeAborted_ -= rhs.TimeAborted_;
    lhs.TimeCompleted_ -= rhs.TimeCompleted_;
    lhs.SmoothedCpuUsage_ -= rhs.SmoothedCpuUsage_;
    lhs.PreemptableCpu_ -= rhs.PreemptableCpu_;
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
    protoJobMetrics->set_disk_reads(jobMetrics.GetDiskReads());
    protoJobMetrics->set_disk_writes(jobMetrics.GetDiskWrites());
    protoJobMetrics->set_time_completed(jobMetrics.GetTimeCompleted());
    protoJobMetrics->set_time_aborted(jobMetrics.GetTimeAborted());
    protoJobMetrics->set_smoothed_cpu_usage(jobMetrics.GetSmoothedCpuUsage());
    protoJobMetrics->set_preemptable_cpu(jobMetrics.GetPreemptableCpu());
}

void FromProto(NScheduler::TJobMetrics* jobMetrics, const NScheduler::NProto::TJobMetrics& protoJobMetrics)
{
    jobMetrics->SetDiskReads(protoJobMetrics.disk_reads());
    jobMetrics->SetDiskWrites(protoJobMetrics.disk_writes());
    jobMetrics->SetTimeCompleted(protoJobMetrics.time_completed());
    jobMetrics->SetTimeAborted(protoJobMetrics.time_aborted());
    jobMetrics->SetSmoothedCpuUsage(protoJobMetrics.smoothed_cpu_usage());
    jobMetrics->SetPreemptableCpu(protoJobMetrics.preemptable_cpu());
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

} // namespace NScheduler
} // namespace NYT
