#include "controller_agent.h"
#include "operation_controller.h"
#include "master_connector.h"
#include "config.h"
#include "private.h"
#include "operation_controller_host.h"
#include "operation.h"

#include <yt/server/cell_scheduler/bootstrap.h>

#include <yt/server/scheduler/cache.h>
#include <yt/server/scheduler/operation.h>
#include <yt/server/scheduler/message_queue.h>

#include <yt/ytlib/api/transaction.h>
#include <yt/ytlib/api/native_connection.h>

#include <yt/ytlib/chunk_client/throttler_manager.h>

#include <yt/ytlib/object_client/helpers.h>

#include <yt/ytlib/event_log/event_log.h>

#include <yt/ytlib/scheduler/controller_agent_tracker_service_proxy.h>

#include <yt/core/concurrency/async_semaphore.h>
#include <yt/core/concurrency/thread_affinity.h>
#include <yt/core/concurrency/thread_pool.h>
#include <yt/core/concurrency/throughput_throttler.h>

#include <yt/core/actions/cancelable_context.h>

#include <yt/core/ytree/convert.h>

#include <util/string/join.h>

namespace NYT {
namespace NControllerAgent {

using namespace NScheduler;
using namespace NCellScheduler;
using namespace NConcurrency;
using namespace NYTree;
using namespace NChunkClient;
using namespace NNodeTrackerClient;
using namespace NEventLog;
using namespace NProfiling;
using namespace NYson;
using namespace NRpc;

////////////////////////////////////////////////////////////////////

static const auto& Logger = ControllerAgentLogger;

////////////////////////////////////////////////////////////////////

class TControllerAgent::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TControllerAgentConfigPtr config,
        NCellScheduler::TBootstrap* bootstrap)
        : Config_(config)
        , Bootstrap_(bootstrap)
        , ControllerThreadPool_(New<TThreadPool>(Config_->ControllerThreadCount, "Controller"))
        , SnapshotIOQueue_(New<TActionQueue>("SnapshotIO"))
        , ChunkLocationThrottlerManager_(New<TThrottlerManager>(
            Config_->ChunkLocationThrottler,
            ControllerAgentLogger))
        , ReconfigurableJobSpecSliceThrottler_(CreateReconfigurableThroughputThrottler(
            Config_->JobSpecSliceThrottler,
            NLogging::TLogger(),
            NProfiling::TProfiler(ControllerAgentProfiler.GetPathPrefix() + "/job_spec_slice_throttler")))
        , JobSpecSliceThrottler_(ReconfigurableJobSpecSliceThrottler_)
        , CoreSemaphore_(New<TAsyncSemaphore>(Config_->MaxConcurrentSafeCoreDumps))
        , EventLogWriter_(New<TEventLogWriter>(
            Config_->EventLog,
            Bootstrap_->GetMasterClient(),
            Bootstrap_->GetControlInvoker(EControlQueue::PeriodicActivity)))
        , MasterConnector_(std::make_unique<TMasterConnector>(
            Config_,
            Bootstrap_))
        , SchedulerProxy_(Bootstrap_->GetLocalRpcChannel())
    {
        SchedulerProxy_.SetDefaultTimeout(Config_->ControllerAgentHeartbeatRpcTimeout);

        MasterConnector_->SubscribeMasterConnecting(BIND(
            &TImpl::OnMasterConnecting,
            Unretained(this)));
        MasterConnector_->SubscribeMasterConnected(BIND(
            &TImpl::OnMasterConnected,
            Unretained(this)));
        MasterConnector_->SubscribeMasterDisconnected(BIND(
            &TImpl::OnMasterDisconnected,
            Unretained(this)));
    }

    void ValidateConnected()
    {
        VERIFY_THREAD_AFFINITY_ANY();

        if (!MasterConnector_->IsConnected()) {
            THROW_ERROR_EXCEPTION(GetMasterDisconnectedError());
        }
    }

    TInstant GetConnectionTime() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return MasterConnector_->GetConnectionTime();
    }

    const IInvokerPtr& GetCancelableInvoker()
    {
        return CancelableInvoker_;
    }

    const IInvokerPtr& GetControllerThreadPoolInvoker()
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return ControllerThreadPool_->GetInvoker();
    }

    const IInvokerPtr& GetSnapshotIOInvoker()
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return SnapshotIOQueue_->GetInvoker();
    }

    TMasterConnector* GetMasterConnector()
    {
        return MasterConnector_.get();
    }

    const TControllerAgentConfigPtr& GetConfig() const
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return Config_;
    }

    const NApi::INativeClientPtr& GetClient() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Bootstrap_->GetMasterClient();
    }

    const TNodeDirectoryPtr& GetNodeDirectory()
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Bootstrap_->GetNodeDirectory();
    }

    const TThrottlerManagerPtr& GetChunkLocationThrottlerManager() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return ChunkLocationThrottlerManager_;
    }

    const TCoreDumperPtr& GetCoreDumper() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Bootstrap_->GetCoreDumper();
    }

    const TAsyncSemaphorePtr& GetCoreSemaphore() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return CoreSemaphore_;
    }

    const TEventLogWriterPtr& GetEventLogWriter() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return EventLogWriter_;
    }

    void UpdateConfig(const TControllerAgentConfigPtr& config)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        Config_ = config;

        ChunkLocationThrottlerManager_->Reconfigure(Config_->ChunkLocationThrottler);

        EventLogWriter_->UpdateConfig(Config_->EventLog);

        SchedulerProxy_.SetDefaultTimeout(Config_->ControllerAgentHeartbeatRpcTimeout);

        ReconfigurableJobSpecSliceThrottler_->Reconfigure(Config_->JobSpecSliceThrottler);

        if (HeartbeatExecutor_) {
            HeartbeatExecutor_->SetPeriod(Config_->ControllerAgentHeartbeatPeriod);
        }

        if (MasterConnector_) {
            MasterConnector_->UpdateConfig(config);
        }

        for (const auto& pair : GetOperations()) {
            const auto& operation = pair.second;
            auto controller = operation->GetController();
            controller->GetCancelableInvoker()->Invoke(
                BIND(&IOperationController::UpdateConfig, controller, config));
        }
    }


    TOperationPtr CreateOperation(const NScheduler::TOperationPtr& operation)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto agentOperation = New<NControllerAgent::TOperation>(operation.Get());
        auto host = New<TOperationControllerHost>(
            agentOperation.Get(),
            CancelableInvoker_,
            OperationEventsOutbox_,
            JobEventsOutbox_,
            Bootstrap_);
        agentOperation->SetHost(host);
        return agentOperation;
    }

    void RegisterOperation(const TOperationId& operationId, const TOperationPtr& operation)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        YCHECK(IdToOperation_.emplace(operationId, operation).second);
    }

    void UnregisterOperation(const TOperationId& operationId)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        YCHECK(IdToOperation_.erase(operationId) == 1);
    }

    TOperationPtr FindOperation(const TOperationId& operationId)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto it = IdToOperation_.find(operationId);
        return it == IdToOperation_.end() ? nullptr : it->second;
    }

    TOperationPtr GetOperationOrThrow(const TOperationId& operationId)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto operation = FindOperation(operationId);
        if (!operation) {
            THROW_ERROR_EXCEPTION("No such operation %v", operationId);
        }
        return operation;
    }

    TOperationIdToOperationMap GetOperations()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return IdToOperation_;
    }


    TFuture<std::vector<TErrorOr<TSharedRef>>> ExtractJobSpecs(const std::vector<TJobSpecRequest>& requests)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        std::vector<TFuture<TSharedRef>> asyncJobSpecs;
        for (const auto& request : requests) {
            LOG_DEBUG("Retrieving job spec (OperationId: %v, JobId: %v)",
                request.OperationId,
                request.JobId);

            auto operation = FindOperation(request.OperationId);
            if (!operation) {
                asyncJobSpecs.push_back(MakeFuture<TSharedRef>(TError("No such operation %v",
                    request.OperationId)));
                continue;
            }

            auto controller = operation->GetController();
            auto asyncJobSpec = BIND(&IOperationController::ExtractJobSpec,
                controller,
                request.JobId)
                .AsyncVia(controller->GetCancelableInvoker())
                .Run();

            asyncJobSpecs.push_back(asyncJobSpec);
        }

        return CombineAll(asyncJobSpecs);
    }

    TFuture<TOperationInfo> BuildOperationInfo(const TOperationId& operationId)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto controller = GetOperationOrThrow(operationId)->GetController();
        return BIND(&IOperationController::BuildOperationInfo, controller)
            .AsyncVia(controller->GetCancelableInvoker())
            .Run();
    }

    TFuture<TYsonString> BuildJobInfo(
        const TOperationId& operationId,
        const TJobId& jobId)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto controller = GetOperationOrThrow(operationId)->GetController();
        return BIND(&IOperationController::BuildJobYson, controller)
            .AsyncVia(controller->GetCancelableInvoker())
            .Run(jobId, /* outputStatistics */ true);
    }

    TExecNodeDescriptorListPtr GetExecNodeDescriptors(const TSchedulingTagFilter& filter) const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        if (filter.IsEmpty()) {
            TReaderGuard guard(ExecNodeDescriptorsLock_);

            return CachedExecNodeDescriptors_;
        }

        return CachedExecNodeDescriptorsByTags_->Get(filter);
    }

    int GetExecNodeCount() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        TReaderGuard guard(ExecNodeDescriptorsLock_);
        return static_cast<int>(CachedExecNodeDescriptors_->Descriptors.size());
    }

    const IThroughputThrottlerPtr& GetJobSpecSliceThrottler() const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return JobSpecSliceThrottler_;
    }

private:
    TControllerAgentConfigPtr Config_;
    NCellScheduler::TBootstrap* const Bootstrap_;

    const TThreadPoolPtr ControllerThreadPool_;
    const TActionQueuePtr SnapshotIOQueue_;
    const TThrottlerManagerPtr ChunkLocationThrottlerManager_;
    const IReconfigurableThroughputThrottlerPtr ReconfigurableJobSpecSliceThrottler_;
    const IThroughputThrottlerPtr JobSpecSliceThrottler_;
    const TAsyncSemaphorePtr CoreSemaphore_;
    const TEventLogWriterPtr EventLogWriter_;
    const std::unique_ptr<TMasterConnector> MasterConnector_;

    TCancelableContextPtr CancelableContext_;
    IInvokerPtr CancelableInvoker_;

    TOperationIdToOperationMap IdToOperation_;

    TReaderWriterSpinLock ExecNodeDescriptorsLock_;
    TExecNodeDescriptorListPtr CachedExecNodeDescriptors_ = New<TExecNodeDescriptorList>();
    TIntrusivePtr<TExpiringCache<TSchedulingTagFilter, TExecNodeDescriptorListPtr>> CachedExecNodeDescriptorsByTags_;

    TControllerAgentTrackerServiceProxy SchedulerProxy_;

    TCpuInstant LastExecNodesUpdateTime_ = TCpuInstant();

    TIncarnationId IncarnationId_;
    TIntrusivePtr<NScheduler::TMessageQueueOutbox<TOperationEvent>> OperationEventsOutbox_;
    TIntrusivePtr<NScheduler::TMessageQueueOutbox<TJobEvent>> JobEventsOutbox_;

    TPeriodicExecutorPtr HeartbeatExecutor_;

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);


    void OnMasterConnecting()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        // NB: We cannot be sure the previous incarnation did a proper cleanup due to possible
        // fiber cancelation.
        DoCleanup();

        CancelableContext_ = New<TCancelableContext>();
        // TODO(babenko): better queue
        CancelableInvoker_ = CancelableContext_->CreateInvoker(Bootstrap_->GetControlInvoker(EControlQueue::Default));

        IncarnationId_ = MasterConnector_->GetIncarnationId();
        OperationEventsOutbox_ = New<NScheduler::TMessageQueueOutbox<TOperationEvent>>(
            NLogging::TLogger(ControllerAgentLogger)
                .AddTag("Kind: OperationEvents, IncarnationId: %v", IncarnationId_));
        JobEventsOutbox_ = New<NScheduler::TMessageQueueOutbox<TJobEvent>>(
            NLogging::TLogger(ControllerAgentLogger)
                .AddTag("Kind: JobEvents, IncarnationId: %v", IncarnationId_));
    }

    void OnMasterConnected()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        CachedExecNodeDescriptorsByTags_ = New<TExpiringCache<TSchedulingTagFilter, TExecNodeDescriptorListPtr>>(
            BIND(&TImpl::CalculateExecNodeDescriptors, MakeStrong(this)),
            Config_->SchedulingTagFilterExpireTimeout,
            GetCancelableInvoker());
        CachedExecNodeDescriptorsByTags_->Start();

        HeartbeatExecutor_ = New<TPeriodicExecutor>(
            CancelableInvoker_,
            BIND(&TControllerAgent::TImpl::SendHeartbeat, MakeWeak(this)),
            Config_->ControllerAgentHeartbeatPeriod);
        HeartbeatExecutor_->Start();
    }

    void DoCleanup()
    {
        for (const auto& pair : IdToOperation_) {
            const auto& operation = pair.second;
            auto controller = operation->GetController();
            controller->Cancel();
        }
        IdToOperation_.clear();

        if (CancelableContext_) {
            CancelableContext_->Cancel();
            CancelableContext_.Reset();
        }

        if (CachedExecNodeDescriptorsByTags_) {
            CachedExecNodeDescriptorsByTags_->Stop();
            CachedExecNodeDescriptorsByTags_.Reset();
        }

        if (HeartbeatExecutor_) {
            HeartbeatExecutor_->Stop();
            HeartbeatExecutor_.Reset();
        }

        IncarnationId_ = {};
        OperationEventsOutbox_.Reset();
        JobEventsOutbox_.Reset();
    }

    void OnMasterDisconnected()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        DoCleanup();
    }

    // TODO: Move this method to some common place to avoid copy/paste.
    TError GetMasterDisconnectedError()
    {
        return TError(
            NRpc::EErrorCode::Unavailable,
            "Master is not connected");
    }

    std::unique_ptr<NScheduler::NProto::TReqHeartbeat> PrepareHeartbeatRequest(TCpuInstant now)
    {
        auto req = std::make_unique<NScheduler::NProto::TReqHeartbeat>();
        ToProto(req->mutable_agent_incarnation_id(), IncarnationId_);

        OperationEventsOutbox_->BuildOutcoming(
            req->mutable_agent_to_scheduler_operation_events_queue(),
            [&] (auto* protoEvent, const auto& event) {
                protoEvent->set_event_type(static_cast<int>(event.Type));
                ToProto(protoEvent->mutable_operation_id(), event.OperationId);
                if (!event.Error.IsOK()) {
                    ToProto(protoEvent->mutable_error(), event.Error);
                }
            });

        JobEventsOutbox_->BuildOutcoming(
            req->mutable_agent_to_scheduler_job_events_queue(),
            [&] (auto* protoEvent, const auto& event) {
                protoEvent->set_event_type(static_cast<int>(event.Type));
                ToProto(protoEvent->mutable_job_id(), event.JobId);
                protoEvent->set_interrupt_reason(static_cast<int>(event.InterruptReason));
                if (!event.Error.IsOK()) {
                    ToProto(protoEvent->mutable_error(), event.Error);
                }
            });

        auto operations = GetOperations();
        for (const auto& pair : operations) {
            const auto& operationId = pair.first;
            const auto& operation = pair.second;
            auto controller = operation->GetController();

            {
                auto jobMetricsDelta = controller->PullJobMetricsDelta();
                ToProto(req->add_job_metrics(), jobMetricsDelta);
            }

            {
                auto* operationAlertsProto = req->add_operation_alerts();
                ToProto(operationAlertsProto->mutable_operation_id(), operationId);
                for (const auto& pair : controller->GetAlerts()) {
                    auto alertType = pair.first;
                    const auto& alert = pair.second;
                    auto* protoAlert = operationAlertsProto->add_alerts();
                    protoAlert->set_type(static_cast<int>(alertType));
                    ToProto(protoAlert->mutable_error(), alert);
                }
            }
        }

        bool shouldRequestExecNodes = LastExecNodesUpdateTime_ + DurationToCpuDuration(Config_->ExecNodesRequestPeriod) < now;
        req->set_exec_nodes_requested(shouldRequestExecNodes);

        // TODO(ignat): add some backoff.
        {
            std::vector<TString> suspiciousJobsYsons;
            for (const auto& pair : operations) {
                const auto& operation = pair.second;
                auto controller = operation->GetController();
                suspiciousJobsYsons.push_back(controller->GetSuspiciousJobsYson().GetData());
            }
            req->set_suspicious_jobs(JoinSeq("", suspiciousJobsYsons));
        }

        return req;
    }

    void SendHeartbeat()
    {
        auto now = GetCpuInstant();
        auto preparedRequest = PrepareHeartbeatRequest(now);

        TControllerAgentTrackerServiceProxy::TRspHeartbeatPtr rsp;
        while (true) {
            LOG_INFO("Sending heartbeat");

            auto req = SchedulerProxy_.Heartbeat();
            req->CopyFrom(*preparedRequest);

            auto rspOrError = WaitFor(req->Invoke());
            if (rspOrError.IsOK()) {
                rsp = rspOrError.Value();
                break;
            }

            LOG_WARNING(rspOrError, "Heartbeat failed, retrying");
            Y_UNUSED(WaitFor(TDelayedExecutor::MakeDelayed(Config_->ControllerAgentHeartbeatFailureBackoff)));
        }

        LOG_INFO("Heartbeat succeeded");

        OperationEventsOutbox_->HandleStatus(rsp->agent_to_scheduler_operation_events_queue());
        JobEventsOutbox_->HandleStatus(rsp->agent_to_scheduler_job_events_queue());

        if (rsp->has_exec_nodes()) {
            auto execNodeDescriptors = New<TExecNodeDescriptorList>();
            FromProto(&execNodeDescriptors->Descriptors, rsp->exec_nodes().exec_nodes());

            {
                TWriterGuard guard(ExecNodeDescriptorsLock_);
                std::swap(CachedExecNodeDescriptors_, execNodeDescriptors);
            }

            LastExecNodesUpdateTime_ = now;
        }
    }

    // TODO(ignat): eliminate this copy/paste from scheduler.cpp somehow.
    TExecNodeDescriptorListPtr CalculateExecNodeDescriptors(const TSchedulingTagFilter& filter) const
    {
        VERIFY_THREAD_AFFINITY_ANY();

        TReaderGuard guard(ExecNodeDescriptorsLock_);

        auto result = New<TExecNodeDescriptorList>();
        for (const auto& descriptor : CachedExecNodeDescriptors_->Descriptors) {
            if (filter.CanSchedule(descriptor.Tags)) {
                result->Descriptors.push_back(descriptor);
            }
        }
        return result;
    }
};

////////////////////////////////////////////////////////////////////

TControllerAgent::TControllerAgent(
    TControllerAgentConfigPtr config,
    NCellScheduler::TBootstrap* bootstrap)
    : Impl_(New<TImpl>(config, bootstrap))
{ }

TControllerAgent::~TControllerAgent() = default;

const IInvokerPtr& TControllerAgent::GetControllerThreadPoolInvoker()
{
    return Impl_->GetControllerThreadPoolInvoker();
}

const IInvokerPtr& TControllerAgent::GetSnapshotIOInvoker()
{
    return Impl_->GetSnapshotIOInvoker();
}

TMasterConnector* TControllerAgent::GetMasterConnector()
{
    return Impl_->GetMasterConnector();
}

void TControllerAgent::ValidateConnected() const
{
    Impl_->ValidateConnected();
}

TInstant TControllerAgent::GetConnectionTime() const
{
    return Impl_->GetConnectionTime();
}

const TControllerAgentConfigPtr& TControllerAgent::GetConfig() const
{
    return Impl_->GetConfig();
}

const NApi::INativeClientPtr& TControllerAgent::GetClient() const
{
    return Impl_->GetClient();
}

const TNodeDirectoryPtr& TControllerAgent::GetNodeDirectory()
{
    return Impl_->GetNodeDirectory();
}

const TThrottlerManagerPtr& TControllerAgent::GetChunkLocationThrottlerManager() const
{
    return Impl_->GetChunkLocationThrottlerManager();
}

const TCoreDumperPtr& TControllerAgent::GetCoreDumper() const
{
    return Impl_->GetCoreDumper();
}

const TAsyncSemaphorePtr& TControllerAgent::GetCoreSemaphore() const
{
    return Impl_->GetCoreSemaphore();
}

const TEventLogWriterPtr& TControllerAgent::GetEventLogWriter() const
{
    return Impl_->GetEventLogWriter();
}

void TControllerAgent::UpdateConfig(const TControllerAgentConfigPtr& config)
{
    Impl_->UpdateConfig(config);
}

TOperationPtr TControllerAgent::CreateOperation(const NScheduler::TOperationPtr& operation)
{
    return Impl_->CreateOperation(operation);
}

void TControllerAgent::RegisterOperation(const TOperationId& operationId, const TOperationPtr& operation)
{
    Impl_->RegisterOperation(operationId, operation);
}

void TControllerAgent::UnregisterOperation(const TOperationId& operationId)
{
    Impl_->UnregisterOperation(operationId);
}

TOperationPtr TControllerAgent::FindOperation(const TOperationId& operationId)
{
    return Impl_->FindOperation(operationId);
}

TOperationIdToOperationMap TControllerAgent::GetOperations()
{
    return Impl_->GetOperations();
}

TFuture<std::vector<TErrorOr<TSharedRef>>> TControllerAgent::ExtractJobSpecs(
    const std::vector<TJobSpecRequest>& requests)
{
    return Impl_->ExtractJobSpecs(requests);
}

TFuture<TOperationInfo> TControllerAgent::BuildOperationInfo(const TOperationId& operationId)
{
    return Impl_->BuildOperationInfo(operationId);
}

TFuture<TYsonString> TControllerAgent::BuildJobInfo(
    const TOperationId& operationId,
    const TJobId& jobId)
{
    return Impl_->BuildJobInfo(operationId, jobId);
}

int TControllerAgent::GetExecNodeCount() const
{
    return Impl_->GetExecNodeCount();
}

TExecNodeDescriptorListPtr TControllerAgent::GetExecNodeDescriptors(const TSchedulingTagFilter& filter) const
{
    return Impl_->GetExecNodeDescriptors(filter);
}

const IThroughputThrottlerPtr& TControllerAgent::GetJobSpecSliceThrottler() const
{
    return Impl_->GetJobSpecSliceThrottler();
}

////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT
