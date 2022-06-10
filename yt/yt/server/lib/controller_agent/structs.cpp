#include "public.h"
#include "structs.h"

#include <yt/yt/server/lib/controller_agent/serialize.h>

#include <yt/yt/server/lib/exec_node/public.h>

#include <yt/yt/server/lib/scheduler/proto/controller_agent_tracker_service.pb.h>

#include <yt/yt/ytlib/job_proxy/public.h>

#include <yt/yt/core/misc/protobuf_helpers.h>

#include <util/generic/cast.h>

#define GET_PROTO_FIELD_OR_CRASH(obj, field) ([&] { YT_VERIFY(obj->has_ ## field()); }(), obj->field())

namespace NYT::NControllerAgent {

using namespace NScheduler;
using namespace NYson;

using NYT::FromProto;
using NYT::ToProto;
using NLogging::TLogger;
using NScheduler::NProto::TSchedulerJobResultExt;
using NScheduler::ESchedulerToAgentJobEventType;

////////////////////////////////////////////////////////////////////////////////

namespace {

void MergeJobSummaries(
    TJobSummary& nodeJobSummary,
    TFinishedJobSummary&& schedulerJobSummary)
{
    YT_VERIFY(nodeJobSummary.Id == schedulerJobSummary.Id);

    nodeJobSummary.JobExecutionCompleted = schedulerJobSummary.JobExecutionCompleted;
    nodeJobSummary.FinishTime = schedulerJobSummary.FinishTime;
}

EAbortReason GetAbortReason(const TError& resultError, const TLogger& Logger)
{
    try {
        return resultError.Attributes().Get<EAbortReason>("abort_reason", EAbortReason::Scheduler);
    } catch (const std::exception& ex) {
        // Process unknown abort reason from node.
        YT_LOG_WARNING(ex, "Found unknown abort reason in job result");
        return EAbortReason::Unknown;
    }
}

ESchedulerToAgentJobEventType ParseEventType(NScheduler::NProto::TSchedulerToAgentJobEvent* protoEvent)
{
    return CheckedEnumCast<ESchedulerToAgentJobEventType>(protoEvent->event_type());
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

void FromProto(TStartedJobSummary* summary, NScheduler::NProto::TSchedulerToAgentJobEvent* protoEvent)
{
    YT_VERIFY(ParseEventType(protoEvent) == ESchedulerToAgentJobEventType::Started);

    summary->OperationId = FromProto<TOperationId>(protoEvent->operation_id());
    summary->Id = FromProto<TJobId>(protoEvent->job_id());

    summary->StartTime = FromProto<TInstant>(GET_PROTO_FIELD_OR_CRASH(protoEvent, start_time));
}

////////////////////////////////////////////////////////////////////////////////

TJobSummary::TJobSummary(TJobId id, EJobState state)
    : Result()
    , Id(id)
    , State(state)
{ }

TJobSummary::TJobSummary(NJobTrackerClient::NProto::TJobStatus* status)
    : Id(FromProto<TJobId>(status->job_id()))
    , State(CheckedEnumCast<EJobState>(status->state()))
{
    Result = std::move(*status->mutable_result());
    TimeStatistics = FromProto<NJobAgent::TTimeStatistics>(status->time_statistics());
    if (status->has_statistics()) {
        StatisticsYson = TYsonString(status->statistics());
    }
    if (status->has_phase()) {
        Phase = CheckedEnumCast<EJobPhase>(status->phase());
    }

    LastStatusUpdateTime = FromProto<TInstant>(status->status_timestamp());
    JobExecutionCompleted = status->job_execution_completed();
}

void TJobSummary::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Result);
    Persist(context, Id);
    Persist(context, State);
    Persist(context, FinishTime);
    Persist(context, Statistics);
    Persist(context, StatisticsYson);
    if (context.GetVersion() < ESnapshotVersion::DropLogAndProfile) {
        bool logAndProfile{};
        Persist(context, logAndProfile);
    }
    Persist(context, ReleaseFlags);
    Persist(context, Phase);
    Persist(context, TimeStatistics);
}

TJobResult& TJobSummary::GetJobResult()
{
    YT_VERIFY(Result);
    return *Result;
}

const TJobResult& TJobSummary::GetJobResult() const
{
    YT_VERIFY(Result);
    return *Result;
}

TSchedulerJobResultExt& TJobSummary::GetSchedulerJobResult()
{
    YT_VERIFY(Result);
    YT_VERIFY(Result->HasExtension(TSchedulerJobResultExt::scheduler_job_result_ext));
    return *Result->MutableExtension(TSchedulerJobResultExt::scheduler_job_result_ext);
}

const TSchedulerJobResultExt& TJobSummary::GetSchedulerJobResult() const
{
    YT_VERIFY(Result);
    YT_VERIFY(Result->HasExtension(TSchedulerJobResultExt::scheduler_job_result_ext));
    return Result->GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);
}

const TSchedulerJobResultExt* TJobSummary::FindSchedulerJobResult() const
{
    YT_VERIFY(Result);
    return Result->HasExtension(TSchedulerJobResultExt::scheduler_job_result_ext)
        ? &Result->GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext)
        : nullptr;
}

////////////////////////////////////////////////////////////////////////////////

TCompletedJobSummary::TCompletedJobSummary(NJobTrackerClient::NProto::TJobStatus* status)
    : TJobSummary(status)
{
    YT_VERIFY(State == ExpectedState);
}

void TCompletedJobSummary::Persist(const TPersistenceContext& context)
{
    TJobSummary::Persist(context);

    using NYT::Persist;

    Persist(context, Abandoned);
    Persist(context, InterruptReason);
    // TODO(max42): now we persist only those completed job summaries that correspond
    // to non-interrupted jobs, because Persist(context, UnreadInputDataSlices) produces
    // lots of ugly template resolution errors. I wasn't able to fix it :(
    YT_VERIFY(InterruptReason == EInterruptReason::None);
    Persist(context, SplitJobCount);
}

std::unique_ptr<TCompletedJobSummary> CreateAbandonedJobSummary(TJobId jobId)
{
    TCompletedJobSummary summary{};

    summary.Id = jobId;
    summary.State = EJobState::Completed;
    summary.Abandoned = true;
    summary.FinishTime = TInstant::Now();

    return std::make_unique<TCompletedJobSummary>(std::move(summary));
}

////////////////////////////////////////////////////////////////////////////////

TAbortedJobSummary::TAbortedJobSummary(TJobId id, EAbortReason abortReason)
    : TJobSummary(id, EJobState::Aborted)
    , AbortReason(abortReason)
{
    FinishTime = TInstant::Now();
}

TAbortedJobSummary::TAbortedJobSummary(const TJobSummary& other, EAbortReason abortReason)
    : TJobSummary(other)
    , AbortReason(abortReason)
{
    State = EJobState::Aborted;
    FinishTime = TInstant::Now();
}

TAbortedJobSummary::TAbortedJobSummary(NJobTrackerClient::NProto::TJobStatus* status)
    : TJobSummary(status)
{
    YT_VERIFY(State == ExpectedState);
}

std::unique_ptr<TAbortedJobSummary> CreateAbortedJobSummary(TAbortedBySchedulerJobSummary&& eventSummary, const TLogger& Logger)
{
    auto abortReason = [&] {
        if (eventSummary.AbortReason) {
            return *eventSummary.AbortReason;
        }

        return GetAbortReason(eventSummary.Error, Logger);
    }();
    
    TAbortedJobSummary summary{eventSummary.Id, abortReason};

    summary.FinishTime = eventSummary.FinishTime;

    ToProto(summary.Result.emplace().mutable_error(), eventSummary.Error);

    summary.Scheduled = eventSummary.Scheduled;
    summary.AbortedByScheduler = true;

    return std::make_unique<TAbortedJobSummary>(std::move(summary));
}

std::unique_ptr<TAbortedJobSummary> CreateAbortedSummaryOnGetSpecFailed(TFinishedJobSummary&& finishedJobSummary)
{
    YT_VERIFY(finishedJobSummary.GetSpecFailed);
    TAbortedJobSummary summary{finishedJobSummary.Id, EAbortReason::GetSpecFailed};

    summary.FinishTime = finishedJobSummary.FinishTime;

    auto error = TError("Failed to get job spec")
        << TErrorAttribute("abort_reason", NScheduler::EAbortReason::GetSpecFailed);
    ToProto(summary.Result.emplace().mutable_error(), error);

    return std::make_unique<TAbortedJobSummary>(std::move(summary));
}

////////////////////////////////////////////////////////////////////////////////

TFailedJobSummary::TFailedJobSummary(NJobTrackerClient::NProto::TJobStatus* status)
    : TJobSummary(status)
{
    YT_VERIFY(State == ExpectedState);
}

////////////////////////////////////////////////////////////////////////////////

TRunningJobSummary::TRunningJobSummary(NJobTrackerClient::NProto::TJobStatus* status)
    : TJobSummary(status)
    , Progress(status->progress())
    , StderrSize(status->stderr_size())
{ }

////////////////////////////////////////////////////////////////////////////////

void FromProto(TFinishedJobSummary* finishedJobSummary, NScheduler::NProto::TSchedulerToAgentJobEvent* protoEvent)
{
    YT_VERIFY(ParseEventType(protoEvent) == ESchedulerToAgentJobEventType::Finished);

    finishedJobSummary->OperationId = FromProto<TOperationId>(protoEvent->operation_id());
    finishedJobSummary->Id = FromProto<TJobId>(protoEvent->job_id());

    finishedJobSummary->FinishTime = FromProto<TInstant>(GET_PROTO_FIELD_OR_CRASH(protoEvent, finish_time));
    finishedJobSummary->JobExecutionCompleted = GET_PROTO_FIELD_OR_CRASH(protoEvent, job_execution_completed);
    if (protoEvent->has_interrupt_reason()) {
        finishedJobSummary->InterruptReason = CheckedEnumCast<EInterruptReason>(protoEvent->interrupt_reason());
    }
    if (protoEvent->has_preempted_for()) {
        finishedJobSummary->PreemptedFor = FromProto<NScheduler::TPreemptedFor>(protoEvent->preempted_for());
    }
    finishedJobSummary->Preempted = GET_PROTO_FIELD_OR_CRASH(protoEvent, preempted);
    if (protoEvent->has_preemption_reason()) {
        finishedJobSummary->PreemptionReason = FromProto<TString>(protoEvent->preemption_reason());
    }

    finishedJobSummary->GetSpecFailed = protoEvent->get_spec_failed();
}

////////////////////////////////////////////////////////////////////////////////

void FromProto(TAbortedBySchedulerJobSummary* abortedJobSummary, NScheduler::NProto::TSchedulerToAgentJobEvent* protoEvent)
{
    YT_VERIFY(ParseEventType(protoEvent) == ESchedulerToAgentJobEventType::AbortedByScheduler);

    abortedJobSummary->OperationId = FromProto<TOperationId>(protoEvent->operation_id());
    abortedJobSummary->Id = FromProto<TJobId>(protoEvent->job_id());
    abortedJobSummary->FinishTime = FromProto<TInstant>(GET_PROTO_FIELD_OR_CRASH(protoEvent, finish_time));
    if (protoEvent->has_abort_reason()) {
        abortedJobSummary->AbortReason = CheckedEnumCast<EAbortReason>(protoEvent->abort_reason());
    }
    abortedJobSummary->Error = FromProto<TError>(GET_PROTO_FIELD_OR_CRASH(protoEvent, error));
    abortedJobSummary->Scheduled = GET_PROTO_FIELD_OR_CRASH(protoEvent, scheduled);
}

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<TFailedJobSummary> MergeJobSummaries(
    std::unique_ptr<TFailedJobSummary> nodeJobSummary,
    TFinishedJobSummary&& schedulerJobSummary,
    const TLogger& /*Logger*/)
{
    MergeJobSummaries(*nodeJobSummary, std::move(schedulerJobSummary));

    return nodeJobSummary;
}

std::unique_ptr<TAbortedJobSummary> MergeJobSummaries(
    std::unique_ptr<TAbortedJobSummary> nodeJobSummary,
    TFinishedJobSummary&& schedulerJobSummary,
    const TLogger& Logger)
{
    MergeJobSummaries(*nodeJobSummary, std::move(schedulerJobSummary));
    nodeJobSummary->PreemptedFor = std::move(schedulerJobSummary.PreemptedFor);

    auto error = FromProto<TError>(nodeJobSummary->GetJobResult().error());
    if (schedulerJobSummary.Preempted) {
        if (error.FindMatching(NExecNode::EErrorCode::AbortByScheduler) ||
            error.FindMatching(NJobProxy::EErrorCode::JobNotPrepared))
        {
            auto error = TError("Job preempted")
                << TErrorAttribute("abort_reason", EAbortReason::Preemption)
                << TErrorAttribute("preemption_reason", schedulerJobSummary.PreemptionReason);
            nodeJobSummary->Result = NJobTrackerClient::NProto::TJobResult{};
            ToProto(nodeJobSummary->GetJobResult().mutable_error(), error);
        }
    }

    if (!error.IsOK()) {
        nodeJobSummary->AbortReason = GetAbortReason(error, Logger);
    }

    return nodeJobSummary;
}

std::unique_ptr<TCompletedJobSummary> MergeJobSummaries(
    std::unique_ptr<TCompletedJobSummary> nodeJobSummary,
    TFinishedJobSummary&& schedulerJobSummary,
    const TLogger& /*Logger*/)
{
    MergeJobSummaries(*nodeJobSummary, std::move(schedulerJobSummary));
    nodeJobSummary->InterruptReason = schedulerJobSummary.InterruptReason.value_or(EInterruptReason::None);

    return nodeJobSummary;
}

std::unique_ptr<TJobSummary> MergeJobSummaries(
    std::unique_ptr<TJobSummary> nodeJobSummary,
    TFinishedJobSummary&& schedulerJobSummary,
    const TLogger& Logger)
{
    switch (nodeJobSummary->State) {
        case EJobState::Aborted:
            return MergeJobSummaries(
                SummaryCast<TAbortedJobSummary>(std::move(nodeJobSummary)),
                std::move(schedulerJobSummary),
                Logger);
        case EJobState::Completed:
            return MergeJobSummaries(
                SummaryCast<TCompletedJobSummary>(std::move(nodeJobSummary)),
                std::move(schedulerJobSummary),
                Logger);
        case EJobState::Failed:
            return MergeJobSummaries(
                SummaryCast<TFailedJobSummary>(std::move(nodeJobSummary)),
                std::move(schedulerJobSummary),
                Logger);
        default:
            YT_ABORT();
    }
}

std::unique_ptr<TJobSummary> ParseJobSummary(NJobTrackerClient::NProto::TJobStatus* const status, const TLogger& Logger)
{
    const auto state = static_cast<EJobState>(status->state());
    switch (state) {
        case EJobState::Completed:
            return std::make_unique<TCompletedJobSummary>(status);
        case EJobState::Failed:
            return std::make_unique<TFailedJobSummary>(status);
        case EJobState::Aborted:
            return std::make_unique<TAbortedJobSummary>(status);
        case EJobState::Running:
            return std::make_unique<TRunningJobSummary>(status);
        default:
            YT_LOG_ERROR(
                "Unexpected job state in parsing status (JobState: %v, JobId: %v)",
                state,
                FromProto<TJobId>(status->job_id()));
            YT_ABORT();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
