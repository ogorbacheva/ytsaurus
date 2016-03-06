#include "job.h"
#include "private.h"
#include "config.h"
#include "environment.h"
#include "environment_manager.h"
#include "slot.h"
#include "slot_manager.h"

#include <yt/server/cell_node/bootstrap.h>
#include <yt/server/cell_node/config.h>

#include <yt/server/data_node/artifact.h>
#include <yt/server/data_node/chunk_block_manager.h>
#include <yt/server/data_node/chunk.h>
#include <yt/server/data_node/chunk_cache.h>
#include <yt/server/data_node/master_connector.h>

#include <yt/server/job_agent/job.h>

#include <yt/server/scheduler/config.h>

#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>

#include <yt/ytlib/file_client/config.h>
#include <yt/ytlib/file_client/file_chunk_reader.h>
#include <yt/ytlib/file_client/file_ypath_proxy.h>

#include <yt/ytlib/job_prober_client/job_prober_service_proxy.h>

#include <yt/ytlib/security_client/public.h>

#include <yt/ytlib/table_client/helpers.h>
#include <yt/ytlib/table_client/name_table.h>
#include <yt/ytlib/table_client/schemaless_chunk_reader.h>
#include <yt/ytlib/table_client/schemaless_writer.h>

#include <yt/core/actions/cancelable_context.h>

#include <yt/core/bus/tcp_client.h>

#include <yt/core/concurrency/async_stream.h>

#include <yt/core/logging/log_manager.h>

#include <yt/core/misc/proc.h>

#include <yt/core/rpc/bus_channel.h>

namespace NYT {
namespace NExecAgent {

using namespace NRpc;
using namespace NJobProxy;
using namespace NYTree;
using namespace NYson;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NTableClient;
using namespace NFileClient;
using namespace NCellNode;
using namespace NDataNode;
using namespace NCellNode;
using namespace NNodeTrackerClient;
using namespace NNodeTrackerClient::NProto;
using namespace NJobTrackerClient;
using namespace NJobTrackerClient::NProto;
using namespace NScheduler;
using namespace NScheduler::NProto;
using namespace NConcurrency;
using namespace NApi;

using NNodeTrackerClient::TNodeDirectory;
using NScheduler::NProto::TUserJobSpec;

////////////////////////////////////////////////////////////////////////////////

class TJob
    : public NJobAgent::IJob
{
public:
    DEFINE_SIGNAL(void(const TNodeResources&), ResourcesUpdated);

public:
    TJob(
        const TJobId& jobId,
        const TOperationId& operationId,
        const TNodeResources& resourceUsage,
        TJobSpec&& jobSpec,
        TBootstrap* bootstrap)
        : Id_(jobId)
        , OperationId_(operationId)
        , Bootstrap_(bootstrap)
        , ResourceUsage(resourceUsage)
    {
        JobSpec.Swap(&jobSpec);

        const auto& schedulerJobSpecExt = JobSpec.GetExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
        if (schedulerJobSpecExt.has_aux_node_directory()) {
            AuxNodeDirectory_->MergeFrom(schedulerJobSpecExt.aux_node_directory());
        }

        Logger.AddTag("JobId: %v, OperationId: %v, JobType: %v",
            Id_,
            OperationId_,
            GetType());
    }

    virtual void Start() override
    {
        // No SpinLock here, because concurrent access is impossible before
        // calling Start.
        YCHECK(JobState_ == EJobState::Waiting);
        JobState_ = EJobState::Running;

        PrepareTime_ = TInstant::Now();
        auto slotManager = Bootstrap_->GetExecSlotManager();
        Slot_ = slotManager->AcquireSlot();

        auto invoker = CancelableContext_->CreateInvoker(Slot_->GetInvoker());
        BIND(&TJob::DoRun, MakeWeak(this))
            .Via(invoker)
            .Run();
    }

    virtual void Abort(const TError& error) override
    {
        if (GetState() == EJobState::Waiting) {
            // Abort before the start.
            YCHECK(!JobResult_.HasValue());
            DoSetResult(error);
            SetFinalState();
            return;
        }

        {
            TGuard<TSpinLock> guard(SpinLock);
            if (JobState_ != EJobState::Running) {
                return;
            }
            DoSetResult(error);
            JobState_ = EJobState::Aborting;
        }

        CancelableContext_->Cancel();
        YCHECK(Slot_);
        BIND(&TJob::DoAbort, MakeStrong(this))
            .Via(Slot_->GetInvoker())
            .Run();
    }

    virtual const TJobId& GetId() const override
    {
        return Id_;
    }

    virtual const TJobId& GetOperationId() const override
    {
        return OperationId_;
    }

    virtual EJobType GetType() const override
    {
        return EJobType(JobSpec.type());
    }

    virtual const TJobSpec& GetSpec() const override
    {
        return JobSpec;
    }

    virtual EJobState GetState() const override
    {
        TGuard<TSpinLock> guard(SpinLock);
        return JobState_;
    }

    virtual EJobPhase GetPhase() const override
    {
        return JobPhase_;
    }

    virtual TNodeResources GetResourceUsage() const override
    {
        TGuard<TSpinLock> guard(SpinLock);
        return ResourceUsage;
    }

    virtual void SetResourceUsage(const TNodeResources& newUsage) override
    {
        TNodeResources delta = newUsage;
        {
            TGuard<TSpinLock> guard(SpinLock);
            if (JobState_ != EJobState::Running) {
                return;
            }
            delta -= ResourceUsage;
            ResourceUsage = newUsage;
        }
        ResourcesUpdated_.Fire(delta);
    }

    virtual TJobResult GetResult() const override
    {
        TGuard<TSpinLock> guard(SpinLock);
        return JobResult_.Get();
    }

    virtual void SetResult(const TJobResult& jobResult) override
    {
        TGuard<TSpinLock> guard(SpinLock);
        if (JobState_ != EJobState::Running) {
            return;
        }
        DoSetResult(jobResult);
    }

    virtual double GetProgress() const override
    {
        TGuard<TSpinLock> guard(SpinLock);
        return Progress_;
    }

    virtual void SetProgress(double value) override
    {
        TGuard<TSpinLock> guard(SpinLock);
        if (JobState_ == EJobState::Running) {
            Progress_ = value;
        }
    }

    NJobProberClient::TJobProberServiceProxy CreateJobProberProxy() const
    {
        auto jobProberClient = CreateTcpBusClient(Slot_->GetRpcClientConfig());
        auto jobProberChannel = CreateBusChannel(jobProberClient);

        NJobProberClient::TJobProberServiceProxy jobProberProxy(jobProberChannel);
        jobProberProxy.SetDefaultTimeout(Bootstrap_->GetConfig()->ExecAgent->JobProberRpcTimeout);
        return jobProberProxy;
    }

    virtual void SetStatistics(const NJobTrackerClient::NProto::TStatistics& statistics) override
    {
        TGuard<TSpinLock> guard(SpinLock);
        if (JobState_ == EJobState::Running) {
            Statistics_ = statistics;
        }
    }

    virtual std::vector<TChunkId> DumpInputContexts() const override
    {
        auto jobProberProxy = CreateJobProberProxy();
        auto req = jobProberProxy.DumpInputContext();

        ToProto(req->mutable_job_id(), Id_);
        auto rspOrError = WaitFor(req->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error requesting input contexts dump from job proxy");
        const auto& rsp = rspOrError.Value();

        return FromProto<std::vector<TChunkId>>(rsp->chunk_ids());
    }

    virtual TYsonString Strace() const override
    {
        auto jobProberProxy = CreateJobProberProxy();
        auto req = jobProberProxy.Strace();

        ToProto(req->mutable_job_id(), Id_);
        auto rspOrError = WaitFor(req->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error requesting strace dump from job proxy");
        const auto& rsp = rspOrError.Value();

        return TYsonString(rsp->trace());
    }

    virtual void SignalJob(const Stroka& signalName) override
    {
        Signaled_ = true;

        auto jobProberProxy = CreateJobProberProxy();
        auto req = jobProberProxy.SignalJob();

        ToProto(req->mutable_job_id(), Id_);
        ToProto(req->mutable_signal_name(), signalName);
        auto rspOrError = WaitFor(req->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error sending signal to job proxy");
    }

private:
    const TJobId Id_;
    const TOperationId OperationId_;
    NCellNode::TBootstrap* const Bootstrap_;

    TJobSpec JobSpec;

    TSpinLock SpinLock;
    TNodeResources ResourceUsage;

    EJobState JobState_ = EJobState::Waiting;
    EJobPhase JobPhase_ = EJobPhase::Created;

    const TCancelableContextPtr CancelableContext_ = New<TCancelableContext>();

    double Progress_ = 0.0;
    NJobTrackerClient::NProto::TStatistics Statistics_;
    bool Signaled_ = false;

    TNullable<TJobResult> JobResult_;

    TNullable<TInstant> PrepareTime_;
    TNullable<TInstant> ExecTime_;
    TSlotPtr Slot_;

    IProxyControllerPtr ProxyController_;

    EJobState FinalJobState_ = EJobState::Completed;

    std::vector<NDataNode::IChunkPtr> CachedChunks_;

    TNodeDirectoryPtr AuxNodeDirectory_ = New<TNodeDirectory>();

    NLogging::TLogger Logger = ExecAgentLogger;


    void DoRun()
    {
        try {
            YCHECK(JobPhase_ == EJobPhase::Created);
            JobPhase_ = EJobPhase::PreparingConfig;
            PrepareConfig();

            YCHECK(JobPhase_ == EJobPhase::PreparingConfig);
            JobPhase_ = EJobPhase::PreparingProxy;
            PrepareProxy();

            YCHECK(JobPhase_ == EJobPhase::PreparingProxy);
            JobPhase_ = EJobPhase::PreparingSandbox;
            Slot_->InitSandbox();

            YCHECK(JobPhase_ == EJobPhase::PreparingSandbox);
            JobPhase_ = EJobPhase::PreparingTmpfs;
            PrepareTmpfs();

            YCHECK(JobPhase_ == EJobPhase::PreparingTmpfs);
            JobPhase_ = EJobPhase::PreparingFiles;
            PrepareUserFiles();

            YCHECK(JobPhase_ == EJobPhase::PreparingFiles);
            JobPhase_ = EJobPhase::Running;

            {
                TGuard<TSpinLock> guard(SpinLock);
                ExecTime_ = TInstant::Now();
            }

            RunJobProxy();
        } catch (const std::exception& ex) {
            {
                TGuard<TSpinLock> guard(SpinLock);
                if (JobState_ != EJobState::Running) {
                    YCHECK(JobState_ == EJobState::Aborting);
                    return;
                }
                DoSetResult(ex);
                JobState_ = EJobState::Aborting;
            }
            BIND(&TJob::DoAbort, MakeStrong(this))
                .Via(Slot_->GetInvoker())
                .Run();
        }
    }

    // Must be called with set SpinLock.
    void DoSetResult(const TError& error)
    {
        TJobResult jobResult;
        ToProto(jobResult.mutable_error(), error);
        *jobResult.mutable_statistics() = Statistics_;
        DoSetResult(jobResult);
    }

    // Must be called with set SpinLock.
    void DoSetResult(const TJobResult& jobResult)
    {
        if (JobResult_) {
            auto error = FromProto<TError>(JobResult_->error());
            if (!error.IsOK()) {
                return;
            }
        }

        JobResult_ = jobResult;
        if (ExecTime_) {
            JobResult_->set_exec_time(ToProto(TInstant::Now() - *ExecTime_));
            JobResult_->set_prepare_time(ToProto(*ExecTime_ - *PrepareTime_));
        } else if (PrepareTime_) {
            JobResult_->set_prepare_time(ToProto(TInstant::Now() - *PrepareTime_));
        }

        auto error = FromProto<TError>(jobResult.error());

        if (error.IsOK()) {
            return;
        }

        if (IsFatalError(error)) {
            error.Attributes().Set("fatal", IsFatalError(error));
            ToProto(JobResult_->mutable_error(), error);
            FinalJobState_ = EJobState::Failed;
            return;
        }

        auto abortReason = GetAbortReason(jobResult, Signaled_);
        if (abortReason) {
            error.Attributes().Set("abort_reason", abortReason);
            ToProto(JobResult_->mutable_error(), error);
            FinalJobState_ = EJobState::Aborted;
            return;
        }

        FinalJobState_ = EJobState::Failed;
    }

    void PrepareConfig()
    {
        INodePtr ioConfigNode;
        try {
            const auto& schedulerJobSpecExt = JobSpec.GetExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
            ioConfigNode = ConvertToNode(TYsonString(schedulerJobSpecExt.io_config()));
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error deserializing job IO configuration")
                << ex;
        }

        auto ioConfig = New<TJobIOConfig>();
        try {
            ioConfig->Load(ioConfigNode);
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error validating job IO configuration")
                << ex;
        }

        auto proxyConfig = CloneYsonSerializable(Bootstrap_->GetJobProxyConfig());
        proxyConfig->JobIO = ioConfig;
        proxyConfig->UserId = Slot_->GetUserId();
        proxyConfig->TmpfsPath = Slot_->GetTmpfsPath(ESandboxKind::User);

        proxyConfig->RpcServer = Slot_->GetRpcServerConfig();

        auto proxyConfigPath = NFS::CombinePaths(
            Slot_->GetWorkingDirectory(),
            ProxyConfigFileName);

        try {
            TFile file(proxyConfigPath, CreateAlways | WrOnly | Seq | CloseOnExec);
            TFileOutput output(file);
            TYsonWriter writer(&output, EYsonFormat::Pretty);
            proxyConfig->Save(&writer);
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "Error saving job proxy config (Path: %v)",
                proxyConfigPath);
            NLogging::TLogManager::Get()->Shutdown();
            _exit(1);
        }
    }

    void PrepareProxy()
    {
        Stroka environmentType = "default";
        try {
            auto environmentManager = Bootstrap_->GetEnvironmentManager();
            ProxyController_ = environmentManager->CreateProxyController(
                //XXX(psushin): execution environment type must not be directly
                // selectable by user -- it is more of the global cluster setting
                //jobSpec.operation_spec().environment(),
                environmentType,
                Id_,
                OperationId_,
                Slot_);
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Failed to create proxy controller for environment %Qv",
                environmentType)
                << ex;
        }
    }

    void PrepareTmpfs()
    {
        const auto& schedulerJobSpecExt = JobSpec.GetExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
        if (schedulerJobSpecExt.has_user_job_spec()) {
            const auto& userJobSpec = schedulerJobSpecExt.user_job_spec();
            if (userJobSpec.has_tmpfs_size()) {
                Slot_->PrepareTmpfs(ESandboxKind::User, userJobSpec.tmpfs_size());
            }
        }
    }

    void PrepareUserFiles()
    {
        const auto& schedulerJobSpecExt = JobSpec.GetExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);

        if (schedulerJobSpecExt.has_user_job_spec()) {
            const auto& userJobSpec = schedulerJobSpecExt.user_job_spec();
            for (const auto& descriptor : userJobSpec.files()) {
                PrepareFile(ESandboxKind::User, descriptor);
            }
        }

        if (schedulerJobSpecExt.has_input_query_spec()) {
            const auto& querySpec = schedulerJobSpecExt.input_query_spec();
            for (const auto& descriptor : querySpec.udf_files()) {
                PrepareFile(ESandboxKind::Udf, descriptor);
            }
        }
    }

    void RunJobProxy()
    {
        auto runError = WaitFor(ProxyController_->Run());

        // NB: We should explicitly call Kill() to clean up possible child processes.
        ProxyController_->Kill(Slot_->GetProcessGroup());

        runError.ThrowOnError();

        YCHECK(JobResult_.HasValue());
        YCHECK(JobPhase_ == EJobPhase::Running);

        JobPhase_ = EJobPhase::Cleanup;
        Slot_->Clean();
        YCHECK(JobPhase_ == EJobPhase::Cleanup);

        LOG_INFO("Job completed");

        FinalizeJob();
    }

    void FinalizeJob()
    {
        auto slotManager = Bootstrap_->GetExecSlotManager();
        slotManager->ReleaseSlot(Slot_);

        auto resourceDelta = ZeroNodeResources() - ResourceUsage;
        {
            TGuard<TSpinLock> guard(SpinLock);
            SetFinalState();
        }

        ResourcesUpdated_.Fire(resourceDelta);
    }

    // Must be called with set SpinLock.
    void SetFinalState()
    {
        ResourceUsage = ZeroNodeResources();

        JobPhase_ = EJobPhase::Finished;
        JobState_ = FinalJobState_;
    }

    void DoAbort()
    {
        if (GetState() != EJobState::Aborting) {
            return;
        }

        LOG_INFO("Aborting job");

        auto prevJobPhase = JobPhase_;
        JobPhase_ = EJobPhase::Cleanup;

        if (prevJobPhase >= EJobPhase::Running) {
            ProxyController_->Kill(Slot_->GetProcessGroup());
        }

        if (prevJobPhase >= EJobPhase::PreparingSandbox) {
            Slot_->Clean();
        }

        LOG_INFO("Job aborted");

        FinalizeJob();
    }

    void PrepareFile(ESandboxKind sandboxKind, const TFileDescriptor& descriptor)
    {
        const auto& fileName = descriptor.file_name();
        bool isExecutable = descriptor.executable();
        LOG_INFO("Preparing user file (FileName: %v, Executable: %v)",
            fileName,
            isExecutable);

        TArtifactKey key(descriptor);
        auto chunkCache = Bootstrap_->GetChunkCache();
        auto chunkOrError = WaitFor(chunkCache->PrepareArtifact(
            key,
            AuxNodeDirectory_));

        YCHECK(JobPhase_ == EJobPhase::PreparingFiles);
        THROW_ERROR_EXCEPTION_IF_FAILED(chunkOrError,
            "Failed to prepare user file %Qv",
            fileName);

        const auto& chunk = chunkOrError.Value();
        CachedChunks_.push_back(chunk);

        try {
            Slot_->MakeLink(
                sandboxKind,
                chunk->GetFileName(),
                fileName,
                isExecutable);
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION(
                "Failed to create a symlink for user file %Qv",
                fileName)
                << ex;
        }

        LOG_INFO("User file prepared successfully (FileName: %v)",
            fileName);
    }

    static TNullable<EAbortReason> GetAbortReason(const TJobResult& jobResult, bool signaled)
    {
        auto resultError = FromProto<TError>(jobResult.error());

        if (jobResult.HasExtension(TSchedulerJobResultExt::scheduler_job_result_ext)) {
            const auto& schedulerResultExt = jobResult.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);
            if (schedulerResultExt.failed_chunk_ids_size() > 0) {
                return MakeNullable(EAbortReason::FailedChunks);
            }
        }

        if (resultError.FindMatching(NExecAgent::EErrorCode::ResourceOverdraft)) {
            return MakeNullable(EAbortReason::ResourceOverdraft);
        }

        if (resultError.FindMatching(NExecAgent::EErrorCode::AbortByScheduler)) {
            return MakeNullable(EAbortReason::Scheduler);
        }

        if (resultError.FindMatching(NChunkClient::EErrorCode::AllTargetNodesFailed) ||
            resultError.FindMatching(NChunkClient::EErrorCode::MasterCommunicationFailed) ||
            resultError.FindMatching(NChunkClient::EErrorCode::MasterNotConnected) ||
            resultError.FindMatching(EErrorCode::ConfigCreationFailed) ||
            resultError.FindMatching(static_cast<int>(EExitStatus::ExitCodeBase) + static_cast<int>(EJobProxyExitCode::HeartbeatFailed)))
        {
            return MakeNullable(EAbortReason::Other);
        }

        if (signaled) {
            return EAbortReason::Other;
        }

        return Null;
    }

    static bool IsFatalError(const TError& error)
    {
        return
            error.FindMatching(NTableClient::EErrorCode::SortOrderViolation) ||
            error.FindMatching(NSecurityClient::EErrorCode::AuthenticationError) ||
            error.FindMatching(NSecurityClient::EErrorCode::AuthorizationError) ||
            error.FindMatching(NSecurityClient::EErrorCode::AccountLimitExceeded) ||
            error.FindMatching(NSecurityClient::EErrorCode::NoSuchAccount) ||
            error.FindMatching(NNodeTrackerClient::EErrorCode::NoSuchNetwork) ||
            error.FindMatching(NTableClient::EErrorCode::InvalidDoubleValue) ||
            error.FindMatching(NTableClient::EErrorCode::IncomparableType) ||
            error.FindMatching(NTableClient::EErrorCode::UnhashableType);
    }

};

NJobAgent::IJobPtr CreateUserJob(
    const TJobId& jobId,
    const TOperationId& operationId,
    const TNodeResources& resourceUsage,
    TJobSpec&& jobSpec,
    TBootstrap* bootstrap)
{
    return New<TJob>(
        jobId,
        operationId,
        resourceUsage,
        std::move(jobSpec),
        bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NExecAgent
} // namespace NYT


