#include "node_shard.h"
#include "helpers.h"
#include "private.h"
#include "scheduler_strategy.h"

#include <yt/server/exec_agent/public.h>

#include <yt/server/scheduler/config.h>

#include <yt/server/cell_scheduler/bootstrap.h>

#include <yt/server/shell/config.h>

#include <yt/ytlib/object_client/helpers.h>

#include <yt/core/misc/finally.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

using namespace NCellScheduler;
using namespace NChunkClient;
using namespace NConcurrency;
using namespace NJobProberClient;
using namespace NNodeTrackerServer;
using namespace NObjectClient;
using namespace NProfiling;
using namespace NScheduler::NProto;
using namespace NShell;
using namespace NYTree;
using namespace NYson;

using NNodeTrackerClient::NodeIdFromObjectId;

using NNodeTrackerClient::TNodeId;
using NNodeTrackerClient::TNodeDescriptor;

using NCypressClient::TObjectId;

////////////////////////////////////////////////////////////////////////////////

static const auto& Profiler = SchedulerProfiler;

static NProfiling::TAggregateCounter AnalysisTimeCounter;
static NProfiling::TAggregateCounter StrategyJobProcessingTimeCounter;
static NProfiling::TAggregateCounter ScheduleTimeCounter;

////////////////////////////////////////////////////////////////////////////////

TNodeShard::TNodeShard(
    int id,
    const TCellTag& primaryMasterCellTag,
    TSchedulerConfigPtr config,
    INodeShardHost* host,
    TBootstrap* bootstrap)
    : Id_(id)
    , ActionQueue_(New<TActionQueue>(Format("NodeShard:%v", id)))
    , PrimaryMasterCellTag_(primaryMasterCellTag)
    , Config_(config)
    , Host_(host)
    , Bootstrap_(bootstrap)
    , Logger(SchedulerLogger)
{
    Logger.AddTag("NodeShardId: %v", Id_);
}

IInvokerPtr TNodeShard::GetInvoker()
{
    return ActionQueue_->GetInvoker();
}

void TNodeShard::UpdateConfig(const TSchedulerConfigPtr& config)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    Config_ = config;
}

void TNodeShard::OnMasterDisconnected()
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    for (const auto& pair : IdToNode_) {
        auto node = pair.second;
        node->Jobs().clear();
        node->IdToJob().clear();
    }

    ActiveJobCount_ = 0;

    for (auto state : TEnumTraits<EJobState>::GetDomainValues()) {
        for (auto type : TEnumTraits<EJobType>::GetDomainValues()) {
            JobCounter_[state][type] = 0;
            for (auto reason : TEnumTraits<EAbortReason>::GetDomainValues()) {
                AbortedJobCounter_[reason][state][type] = 0;
            }
            for (auto reason : TEnumTraits<EInterruptReason>::GetDomainValues()) {
                CompletedJobCounter_[reason][state][type] = 0;
            }
        }
    }

    SubmitUpdatedAndCompletedJobsToStrategy();
}

void TNodeShard::RegisterOperation(const TOperationId& operationId, const IOperationControllerPtr& operationController)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    YCHECK(OperationStates_.emplace(operationId, operationController).second);
}

void TNodeShard::UnregisterOperation(const TOperationId& operationId)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto it = OperationStates_.find(operationId);
    YCHECK(it != OperationStates_.end());
    for (const auto& job : it->second.Jobs) {
        YCHECK(job.second->GetHasPendingUnregistration());
    }
    OperationStates_.erase(it);
}

void TNodeShard::ProcessHeartbeat(const TScheduler::TCtxHeartbeatPtr& context)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto* request = &context->Request();
    auto* response = &context->Response();

    auto nodeId = request->node_id();
    auto descriptor = FromProto<TNodeDescriptor>(request->node_descriptor());
    const auto& resourceLimits = request->resource_limits();
    const auto& resourceUsage = request->resource_usage();

    context->SetRequestInfo("NodeId: %v, Address: %v, ResourceUsage: %v",
        nodeId,
        descriptor.GetDefaultAddress(),
        FormatResourceUsage(TJobResources(resourceUsage), TJobResources(resourceLimits)));

    YCHECK(Host_->GetNodeShardId(nodeId) == Id_);

    auto node = GetOrRegisterNode(nodeId, descriptor);
    // NB: Resource limits and usage of node should be updated even if
    // node is offline to avoid getting incorrect total limits when node becomes online.
    UpdateNodeResources(node, request->resource_limits(), request->resource_usage());
    if (node->GetMasterState() != ENodeState::Online) {
        THROW_ERROR_EXCEPTION("Node is not online");
    }

    // We should process only one heartbeat at a time from the same node.
    if (node->GetHasOngoingHeartbeat()) {
        THROW_ERROR_EXCEPTION("Node has ongoing heartbeat");
    }

    TLeaseManager::RenewLease(node->GetLease());

    bool isThrottlingActive = false;
    if (ConcurrentHeartbeatCount_ > Config_->HardConcurrentHeartbeatLimit) {
        isThrottlingActive = true;
        LOG_INFO("Hard heartbeat limit reached (NodeAddress: %v, Limit: %v)",
            node->GetDefaultAddress(),
            Config_->HardConcurrentHeartbeatLimit);
    } else if (ConcurrentHeartbeatCount_ > Config_->SoftConcurrentHeartbeatLimit &&
        node->GetLastSeenTime() + Config_->HeartbeatProcessBackoff > TInstant::Now())
    {
        isThrottlingActive = true;
        LOG_INFO("Soft heartbeat limit reached (NodeAddress: %v, Limit: %v)",
            node->GetDefaultAddress(),
            Config_->SoftConcurrentHeartbeatLimit);
    }

    response->set_enable_statistics_reporter(Config_->EnableStatisticsReporter);

    TFuture<void> scheduleJobsAsyncResult = VoidFuture;

    {
        BeginNodeHeartbeatProcessing(node);
        auto heartbeatGuard = Finally([&] {
            EndNodeHeartbeatProcessing(node);
        });

        // NB: No exception must leave this try/catch block.
        try {
            std::vector<TJobPtr> runningJobs;
            bool hasWaitingJobs = false;
            PROFILE_AGGREGATED_TIMING (AnalysisTimeCounter) {
                ProcessHeartbeatJobs(
                    node,
                    request,
                    response,
                    &runningJobs,
                    &hasWaitingJobs);
            }

            if (hasWaitingJobs || isThrottlingActive) {
                if (hasWaitingJobs) {
                    LOG_DEBUG("Waiting jobs found, suppressing new jobs scheduling");
                }
                if (isThrottlingActive) {
                    LOG_DEBUG("Throttling is active, suppressing new jobs scheduling");
                }
                response->set_scheduling_skipped(true);
            } else {
                auto schedulingContext = CreateSchedulingContext(
                    Config_,
                    node,
                    Host_->GetJobSpecSliceThrottler(),
                    runningJobs,
                    PrimaryMasterCellTag_);

                PROFILE_AGGREGATED_TIMING (StrategyJobProcessingTimeCounter) {
                    SubmitUpdatedAndCompletedJobsToStrategy();
                }

                PROFILE_AGGREGATED_TIMING (ScheduleTimeCounter) {
                    node->SetHasOngoingJobsScheduling(true);
                    WaitFor(Host_->GetStrategy()->ScheduleJobs(schedulingContext))
                        .ThrowOnError();
                    node->SetHasOngoingJobsScheduling(false);
                }

                TotalResourceUsage_ -= node->GetResourceUsage();
                node->SetResourceUsage(schedulingContext->ResourceUsage());
                TotalResourceUsage_ += node->GetResourceUsage();

                scheduleJobsAsyncResult = ProcessScheduledJobs(
                    schedulingContext,
                    context);

                // NB: some jobs maybe considered aborted after processing scheduled jobs.
                PROFILE_AGGREGATED_TIMING (StrategyJobProcessingTimeCounter) {
                    SubmitUpdatedAndCompletedJobsToStrategy();
                }

                response->set_scheduling_skipped(false);
            }

            std::vector<TJobPtr> jobsWithPendingUnregistration;
            for (const auto& job : node->Jobs()) {
                if (job->GetHasPendingUnregistration()) {
                    jobsWithPendingUnregistration.push_back(job);
                }
            }

            for (const auto& job : jobsWithPendingUnregistration) {
                DoUnregisterJob(job);
            }
        } catch (const std::exception& ex) {
            LOG_FATAL(ex, "Failed to process heartbeat");
        }
    }

    context->ReplyFrom(scheduleJobsAsyncResult);
}

TExecNodeDescriptorListPtr TNodeShard::GetExecNodeDescriptors()
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto result = New<TExecNodeDescriptorList>();
    result->Descriptors.reserve(IdToNode_.size());
    for (const auto& pair : IdToNode_) {
        const auto& node = pair.second;
        if (node->GetMasterState() == ENodeState::Online) {
            result->Descriptors.push_back(node->BuildExecDescriptor());
        }
    }

    {
        TWriterGuard guard(CachedExecNodeDescriptorsLock_);
        CachedExecNodeDescriptors_ = result;
        CachedExecNodeDescriptorsLastUpdateTime_ = NProfiling::GetCpuInstant();
    }

    return result;
}

void TNodeShard::RemoveOutdatedSchedulingTagFilter(const TSchedulingTagFilter& filter)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    TWriterGuard guard(ResourcesLock_);
    SchedulingTagFilterToResources_.erase(filter);
}

void TNodeShard::HandleNodesAttributes(const std::vector<std::pair<TString, INodePtr>>& nodeMaps)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    for (const auto& nodeMap : nodeMaps) {
        const auto& address = nodeMap.first;
        const auto& attributes = nodeMap.second->Attributes();
        auto objectId = attributes.Get<TObjectId>("id");
        auto nodeId = NodeIdFromObjectId(objectId);
        auto newState = attributes.Get<ENodeState>("state");
        auto ioWeights = attributes.Get<yhash<TString, double>>("io_weights", {});

        LOG_DEBUG("Handling node attributes (NodeId: %v, Address: %v, ObjectId: %v, NewState: %v)",
            nodeId,
            address,
            objectId,
            newState);

        YCHECK(Host_->GetNodeShardId(nodeId) == Id_);

        if (IdToNode_.find(nodeId) == IdToNode_.end()) {
            if (newState == ENodeState::Online) {
                LOG_WARNING("Node is not registered at scheduler but online at master (NodeId: %v, Address: %v)",
                    nodeId,
                    address);
            }
            continue;
        }

        auto execNode = IdToNode_[nodeId];
        auto oldState = execNode->GetMasterState();

        auto tags = attributes.Get<std::vector<TString>>("tags");
        UpdateNodeTags(execNode, tags);

        if (oldState != newState) {
            if (oldState == ENodeState::Online && newState != ENodeState::Online) {
                SubtractNodeResources(execNode);
                AbortJobsAtNode(execNode);
            }
            if (oldState != ENodeState::Online && newState == ENodeState::Online) {
                AddNodeResources(execNode);
            }
        }

        execNode->SetMasterState(newState);
        execNode->SetIOWeights(ioWeights);

        if (oldState != newState) {
            LOG_INFO("Node state changed (NodeId: %v, Address: %v, State: %v->%v)",
                nodeId,
                address,
                oldState,
                newState);
        }
    }
}

void TNodeShard::AbortAllJobs(const TError& abortReason)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    for (auto& pair : OperationStates_) {
        auto& state = pair.second;
        state.JobsAborted = true;
        auto jobs = state.Jobs;
        for (const auto& job : jobs) {
            auto status = JobStatusFromError(abortReason);
            OnJobAborted(job.second, &status);
        }
    }
}

void TNodeShard::AbortOperationJobs(const TOperationId& operationId, const TError& abortReason, bool terminated)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto* operationState = FindOperationState(operationId);
    if (!operationState) {
        return;
    }

    operationState->Terminated = terminated;
    operationState->JobsAborted = true;
    auto jobs = operationState->Jobs;
    for (const auto& job : jobs) {
        auto status = JobStatusFromError(abortReason);
        OnJobAborted(job.second, &status, terminated);
    }

    for (const auto& job : operationState->Jobs) {
        YCHECK(job.second->GetHasPendingUnregistration());
    }
}

void TNodeShard::ResumeOperationJobs(const TOperationId& operationId)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto* operationState = FindOperationState(operationId);
    if (!operationState || operationState->Terminated) {
        return;
    }

    operationState->JobsAborted = false;
}

TYsonString TNodeShard::StraceJob(const TJobId& jobId, const TString& user)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);

    Host_->ValidateOperationPermission(user, job->GetOperationId(), EPermission::Write);

    LOG_DEBUG("Getting strace dump (JobId: %v, OperationId: %v)",
        job->GetId(),
        job->GetOperationId());

    auto proxy = CreateJobProberProxy(job);
    auto req = proxy.Strace();
    ToProto(req->mutable_job_id(), jobId);

    auto rspOrError = WaitFor(req->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error getting strace dump of job %v",
        jobId);

    const auto& rsp = rspOrError.Value();

    LOG_DEBUG("Strace dump received (JobId: %v, OperationId: %v)",
        job->GetId(),
        job->GetOperationId());

    return TYsonString(rsp->trace());
}

TYsonString TNodeShard::GetJobStatistics(const TJobId& jobId)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);
    return job->StatisticsYson();
}

void TNodeShard::DumpJobInputContext(const TJobId& jobId, const TYPath& path, const TString& user)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);

    Host_->ValidateOperationPermission(user, job->GetOperationId(), EPermission::Write);

    LOG_DEBUG("Saving input contexts (JobId: %v, OperationId: %v, Path: %v)",
        job->GetId(),
        job->GetOperationId(),
        path);

    auto proxy = CreateJobProberProxy(job);
    auto req = proxy.DumpInputContext();
    ToProto(req->mutable_job_id(), jobId);

    auto rspOrError = WaitFor(req->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(
        rspOrError,
        "Error saving input context of job %v of operation %v into %v",
        job->GetId(),
        job->GetOperationId(),
        path);

    const auto& rsp = rspOrError.Value();
    auto chunkIds = FromProto<std::vector<TChunkId>>(rsp->chunk_ids());
    YCHECK(chunkIds.size() == 1);

    auto asyncResult = Host_->AttachJobContext(path, chunkIds.front(), job->GetOperationId(), jobId);
    WaitFor(asyncResult)
        .ThrowOnError();

    LOG_DEBUG("Input contexts saved (JobId: %v, OperationId: %v)",
        job->GetId(),
        job->GetOperationId());
}

TNodeDescriptor TNodeShard::GetJobNode(const TJobId& jobId, const TString& user)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());
    auto job = GetJobOrThrow(jobId);

    Host_->ValidateOperationPermission(user, job->GetOperationId(), EPermission::Write);

    return job->GetNode()->NodeDescriptor();
}

void TNodeShard::SignalJob(const TJobId& jobId, const TString& signalName, const TString& user)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);

    Host_->ValidateOperationPermission(user, job->GetOperationId(), EPermission::Write);

    LOG_DEBUG("Sending job signal (JobId: %v, OperationId: %v, Signal: %v)",
        job->GetId(),
        job->GetOperationId(),
        signalName);

    auto proxy = CreateJobProberProxy(job);
    auto req = proxy.SignalJob();
    ToProto(req->mutable_job_id(), jobId);
    ToProto(req->mutable_signal_name(), signalName);

    auto rspOrError = WaitFor(req->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error sending signal %v to job %v",
        signalName,
        jobId);

    LOG_DEBUG("Job signal sent (JobId: %v, OperationId: %v)",
        job->GetId(),
        job->GetOperationId());
}

void TNodeShard::AbandonJob(const TJobId& jobId, const TString& user)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);

    Host_->ValidateOperationPermission(user, job->GetOperationId(), EPermission::Write);

    LOG_DEBUG("Abandoning job by user request (JobId: %v, OperationId: %v, User: %v)",
        job->GetId(),
        job->GetOperationId(),
        user);

    switch (job->GetType()) {
        case EJobType::Map:
        case EJobType::OrderedMap:
        case EJobType::SortedReduce:
        case EJobType::JoinReduce:
        case EJobType::PartitionMap:
        case EJobType::ReduceCombiner:
        case EJobType::PartitionReduce:
            break;
        default:
            THROW_ERROR_EXCEPTION("Cannot abandon job %v of operation %v since it has type %Qlv",
                job->GetId(),
                job->GetOperationId(),
                job->GetType());
    }

    if (job->GetState() != EJobState::Running &&
        job->GetState() != EJobState::Waiting)
    {
        THROW_ERROR_EXCEPTION("Cannot abandon job %v of operation %v since it is not running",
            job->GetId(),
            job->GetOperationId());
    }

    OnJobCompleted(job, nullptr /* jobStatus */, true /* abandoned */);
}

TYsonString TNodeShard::PollJobShell(const TJobId& jobId, const TYsonString& parameters, const TString& user)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);

    TShellParameters shellParameters;
    Deserialize(shellParameters, ConvertToNode(parameters));
    if (shellParameters.Operation == EShellOperation::Spawn) {
        Host_->ValidateOperationPermission(user, job->GetOperationId(), EPermission::Write);
    }

    LOG_DEBUG("Polling job shell (JobId: %v, OperationId: %v, Parameters: %v)",
        job->GetId(),
        job->GetOperationId(),
        ConvertToYsonString(parameters, EYsonFormat::Text));

    auto proxy = CreateJobProberProxy(job);
    auto req = proxy.PollJobShell();
    ToProto(req->mutable_job_id(), jobId);
    ToProto(req->mutable_parameters(), parameters.GetData());

    auto rspOrError = WaitFor(req->Invoke());
    if (!rspOrError.IsOK()) {
        THROW_ERROR_EXCEPTION("Error polling job shell for job %v", jobId)
            << rspOrError
            << TErrorAttribute("parameters", parameters);
    }

    const auto& rsp = rspOrError.Value();
    return TYsonString(rsp->result());
}

void TNodeShard::AbortJob(const TJobId& jobId, const TNullable<TDuration>& interruptTimeout, const TString& user)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);

    Host_->ValidateOperationPermission(user, job->GetOperationId(), EPermission::Write);

    if (job->GetState() != EJobState::Running &&
        job->GetState() != EJobState::Waiting)
    {
        THROW_ERROR_EXCEPTION("Cannot abort job %v of operation %v since it is not running",
            jobId,
            job->GetOperationId());
    }

    if (interruptTimeout.Get(TDuration::Zero()) != TDuration::Zero()) {
        if (!job->GetInterruptible()) {
            THROW_ERROR_EXCEPTION("Cannot interrupt job %v of type %Qlv because such job type does not support interruption",
                jobId,
                job->GetType());
        }

        LOG_DEBUG("Trying to interrupt job by user request (JobId: %v, InterruptTimeout: %v)",
            jobId,
            interruptTimeout);

        auto proxy = CreateJobProberProxy(job);
        auto req = proxy.Interrupt();
        ToProto(req->mutable_job_id(), jobId);

        auto rspOrError = WaitFor(req->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error interrupting job %v",
            jobId);

        LOG_INFO("User interrupt requested (JobId: %v, InterruptTimeout: %v)",
            jobId,
            interruptTimeout);

        DoInterruptJob(job, EInterruptReason::UserRequest, DurationToCpuDuration(*interruptTimeout), user);
    } else {
        LOG_DEBUG("Aborting job by user request (JobId: %v, OperationId: %v, User: %v)",
            jobId,
            job->GetOperationId(),
            user);

        auto status = JobStatusFromError(TError("Job aborted by user request")
            << TErrorAttribute("abort_reason", EAbortReason::UserRequest)
            << TErrorAttribute("user", user));
        OnJobAborted(job, &status);
    }
}

void TNodeShard::AbortJob(const TJobId& jobId, const TError& error)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);
    LOG_DEBUG(error, "Aborting job by internal request (JobId: %v, OperationId: %v)",
        jobId,
        job->GetOperationId());
    auto status = JobStatusFromError(error);
    OnJobAborted(job, &status);
}

void TNodeShard::BuildNodesYson(IYsonConsumer* consumer)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    for (auto& node : IdToNode_) {
        BuildNodeYson(node.second, consumer);
    }
}

void TNodeShard::BuildOperationJobsYson(const TOperationId& operationId, IYsonConsumer* consumer)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto* operationState = FindOperationState(operationId);
    if (!operationState) {
        return;
    }

    for (const auto& job : operationState->Jobs) {
        BuildYsonMapFluently(consumer)
            .Item(ToString(job.first)).BeginMap()
                .Do(BIND(BuildJobAttributes, job.second))
            .EndMap();
    }
}

void TNodeShard::BuildJobYson(const TJobId& jobId, IYsonConsumer* consumer)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto job = GetJobOrThrow(jobId);
    BuildYsonFluently(consumer)
        .BeginMap()
            .Do(BIND(BuildJobAttributes, job))
        .EndMap();
}

void TNodeShard::BuildSuspiciousJobsYson(IYsonConsumer* consumer)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    for (const auto& operationState : OperationStates_) {
        for (const auto& jobPair : operationState.second.Jobs) {
            const auto& job = jobPair.second;
            if (job->GetSuspicious()) {
                BuildSuspiciousJobYson(job, consumer);
            }
        }
    }
}

void TNodeShard::UpdateState(const TNodeShardPatch& patch)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    for (const auto& pair : patch.OperationPatches) {
        const auto& operationId = pair.first;
        const auto& operationPatch = pair.second;
        auto* operationState = FindOperationState(operationId);
        if (!operationState) {
            continue;
        }
        operationState->CanCreateJobNodeForAbortedOrFailedJobs = operationPatch.CanCreateJobNodeForAbortedOrFailedJobs;
        operationState->CanCreateJobNodeForJobsWithStderr = operationPatch.CanCreateJobNodeForJobsWithStderr;
    }
}

TJobResources TNodeShard::GetTotalResourceLimits()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(ResourcesLock_);

    return TotalResourceLimits_;
}

TJobResources TNodeShard::GetTotalResourceUsage()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(ResourcesLock_);

    return TotalResourceUsage_;
}

TJobResources TNodeShard::CalculateResourceLimits(const TSchedulingTagFilter& filter)
{
    VERIFY_THREAD_AFFINITY_ANY();

    TJobResources resources;

    {
        TReaderGuard guard(CachedExecNodeDescriptorsLock_);
        for (const auto& node : CachedExecNodeDescriptors_->Descriptors) {
            if (node.CanSchedule(filter)) {
                resources += node.ResourceLimits;
            }
        }
    }

    {
        TWriterGuard guard(ResourcesLock_);
        SchedulingTagFilterToResources_[filter] = resources;
    }

    auto delay = NProfiling::DurationToCpuDuration(Config_->NodeShardUpdateExecNodesInformationDelay);
    if (CachedExecNodeDescriptorsLastUpdateTime_ + delay < NProfiling::GetCpuInstant()) {
        GetInvoker()->Invoke(BIND(&TNodeShard::GetExecNodeDescriptors, MakeStrong(this)));
    }

    return resources;
}

TJobResources TNodeShard::GetResourceLimits(const TSchedulingTagFilter& filter)
{
    VERIFY_THREAD_AFFINITY_ANY();

    if (filter.IsEmpty()) {
        return TotalResourceLimits_;
    }

    {
        TReaderGuard guard(ResourcesLock_);

        auto it = SchedulingTagFilterToResources_.find(filter);
        if (it == SchedulingTagFilterToResources_.end()) {
            guard.Release();
            return CalculateResourceLimits(filter);
        } else {
            return it->second;
        }
    }
}

int TNodeShard::GetActiveJobCount()
{
    VERIFY_THREAD_AFFINITY_ANY();

    return ActiveJobCount_;
}

TJobCounter TNodeShard::GetJobCounter()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(JobCounterLock_);

    return JobCounter_;
}

TAbortedJobCounter TNodeShard::GetAbortedJobCounter()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(JobCounterLock_);

    return AbortedJobCounter_;
}

TCompletedJobCounter TNodeShard::GetCompletedJobCounter()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(JobCounterLock_);

    return CompletedJobCounter_;
}

TJobTimeStatisticsDelta TNodeShard::GetJobTimeStatisticsDelta()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TWriterGuard guard(JobTimeStatisticsDeltaLock_);

    auto result = JobTimeStatisticsDelta_;
    JobTimeStatisticsDelta_.Reset();
    return result;
}

int TNodeShard::GetExecNodeCount()
{
    VERIFY_THREAD_AFFINITY_ANY();

    return ExecNodeCount_;
}

int TNodeShard::GetTotalNodeCount()
{
    VERIFY_THREAD_AFFINITY_ANY();

    return TotalNodeCount_;
}

TExecNodePtr TNodeShard::GetOrRegisterNode(TNodeId nodeId, const TNodeDescriptor& descriptor)
{
    auto it = IdToNode_.find(nodeId);
    if (it == IdToNode_.end()) {
        return RegisterNode(nodeId, descriptor);
    }

    auto node = it->second;
    // Update the current descriptor, just in case.
    node->NodeDescriptor() = descriptor;
    return node;
}

TExecNodePtr TNodeShard::RegisterNode(TNodeId nodeId, const TNodeDescriptor& descriptor)
{
    auto node = New<TExecNode>(nodeId, descriptor);
    const auto& address = node->GetDefaultAddress();

    auto lease = TLeaseManager::CreateLease(
        Config_->NodeHeartbeatTimeout,
        BIND(&TNodeShard::UnregisterNode, MakeWeak(this), node)
            .Via(GetInvoker()));

    node->SetLease(lease);
    YCHECK(IdToNode_.insert(std::make_pair(node->GetId(), node)).second);

    LOG_INFO("Node registered (Address: %v)", address);

    return node;
}

void TNodeShard::UnregisterNode(TExecNodePtr node)
{
    if (node->GetHasOngoingHeartbeat()) {
        LOG_INFO("Node unregistration postponed until heartbeat is finished (Address: %v)",
            node->GetDefaultAddress());
        node->SetHasPendingUnregistration(true);
    } else {
        DoUnregisterNode(node);
    }
}

void TNodeShard::DoUnregisterNode(TExecNodePtr node)
{
    LOG_INFO("Node unregistered (Address: %v)", node->GetDefaultAddress());

    if (node->GetMasterState() == ENodeState::Online) {
        SubtractNodeResources(node);
    }

    AbortJobsAtNode(node);

    YCHECK(IdToNode_.erase(node->GetId()) == 1);
}

void TNodeShard::AbortJobsAtNode(TExecNodePtr node)
{
    // Make a copy, the collection will be modified.
    auto jobs = node->Jobs();
    const auto& address = node->GetDefaultAddress();
    for (const auto& job : jobs) {
        LOG_DEBUG("Aborting job on an offline node %v (JobId: %v, OperationId: %v)",
            address,
            job->GetId(),
            job->GetOperationId());
        auto status = JobStatusFromError(
            TError("Node offline")
            << TErrorAttribute("abort_reason", EAbortReason::NodeOffline));
        OnJobAborted(job, &status);
    }
}

void TNodeShard::ProcessHeartbeatJobs(
    TExecNodePtr node,
    NJobTrackerClient::NProto::TReqHeartbeat* request,
    NJobTrackerClient::NProto::TRspHeartbeat* response,
    std::vector<TJobPtr>* runningJobs,
    bool* hasWaitingJobs)
{
    auto now = GetCpuInstant();

    bool forceJobsLogging = false;
    auto lastJobsLogTime = node->GetLastJobsLogTime();
    if (!lastJobsLogTime || now > lastJobsLogTime.Get() + DurationToCpuDuration(Config_->JobsLoggingPeriod)) {
        forceJobsLogging = true;
        node->SetLastJobsLogTime(now);
    }

    bool checkMissingJobs = false;
    auto lastCheckMissingJobsTime = node->GetLastCheckMissingJobsTime();
    if (!lastCheckMissingJobsTime || now > lastCheckMissingJobsTime.Get() + DurationToCpuDuration(Config_->CheckMissingJobsPeriod)) {
        checkMissingJobs = true;
        node->SetLastCheckMissingJobsTime(now);
    }

    if (checkMissingJobs) {
        // Verify that all flags are in initial state.
        for (const auto& job : node->Jobs()) {
            YCHECK(!job->GetFoundOnNode());
        }
    }

    for (auto& jobStatus : *request->mutable_jobs()) {
        auto jobType = EJobType(jobStatus.job_type());
        // Skip jobs that are not issued by the scheduler.
        if (jobType <= EJobType::SchedulerFirst || jobType >= EJobType::SchedulerLast) {
            continue;
        }

        auto job = ProcessJobHeartbeat(
            node,
            request,
            response,
            &jobStatus,
            forceJobsLogging);
        if (job) {
            if (checkMissingJobs) {
                job->SetFoundOnNode(true);
            }
            switch (job->GetState()) {
                case EJobState::Running:
                    runningJobs->push_back(job);
                    break;
                case EJobState::Waiting:
                    *hasWaitingJobs = true;
                    break;
                default:
                    break;
            }
        }
    }

    if (checkMissingJobs) {
        std::vector<TJobPtr> missingJobs;
        for (const auto& job : node->Jobs()) {
            if (!job->GetFoundOnNode()) {
                missingJobs.push_back(job);
            } else {
                job->SetFoundOnNode(false);
            }
        }

        for (const auto& job : missingJobs) {
            LOG_ERROR("Job is missing (Address: %v, JobId: %v, OperationId: %v)",
                node->GetDefaultAddress(),
                job->GetId(),
                job->GetOperationId());
            auto status = JobStatusFromError(TError("Job vanished"));
            OnJobAborted(job, &status);
        }
    }
}

NLogging::TLogger TNodeShard::CreateJobLogger(const TJobId& jobId, EJobState state, const TString& address)
{
    auto logger = Logger;
    logger.AddTag("Address: %v, JobId: %v, State: %v",
        address,
        jobId,
        state);
    return logger;
}

TJobPtr TNodeShard::ProcessJobHeartbeat(
    TExecNodePtr node,
    NJobTrackerClient::NProto::TReqHeartbeat* request,
    NJobTrackerClient::NProto::TRspHeartbeat* response,
    TJobStatus* jobStatus,
    bool forceJobsLogging)
{
    auto jobId = FromProto<TJobId>(jobStatus->job_id());
    auto state = EJobState(jobStatus->state());
    const auto& address = node->GetDefaultAddress();

    auto Logger = CreateJobLogger(jobId, state, address);

    auto job = FindJob(jobId, node);
    if (!job) {
        switch (state) {
            case EJobState::Completed:
                LOG_DEBUG("Unknown job has completed, removal scheduled");
                ToProto(response->add_jobs_to_remove(), jobId);
                break;

            case EJobState::Failed:
                LOG_DEBUG("Unknown job has failed, removal scheduled");
                ToProto(response->add_jobs_to_remove(), jobId);
                break;

            case EJobState::Aborted:
                LOG_DEBUG(FromProto<TError>(jobStatus->result().error()), "Job aborted, removal scheduled");
                ToProto(response->add_jobs_to_remove(), jobId);
                break;

            case EJobState::Running:
                LOG_DEBUG("Unknown job is running, abort scheduled");
                ToProto(response->add_jobs_to_abort(), jobId);
                break;

            case EJobState::Waiting:
                LOG_DEBUG("Unknown job is waiting, abort scheduled");
                ToProto(response->add_jobs_to_abort(), jobId);
                break;

            case EJobState::Aborting:
                LOG_DEBUG("Job is aborting");
                break;

            default:
                Y_UNREACHABLE();
        }
        return nullptr;
    }

    auto codicilGuard = MakeOperationCodicilGuard(job->GetOperationId());

    Logger.AddTag("Type: %v, OperationId: %v",
        job->GetType(),
        job->GetOperationId());

    // Check if the job is running on a proper node.
    if (node->GetId() != job->GetNode()->GetId()) {
        const auto& expectedAddress = job->GetNode()->GetDefaultAddress();
        // Job has moved from one node to another. No idea how this could happen.
        if (state == EJobState::Aborting) {
            // Do nothing, job is already terminating.
        } else if (state == EJobState::Completed || state == EJobState::Failed || state == EJobState::Aborted) {
            ToProto(response->add_jobs_to_remove(), jobId);
            LOG_WARNING("Job status report was expected from %v, removal scheduled",
                expectedAddress);
        } else {
            ToProto(response->add_jobs_to_abort(), jobId);
            LOG_WARNING("Job status report was expected from %v, abort scheduled",
                expectedAddress);
        }
        return nullptr;
    }

    bool shouldLogJob = (state != job->GetState()) || forceJobsLogging;
    bool updateRunningJob = false;
    auto now = GetCpuInstant();
    if ((state == EJobState::Running && jobStatus->has_statistics()) || state == EJobState::Waiting) {
        auto lastRunningJobUpdateTime = job->GetLastRunningJobUpdateTime();
        if (!lastRunningJobUpdateTime || now > lastRunningJobUpdateTime.Get() + DurationToCpuDuration(Config_->RunningJobsUpdatePeriod)) {
            updateRunningJob = true;
            job->SetLastRunningJobUpdateTime(now);
        }
    }
    switch (state) {
        case EJobState::Completed: {
            LOG_DEBUG("Job completed, removal scheduled");
            OnJobCompleted(job, jobStatus);
            ToProto(response->add_jobs_to_remove(), jobId);
            break;
        }

        case EJobState::Failed: {
            auto error = FromProto<TError>(jobStatus->result().error());
            LOG_DEBUG(error, "Job failed, removal scheduled");
            OnJobFailed(job, jobStatus);
            ToProto(response->add_jobs_to_remove(), jobId);
            break;
        }

        case EJobState::Aborted: {
            auto error = FromProto<TError>(jobStatus->result().error());
            LOG_DEBUG(error, "Job aborted, removal scheduled");
            if (job->GetPreempted() && error.GetCode() == NExecAgent::EErrorCode::AbortByScheduler) {
                auto error = TError("Job preempted")
                    << TErrorAttribute("abort_reason", EAbortReason::Preemption)
                    << TErrorAttribute("preemption_reason", job->GetPreemptionReason());
                auto status = JobStatusFromError(error);
                OnJobAborted(job, &status);
            } else {
                OnJobAborted(job, jobStatus);
            }
            ToProto(response->add_jobs_to_remove(), jobId);
            break;
        }

        case EJobState::Running:
        case EJobState::Waiting:
            if (job->GetState() == EJobState::Aborted) {
                LOG_DEBUG("Aborting job");
                ToProto(response->add_jobs_to_abort(), jobId);
            } else {
                LOG_DEBUG_IF(shouldLogJob, "Job is %lv", state);
                SetJobState(job, state);
                switch (state) {
                    case EJobState::Running:
                        job->SetProgress(jobStatus->progress());
                        if (updateRunningJob) {
                            OnJobRunning(job, jobStatus);
                        }
                        if (job->GetInterruptReason() != EInterruptReason::None) {
                            ToProto(response->add_jobs_to_interrupt(), jobId);
                        }
                        if (job->GetInterruptDeadline() != 0 && now > job->GetInterruptDeadline()) {
                            LOG_DEBUG("Interrupted job deadline reached, aborting (InterruptDeadline: %v, JobId: %v, OperationId: %v)",
                                CpuInstantToInstant(job->GetInterruptDeadline()),
                                jobId,
                                job->GetOperationId());
                            ToProto(response->add_jobs_to_abort(), jobId);
                        }
                        break;

                    case EJobState::Waiting:
                        if (updateRunningJob) {
                            OnJobWaiting(job);
                        }
                        break;

                    default:
                        Y_UNREACHABLE();
                }
            }
            break;

        case EJobState::Aborting:
            LOG_DEBUG("Job is aborting");
            break;

        default:
            Y_UNREACHABLE();
    }

    return job;
}

void TNodeShard::UpdateNodeTags(TExecNodePtr node, const std::vector<TString>& tagsList)
{
    yhash_set<TString> newTags(tagsList.begin(), tagsList.end());

    if (node->GetMasterState() == ENodeState::Online) {
        TWriterGuard guard(ResourcesLock_);

        for (auto& pair : SchedulingTagFilterToResources_) {
            const auto& filter = pair.first;
            bool oldCanSchedule = filter.CanSchedule(node->Tags());
            bool newCanSchedule = filter.CanSchedule(newTags);
            if (oldCanSchedule && !newCanSchedule) {
                pair.second -= node->GetResourceLimits();
            }
            if (!oldCanSchedule && newCanSchedule) {
                pair.second += node->GetResourceLimits();
            }
        }
    }

    node->Tags() = newTags;
}

void TNodeShard::SubtractNodeResources(TExecNodePtr node)
{
    TWriterGuard guard(ResourcesLock_);

    TotalResourceLimits_ -= node->GetResourceLimits();
    TotalResourceUsage_ -= node->GetResourceUsage();
    TotalNodeCount_ -= 1;
    if (node->GetResourceLimits().GetUserSlots() > 0) {
        ExecNodeCount_ -= 1;
    }

    for (auto& pair : SchedulingTagFilterToResources_) {
        if (node->CanSchedule(pair.first)) {
            pair.second -= node->GetResourceLimits();
        }
    }
}

void TNodeShard::AddNodeResources(TExecNodePtr node)
{
    TWriterGuard guard(ResourcesLock_);

    TotalResourceLimits_ += node->GetResourceLimits();
    TotalResourceUsage_ += node->GetResourceUsage();
    TotalNodeCount_ += 1;

    if (node->GetResourceLimits().GetUserSlots() > 0) {
        ExecNodeCount_ += 1;
    } else {
        // Check that we succesfully reset all resource limits to zero for node with zero user slots.
        YCHECK(node->GetResourceLimits() == ZeroJobResources());
    }

    for (auto& pair : SchedulingTagFilterToResources_) {
        if (node->CanSchedule(pair.first)) {
            pair.second += node->GetResourceLimits();
        }
    }
}

void TNodeShard::UpdateNodeResources(TExecNodePtr node, const TJobResources& limits, const TJobResources& usage)
{
    auto oldResourceLimits = node->GetResourceLimits();
    auto oldResourceUsage = node->GetResourceUsage();

    // NB: Total limits are updated separately in heartbeat.
    if (limits.GetUserSlots() > 0) {
        if (node->GetResourceLimits().GetUserSlots() == 0 && node->GetMasterState() == ENodeState::Online) {
            ExecNodeCount_ += 1;
        }
        node->SetResourceLimits(limits);
        node->SetResourceUsage(usage);
    } else {
        if (node->GetResourceLimits().GetUserSlots() > 0 && node->GetMasterState() == ENodeState::Online) {
            ExecNodeCount_ -= 1;
        }
        node->SetResourceLimits(ZeroJobResources());
        node->SetResourceUsage(ZeroJobResources());
    }

    if (node->GetMasterState() == ENodeState::Online) {
        TWriterGuard guard(ResourcesLock_);

        TotalResourceLimits_ -= oldResourceLimits;
        TotalResourceLimits_ += node->GetResourceLimits();
        for (auto& pair : SchedulingTagFilterToResources_) {
            if (node->CanSchedule(pair.first)) {
                auto& resources = pair.second;
                resources -= oldResourceLimits;
                resources += node->GetResourceLimits();
            }
        }

        TotalResourceUsage_ -= oldResourceUsage;
        TotalResourceUsage_ += node->GetResourceUsage();
    }
}

void TNodeShard::BeginNodeHeartbeatProcessing(TExecNodePtr node)
{
    node->SetHasOngoingHeartbeat(true);

    ConcurrentHeartbeatCount_ += 1;
}

void TNodeShard::EndNodeHeartbeatProcessing(TExecNodePtr node)
{
    YCHECK(node->GetHasOngoingHeartbeat());

    node->SetHasOngoingHeartbeat(false);

    ConcurrentHeartbeatCount_ -= 1;
    node->SetLastSeenTime(TInstant::Now());

    if (node->GetHasPendingUnregistration()) {
        DoUnregisterNode(node);
    }
}

TFuture<void> TNodeShard::ProcessScheduledJobs(
    const ISchedulingContextPtr& schedulingContext,
    const TScheduler::TCtxHeartbeatPtr& rpcContext)
{
    auto* response = &rpcContext->Response();

    std::vector<TFuture<TSharedRef>> asyncJobSpecs;
    for (const auto& job : schedulingContext->StartedJobs()) {
        auto* operationState = FindOperationState(job->GetOperationId());
        if (!operationState || operationState->JobsAborted) {
            LOG_DEBUG("Dangling started job found (JobId: %v, OperationId: %v)",
                job->GetId(),
                job->GetOperationId());
            if (operationState && !operationState->Terminated) {
                const auto& controller = operationState->Controller;
                controller->GetCancelableInvoker()->Invoke(BIND(
                    &IOperationController::OnJobAborted,
                    controller,
                    Passed(std::make_unique<TAbortedJobSummary>(
                        job->GetId(),
                        EAbortReason::SchedulingOperationSuspended))));
                CompletedJobs_.emplace_back(job->GetOperationId(), job->GetId());
            }
            continue;
        }

        RegisterJob(job);
        IncreaseProfilingCounter(job, 1);

        const auto& controller = operationState->Controller;
        controller->GetCancelableInvoker()->Invoke(BIND(
            &IOperationController::OnJobStarted,
            controller,
            job->GetId(),
            job->GetStartTime()));

        auto* startInfo = response->add_jobs_to_start();
        ToProto(startInfo->mutable_job_id(), job->GetId());
        ToProto(startInfo->mutable_operation_id(), job->GetOperationId());
        *startInfo->mutable_resource_limits() = job->ResourceUsage().ToNodeResources();

        // Build spec asynchronously.
        asyncJobSpecs.push_back(
            BIND([=, this_ = MakeStrong(this), rpcContext = rpcContext, specBuilder = job->GetSpecBuilder()] () {
                if (rpcContext->IsCanceled()) {
                    THROW_ERROR_EXCEPTION(NYT::EErrorCode::Canceled, "RPC request canceled");
                }
                NJobTrackerClient::NProto::TJobSpec spec;
                specBuilder(&spec);
                return SerializeToProtoWithEnvelope(spec, Config_->JobSpecCodec);
            })
            .AsyncVia(Host_->GetJobSpecBuilderInvoker())
            .Run());

        // Release to avoid circular references.
        job->SetSpecBuilder(TJobSpecBuilder());
    }

    for (const auto& job : schedulingContext->PreemptedJobs()) {
        if (!OperationExists(job->GetOperationId()) || job->GetHasPendingUnregistration()) {
            LOG_DEBUG("Dangling preempted job found (JobId: %v, OperationId: %v)",
                job->GetId(),
                job->GetOperationId());
            continue;
        }

        if (job->GetInterruptible() && Config_->JobInterruptTimeout != TDuration::Zero()) {
            if (!job->GetPreempted()) {
                PreemptJob(job, DurationToCpuDuration(Config_->JobInterruptTimeout));
                ToProto(response->add_jobs_to_interrupt(), job->GetId());
            }
            // Else do nothing: job was already interrupted, by deadline not reached yet.
        } else {
            PreemptJob(job, Null);
            ToProto(response->add_jobs_to_abort(), job->GetId());
        }
    }

    return Combine(asyncJobSpecs).Apply(BIND([rpcContext] (const std::vector<TSharedRef>& jobSpecs) {
        auto* response = &rpcContext->Response();
        response->Attachments() = jobSpecs;
    }));
}

void TNodeShard::OnJobRunning(const TJobPtr& job, TJobStatus* status)
{
    YCHECK(job->GetState() == EJobState::Running);

    auto delta = status->resource_usage() - job->ResourceUsage();
    UpdatedJobs_.emplace_back(job->GetOperationId(), job->GetId(), delta);
    job->ResourceUsage() = status->resource_usage();
    if (job->GetState() == EJobState::Running ||
        job->GetState() == EJobState::Waiting)
    {
        if (status->has_statistics()) {
            auto asyncResult = BIND(BuildBriefStatistics, TYsonString(status->statistics()))
                .AsyncVia(Host_->GetStatisticsAnalyzerInvoker())
                .Run();

            asyncResult.Subscribe(BIND(
                &TJob::AnalyzeBriefStatistics,
                job,
                Config_->SuspiciousInactivityTimeout,
                Config_->SuspiciousCpuUsageThreshold,
                Config_->SuspiciousInputPipeIdleTimeFraction)
                .Via(GetInvoker()));

            auto* operationState = FindOperationState(job->GetOperationId());
            if (operationState) {
                const auto& controller = operationState->Controller;
                BIND(&IOperationController::OnJobRunning,
                    controller,
                    Passed(std::make_unique<TJobSummary>(job)))
                    .Via(controller->GetCancelableInvoker())
                    .Run();
            }
        }

        job->SetStatus(status);
    }
}

void TNodeShard::OnJobWaiting(const TJobPtr& /*job*/)
{
    // Do nothing.
}

void TNodeShard::OnJobCompleted(const TJobPtr& job, TJobStatus* status, bool abandoned)
{
    if (job->GetState() == EJobState::Running ||
        job->GetState() == EJobState::Waiting ||
        job->GetState() == EJobState::None)
    {
        // The value of status may be nullptr on abandoned jobs.
        if (status != nullptr) {
            const auto& result = status->result();
            const auto& schedulerResultExt = result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);
            if (schedulerResultExt.unread_input_data_slice_descriptors_size() == 0) {
                job->SetInterruptReason(EInterruptReason::None);
            }
        } else {
            YCHECK(abandoned);
            job->SetInterruptReason(EInterruptReason::None);
        }

        SetJobState(job, EJobState::Completed);
        job->SetStatus(status);

        OnJobFinished(job);

        ProcessFinishedJobResult(job);

        auto* operationState = FindOperationState(job->GetOperationId());
        if (operationState) {
            const auto& controller = operationState->Controller;
            controller->GetCancelableInvoker()->Invoke(BIND(
                &IOperationController::OnJobCompleted,
                controller,
                Passed(std::make_unique<TCompletedJobSummary>(job, abandoned))));
        }
    }

    UnregisterJob(job);
}

void TNodeShard::OnJobFailed(const TJobPtr& job, TJobStatus* status)
{
    if (job->GetState() == EJobState::Running ||
        job->GetState() == EJobState::Waiting ||
        job->GetState() == EJobState::None)
    {
        SetJobState(job, EJobState::Failed);
        job->SetStatus(status);

        OnJobFinished(job);

        ProcessFinishedJobResult(job);

        auto* operationState = FindOperationState(job->GetOperationId());
        if (operationState) {
            const auto& controller = operationState->Controller;
            controller->GetCancelableInvoker()->Invoke(BIND(
                &IOperationController::OnJobFailed,
                controller,
                Passed(std::make_unique<TFailedJobSummary>(job))));
        }
    }

    UnregisterJob(job);
}

void TNodeShard::OnJobAborted(const TJobPtr& job, TJobStatus* status, bool operationTerminated)
{
    // Only update the status for the first time.
    // Typically the scheduler decides to abort the job on its own.
    // In this case we should ignore the status returned from the node
    // and avoid notifying the controller twice.
    if (job->GetState() == EJobState::Running ||
        job->GetState() == EJobState::Waiting ||
        job->GetState() == EJobState::None)
    {
        job->SetStatus(status);
        // We should set status before to correctly consider AbortReason.
        SetJobState(job, EJobState::Aborted);

        OnJobFinished(job);

        // Check if job was aborted due to signal.
        if (GetAbortReason(job->Status().result()) == EAbortReason::UserRequest) {
            ProcessFinishedJobResult(job);
        }

        auto* operationState = FindOperationState(job->GetOperationId());
        if (operationState && !operationTerminated) {
            const auto& controller = operationState->Controller;
            controller->GetCancelableInvoker()->Invoke(BIND(
                &IOperationController::OnJobAborted,
                controller,
                Passed(std::make_unique<TAbortedJobSummary>(job))));
        }
    }

    UnregisterJob(job);
}

void TNodeShard::OnJobFinished(const TJobPtr& job)
{
    job->SetFinishTime(TInstant::Now());
    auto duration = job->GetDuration();

    TWriterGuard guard(JobTimeStatisticsDeltaLock_);

    switch (job->GetState()) {
        case EJobState::Completed:
            JobTimeStatisticsDelta_.CompletedJobTimeDelta += duration.MicroSeconds();
            break;
        case EJobState::Failed:
            JobTimeStatisticsDelta_.FailedJobTimeDelta += duration.MicroSeconds();
            break;
        case EJobState::Aborted:
            JobTimeStatisticsDelta_.AbortedJobTimeDelta += duration.MicroSeconds();
            break;
        default:
            Y_UNREACHABLE();
    }
}

void TNodeShard::SubmitUpdatedAndCompletedJobsToStrategy()
{
    if (!UpdatedJobs_.empty() || !CompletedJobs_.empty()) {
        Host_->GetStrategy()->ProcessUpdatedAndCompletedJobs(
            UpdatedJobs_,
            CompletedJobs_);
        UpdatedJobs_.clear();
        CompletedJobs_.clear();
    }
}

void TNodeShard::ProcessFinishedJobResult(const TJobPtr& job)
{
    auto jobFailedOrAborted = job->GetState() == EJobState::Failed || job->GetState() == EJobState::Aborted;
    const auto& schedulerResultExt = job->Status().result().GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);

    auto stderrChunkId = FromProto<TChunkId>(schedulerResultExt.stderr_chunk_id());
    auto failContextChunkId = FromProto<TChunkId>(schedulerResultExt.fail_context_chunk_id());

    TYsonString attributes;
    {
        TStringStream outputStream;
        TYsonWriter writer(&outputStream, EYsonFormat::Binary, EYsonType::MapFragment);
        BuildJobAttributes(job, &writer);
        attributes = TYsonString(outputStream.Str(), EYsonType::MapFragment);
    }

    auto* operationState = FindOperationState(job->GetOperationId());
    TFuture<TYsonString> inputPathsFuture;
    if (operationState) {
        // Keep it sync with TScheduler::ShouldCreateJobNode
        if ((jobFailedOrAborted && operationState->CanCreateJobNodeForAbortedOrFailedJobs) ||
            (stderrChunkId && operationState->CanCreateJobNodeForJobsWithStderr))
        {
            const auto& controller = operationState->Controller;
            inputPathsFuture = BIND(&IOperationController::BuildInputPathYson, controller)
                .AsyncVia(controller->GetCancelableInvoker())
                .Run(job->GetId());
        } else {
            inputPathsFuture = MakeFuture(TYsonString());
        }
    } else {
        inputPathsFuture = MakeFuture<TYsonString>(
            TError("No controller for operation %v", job->GetOperationId()));
    }

    auto asyncResult = Host_->UpdateOperationWithFinishedJob(
        job->GetOperationId(),
        job->GetId(),
        jobFailedOrAborted,
        attributes,
        stderrChunkId,
        failContextChunkId,
        inputPathsFuture);

    asyncResult.Subscribe(BIND([this, this_ = MakeStrong(this)] (const TError& error) {
        if (!error.IsOK()) {
            LOG_ERROR(error, "Failed to process finished job result");
        }
    }));
}

void TNodeShard::IncreaseProfilingCounter(const TJobPtr& job, i64 value)
{
    TWriterGuard guard(JobCounterLock_);

    TJobCounter* counter = &JobCounter_;
    if (job->GetState() == EJobState::Aborted) {
        counter = &AbortedJobCounter_[GetAbortReason(job->Status().result())];
    } else if (job->GetState() == EJobState::Completed) {
        counter = &CompletedJobCounter_[job->GetInterruptReason()];
    }
    (*counter)[job->GetState()][job->GetType()] += value;
}

void TNodeShard::SetJobState(const TJobPtr& job, EJobState state)
{
    IncreaseProfilingCounter(job, -1);
    job->SetState(state);
    IncreaseProfilingCounter(job, 1);
}

void TNodeShard::RegisterJob(const TJobPtr& job)
{
    auto& operationState = GetOperationState(job->GetOperationId());

    auto node = job->GetNode();

    YCHECK(operationState.Jobs.emplace(job->GetId(), job).second);
    YCHECK(node->Jobs().insert(job).second);
    YCHECK(node->IdToJob().insert(std::make_pair(job->GetId(), job)).second);
    ++ActiveJobCount_;

    LOG_DEBUG("Job registered (JobId: %v, JobType: %v, OperationId: %v)",
        job->GetId(),
        job->GetType(),
        job->GetOperationId());
}

void TNodeShard::UnregisterJob(const TJobPtr& job)
{
    auto node = job->GetNode();

    if (node->GetHasOngoingJobsScheduling()) {
        job->SetHasPendingUnregistration(true);
    } else {
        DoUnregisterJob(job);
    }
}

void TNodeShard::DoUnregisterJob(const TJobPtr& job)
{
    auto* operationState = FindOperationState(job->GetOperationId());
    auto node = job->GetNode();

    YCHECK(!node->GetHasOngoingJobsScheduling());

    YCHECK(node->Jobs().erase(job) == 1);
    YCHECK(node->IdToJob().erase(job->GetId()) == 1);
    --ActiveJobCount_;

    if (operationState) {
        YCHECK(operationState->Jobs.erase(job->GetId()) == 1);

        CompletedJobs_.emplace_back(job->GetOperationId(), job->GetId());

        LOG_DEBUG("Job unregistered (JobId: %v, OperationId: %v)",
            job->GetId(),
            job->GetOperationId());
    } else {
        LOG_DEBUG("Dangling job unregistered (JobId: %v, OperationId: %v)",
            job->GetId(),
            job->GetOperationId());
    }
}

void TNodeShard::PreemptJob(const TJobPtr& job, TNullable<TCpuDuration> interruptTimeout)
{
    LOG_DEBUG("Preempting job (JobId: %v, OperationId: %v, Interruptible: %v, Reason: %v)",
        job->GetId(),
        job->GetOperationId(),
        job->GetInterruptible(),
        job->GetPreemptionReason());

    job->SetPreempted(true);

    if (interruptTimeout) {
        DoInterruptJob(job, EInterruptReason::Preemption, *interruptTimeout);
    }
}

void TNodeShard::DoInterruptJob(
    const TJobPtr& job,
    EInterruptReason reason,
    TCpuDuration interruptTimeout,
    TNullable<TString> interruptUser)
{
    LOG_DEBUG("Interrupting job (Reason: %v, InterruptTimeout: %.3g, JobId: %v, OperationId: %v)",
        reason,
        CpuDurationToDuration(interruptTimeout).SecondsFloat(),
        job->GetId(),
        job->GetOperationId());

    if (job->GetInterruptReason() == EInterruptReason::None && reason != EInterruptReason::None) {
        job->SetInterruptReason(reason);
    }

    if (interruptTimeout != 0) {
        auto interruptDeadline = GetCpuInstant() + interruptTimeout;
        if (job->GetInterruptDeadline() == 0 || interruptDeadline < job->GetInterruptDeadline()) {
            job->SetInterruptDeadline(interruptDeadline);
        }
    }
}

void TNodeShard::InterruptJob(const TJobId& jobId, EInterruptReason reason)
{
    auto job = FindJob(jobId);
    if (job) {
        DoInterruptJob(job, reason);
    }
}

TExecNodePtr TNodeShard::GetNodeByJob(const TJobId& jobId)
{
    auto nodeId = NodeIdFromJobId(jobId);
    auto it = IdToNode_.find(nodeId);
    if (it == IdToNode_.end()) {
        return nullptr;
    }
    return it->second;
}

TJobPtr TNodeShard::FindJob(const TJobId& jobId, const TExecNodePtr& node)
{
    const auto& idToJob = node->IdToJob();
    auto it = idToJob.find(jobId);
    return it == idToJob.end() ? nullptr : it->second;
}

TJobPtr TNodeShard::FindJob(const TJobId& jobId)
{
    auto node = GetNodeByJob(jobId);
    if (!node) {
        return nullptr;
    }
    return FindJob(jobId, node);
}

TJobPtr TNodeShard::GetJobOrThrow(const TJobId& jobId)
{
    auto job = FindJob(jobId);
    if (!job) {
        THROW_ERROR_EXCEPTION(
            NScheduler::EErrorCode::NoSuchJob,
            "No such job %v",
            jobId);
    }
    return job;
}

TJobProberServiceProxy TNodeShard::CreateJobProberProxy(const TJobPtr& job)
{
    auto address = job->GetNode()->NodeDescriptor().GetAddress(Bootstrap_->GetLocalNetworks());
    return Host_->CreateJobProberProxy(address);
}

bool TNodeShard::OperationExists(const TOperationId& operationId) const
{
    return OperationStates_.find(operationId) != OperationStates_.end();
}

TNodeShard::TOperationState* TNodeShard::FindOperationState(const TOperationId& operationId)
{
    auto it = OperationStates_.find(operationId);
    return it != OperationStates_.end() ? &it->second : nullptr;
}

TNodeShard::TOperationState& TNodeShard::GetOperationState(const TOperationId& operationId)
{
    auto it = OperationStates_.find(operationId);
    YCHECK(it != OperationStates_.end());
    return it->second;
}

void TNodeShard::BuildNodeYson(TExecNodePtr node, IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item(node->GetDefaultAddress()).BeginMap()
            .Do([=] (TFluentMap fluent) {
                BuildExecNodeAttributes(node, fluent);
            })
        .EndMap();
}

void TNodeShard::BuildSuspiciousJobYson(const TJobPtr& job, IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item(ToString(job->GetId())).BeginMap()
            .Item("operation_id").Value(ToString(job->GetOperationId()))
            .Item("type").Value(FormatEnum(job->GetType()))
            .Item("brief_statistics").Value(job->GetBriefStatistics())
            .Item("node").Value(job->GetNode()->GetDefaultAddress())
            .Item("last_activity_time").Value(job->GetLastActivityTime())
        .EndMap();
}

////////////////////////////////////////////////////////////////////////////////

//! Proxy object to control job outside of node shard.
class TJobHost
    : public IJobHost
{
public:
    TJobHost(const TJobId& jobId, const TNodeShardPtr& nodeShard)
        : JobId_(jobId)
        , NodeShard_(nodeShard)
    { }

    virtual TFuture<void> InterruptJob(EInterruptReason reason) override
    {
        return BIND(&TNodeShard::InterruptJob, NodeShard_, JobId_, reason)
            .AsyncVia(NodeShard_->GetInvoker())
            .Run();
    }

    virtual TFuture<void> AbortJob(const TError& error) override
    {
        // A neat way to choose the proper overload.
        typedef void (TNodeShard::*CorrectSignature)(const TJobId&, const TError&);
        return BIND(static_cast<CorrectSignature>(&TNodeShard::AbortJob), NodeShard_, JobId_, error)
            .AsyncVia(NodeShard_->GetInvoker())
            .Run();
    }

private:
    TJobId JobId_;
    TNodeShardPtr NodeShard_;
};

DEFINE_REFCOUNTED_TYPE(TJobHost)

////////////////////////////////////////////////////////////////////////////////

IJobHostPtr CreateJobHost(const TJobId& jobId, const TNodeShardPtr& nodeShard)
{
    return New<TJobHost>(jobId, nodeShard);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
