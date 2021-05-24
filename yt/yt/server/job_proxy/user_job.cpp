#include "user_job.h"

#include "asan_warning_filter.h"
#include "private.h"
#include "job_detail.h"
#include "stderr_writer.h"
#include "user_job_synchronizer_service.h"
#include "user_job_write_controller.h"
#include "memory_tracker.h"
#include "tmpfs_manager.h"
#include "environment.h"
#include "core_watcher.h"

#ifdef __linux__
#include <yt/yt/server/lib/containers/instance.h>
#include <yt/yt/server/lib/containers/porto_executor.h>
#endif

#include <yt/yt/server/lib/job_proxy/config.h>

#include <yt/yt/server/lib/exec_agent/supervisor_service_proxy.h>

#include <yt/yt/server/lib/misc/public.h>

#include <yt/yt/server/lib/shell/shell_manager.h>

#include <yt/yt/server/lib/user_job_executor/config.h>

#include <yt/yt/server/lib/user_job_synchronizer_client/user_job_synchronizer.h>

#include <yt/yt/ytlib/chunk_client/chunk_reader_statistics.h>
#include <yt/yt/ytlib/chunk_client/helpers.h>

#include <yt/yt/ytlib/core_dump/proto/core_info.pb.h>

#include <yt/yt/ytlib/file_client/file_chunk_output.h>

#include <yt/yt/ytlib/job_proxy/user_job_read_controller.h>

#include <yt/yt/ytlib/job_prober_client/job_probe.h>

#include <yt/yt/ytlib/query_client/evaluator.h>
#include <yt/yt/ytlib/query_client/query.h>
#include <yt/yt/ytlib/query_client/public.h>
#include <yt/yt/ytlib/query_client/functions_cache.h>

#include <yt/yt/ytlib/table_client/helpers.h>
#include <yt/yt/ytlib/table_client/schemaless_multi_chunk_reader.h>
#include <yt/yt/ytlib/table_client/schemaless_chunk_writer.h>

#include <yt/yt/ytlib/transaction_client/public.h>

#include <yt/yt/ytlib/tools/proc.h>
#include <yt/yt/ytlib/tools/tools.h>
#include <yt/yt/ytlib/tools/signaler.h>

#include <yt/yt/client/formats/parser.h>

#include <yt/yt/client/query_client/query_statistics.h>

#include <yt/yt/client/table_client/name_table.h>
#include <yt/yt/client/table_client/unversioned_writer.h>
#include <yt/yt/client/table_client/schemaful_reader_adapter.h>
#include <yt/yt/client/table_client/table_consumer.h>

#include <yt/yt/core/concurrency/action_queue.h>
#include <yt/yt/core/concurrency/delayed_executor.h>
#include <yt/yt/core/concurrency/thread_pool.h>
#include <yt/yt/core/concurrency/periodic_executor.h>

#include <yt/yt/core/misc/finally.h>
#include <yt/yt/core/misc/fs.h>
#include <yt/yt/core/misc/numeric_helpers.h>
#include <yt/yt/core/misc/pattern_formatter.h>
#include <yt/yt/core/misc/proc.h>
#include <yt/yt/library/process/process.h>
#include <yt/yt/core/misc/public.h>
#include <yt/yt/library/process/subprocess.h>

#include <yt/yt/core/misc/statistics.h>

#include <yt/yt/core/net/connection.h>

#include <yt/yt/core/rpc/server.h>

#include <yt/yt/core/ypath/tokenizer.h>

#include <util/generic/guid.h>

#include <util/stream/null.h>
#include <util/stream/tee.h>

#include <util/system/compiler.h>
#include <util/system/execpath.h>
#include <util/system/fs.h>
#include <util/system/shellcommand.h>

namespace NYT::NJobProxy {

using namespace NTools;
using namespace NYTree;
using namespace NYson;
using namespace NNet;
using namespace NTableClient;
using namespace NFormats;
using namespace NFS;
using namespace NScheduler;
using namespace NScheduler::NProto;
using namespace NShell;
using namespace NTransactionClient;
using namespace NConcurrency;
using namespace NJobAgent;
using namespace NChunkClient;
using namespace NFileClient;
using namespace NChunkClient::NProto;
using namespace NPipes;
using namespace NQueryClient;
using namespace NRpc;
using namespace NCoreDump;
using namespace NExecAgent;
using namespace NYPath;
using namespace NJobProberClient;
using namespace NJobTrackerClient;
using namespace NUserJobExecutor;
using namespace NUserJobSynchronizerClient;

using NJobTrackerClient::NProto::TJobResult;
using NJobTrackerClient::NProto::TJobSpec;
using NScheduler::NProto::TUserJobSpec;
using NCoreDump::NProto::TCoreInfo;
using NChunkClient::TDataSliceDescriptor;

////////////////////////////////////////////////////////////////////////////////

#ifdef _unix_

static const int JobStatisticsFD = 5;
static const int JobProfileFD = 8;
static const size_t BufferSize = 1_MB;

static const size_t MaxCustomStatisticsPathLength = 512;

static TNullOutput NullOutput;

////////////////////////////////////////////////////////////////////////////////

static TString CreateNamedPipePath()
{
    const TString& name = CreateGuidAsString();
    return NFS::GetRealPath(NFS::CombinePaths("./pipes", name));
}

////////////////////////////////////////////////////////////////////////////////

class TUserJob
    : public TJob
{
public:
    TUserJob(
        IJobHost* host,
        const TUserJobSpec& userJobSpec,
        TJobId jobId,
        const std::vector<int>& ports,
        std::unique_ptr<TUserJobWriteController> userJobWriteController)
        : TJob(host)
        , Logger(Host_->GetLogger())
        , JobId_(jobId)
        , UserJobWriteController_(std::move(userJobWriteController))
        , UserJobSpec_(userJobSpec)
        , Config_(Host_->GetConfig())
        , JobIOConfig_(Host_->GetJobSpecHelper()->GetJobIOConfig())
        , UserJobEnvironment_(Host_->CreateUserJobEnvironment())
        , Ports_(ports)
        , JobErrorPromise_(NewPromise<void>())
        , JobEnvironmentType_(ConvertTo<TJobEnvironmentConfigPtr>(Config_->JobEnvironment)->Type)
        , PipeIOPool_(New<TThreadPool>(JobIOConfig_->PipeIOPoolSize, "PipeIO"))
        , AuxQueue_(New<TActionQueue>("JobAux"))
        , ReadStderrInvoker_(CreateSerializedInvoker(PipeIOPool_->GetInvoker()))
        , TmpfsManager_(New<TTmpfsManager>(Config_->TmpfsManager))
        , MemoryTracker_(New<TMemoryTracker>(Config_->MemoryTracker, UserJobEnvironment_, TmpfsManager_))
    {
        Host_->GetRpcServer()->RegisterService(CreateUserJobSynchronizerService(Logger, ExecutorPreparedPromise_, AuxQueue_->GetInvoker()));

        auto jobEnvironmentConfig = ConvertTo<TJobEnvironmentConfigPtr>(Config_->JobEnvironment);
        MemoryWatchdogPeriod_ = jobEnvironmentConfig->MemoryWatchdogPeriod;

        UserJobReadController_ = CreateUserJobReadController(
            Host_->GetJobSpecHelper(),
            Host_->GetClient(),
            PipeIOPool_->GetInvoker(),
            Host_->LocalDescriptor(),
            BIND(&IJobHost::ReleaseNetwork, MakeWeak(Host_)),
            SandboxDirectoryNames[ESandboxKind::Udf],
            ChunkReadOptions_,
            Host_->GetReaderBlockCache(),
            /*chunkMetaCache*/ nullptr,
            Host_->GetTrafficMeter(),
            Host_->GetInBandwidthThrottler(),
            Host_->GetOutRpsThrottler());

        InputPipeBlinker_ = New<TPeriodicExecutor>(
            AuxQueue_->GetInvoker(),
            BIND(&TUserJob::BlinkInputPipe, MakeWeak(this)),
            Config_->InputPipeBlinkerPeriod);

        MemoryWatchdogExecutor_ = New<TPeriodicExecutor>(
            AuxQueue_->GetInvoker(),
            BIND(&TUserJob::CheckMemoryUsage, MakeWeak(this)),
            MemoryWatchdogPeriod_);

        if (jobEnvironmentConfig->Type != EJobEnvironmentType::Simple) {
            UserId_ = jobEnvironmentConfig->StartUid + Config_->SlotIndex;
        }

        // TODO(gritukan): Why can't we set it even to 19500?
        if (Config_->DoNotSetUserId) {
            // TODO(gritukan): Make user id optional in exec.
            UserId_ = 0;
        }

        if (UserJobEnvironment_) {
            if (!host->GetConfig()->BusServer->UnixDomainSocketPath) {
                THROW_ERROR_EXCEPTION("Unix domain socket path is not configured");
            }

            IUserJobEnvironment::TUserJobProcessOptions options;
            if (UserJobSpec_.has_core_table_spec()) {
                options.SlotCoreWatcherDirectory = NFS::CombinePaths({Host_->GetSlotPath(), "cores"});
                options.CoreWatcherDirectory = NFS::CombinePaths({Host_->GetPreparationPath(), "cores"});
            }
            options.EnablePorto = TranslateEnablePorto(CheckedEnumCast<NScheduler::EEnablePorto>(UserJobSpec_.enable_porto()));
            options.EnableCudaGpuCoreDump = UserJobSpec_.enable_cuda_gpu_core_dump();
            options.HostName = Config_->HostName;
            options.NetworkAddresses = Config_->NetworkAddresses;
            options.ThreadLimit = UserJobSpec_.thread_limit();

            Process_ = UserJobEnvironment_->CreateUserJobProcess(
                ExecProgramName,
                options);

            BlockIOWatchdogExecutor_ = New<TPeriodicExecutor>(
                AuxQueue_->GetInvoker(),
                BIND(&TUserJob::CheckBlockIOUsage, MakeWeak(this)),
                UserJobEnvironment_->GetBlockIOWatchdogPeriod());
        } else {
            Process_ = New<TSimpleProcess>(ExecProgramName, false);
        }

        if (UserJobSpec_.has_core_table_spec()) {
            const auto& coreTableSpec = UserJobSpec_.core_table_spec();

            auto tableWriterOptions = ConvertTo<TTableWriterOptionsPtr>(
                TYsonString(coreTableSpec.output_table_spec().table_writer_options()));
            tableWriterOptions->EnableValidationOptions();
            auto chunkList = FromProto<TChunkListId>(coreTableSpec.output_table_spec().chunk_list_id());
            auto blobTableWriterConfig = ConvertTo<TBlobTableWriterConfigPtr>(TYsonString(coreTableSpec.blob_table_writer_config()));
            auto debugTransactionId = FromProto<TTransactionId>(UserJobSpec_.debug_output_transaction_id());

            CoreWatcher_ = New<TCoreWatcher>(
                Config_->CoreWatcher,
                NFS::GetRealPath("./cores"),
                Host_,
                AuxQueue_->GetInvoker(),
                blobTableWriterConfig,
                tableWriterOptions,
                debugTransactionId,
                chunkList);
        }
    }

    virtual void Initialize() override
    { }

    virtual TJobResult Run() override
    {
        YT_LOG_DEBUG("Starting job process");

        UserJobWriteController_->Init();

        Prepare();

        bool expected = false;
        if (Prepared_.compare_exchange_strong(expected, true)) {
            ProcessFinished_ = Process_->Spawn();
            YT_LOG_INFO("Job process started");

            if (BlockIOWatchdogExecutor_) {
                BlockIOWatchdogExecutor_->Start();
            }

            TDelayedExecutorCookie timeLimitCookie;
            if (UserJobSpec_.has_job_time_limit()) {
                auto timeLimit = FromProto<TDuration>(UserJobSpec_.job_time_limit());
                YT_LOG_INFO("Setting job time limit (Limit: %v)",
                    timeLimit);
                timeLimitCookie = TDelayedExecutor::Submit(
                    BIND(&TUserJob::OnJobTimeLimitExceeded, MakeWeak(this))
                        .Via(AuxQueue_->GetInvoker()),
                    timeLimit);
            }

            DoJobIO();

            TDelayedExecutor::Cancel(timeLimitCookie);
            WaitFor(InputPipeBlinker_->Stop())
                .ThrowOnError();

            if (!JobErrorPromise_.IsSet()) {
                FinalizeJobIO();
            }
            UploadStderrFile();

            CleanupUserProcesses();

            if (BlockIOWatchdogExecutor_) {
                WaitFor(BlockIOWatchdogExecutor_->Stop())
                    .ThrowOnError();
            }
            WaitFor(MemoryWatchdogExecutor_->Stop())
                .ThrowOnError();
        } else {
            JobErrorPromise_.TrySet(TError("Job aborted"));
        }

        auto jobResultError = JobErrorPromise_.TryGet();

        std::vector<TError> innerErrors;

        if (jobResultError)  {
            innerErrors.push_back(*jobResultError);
        }

        TJobResult result;
        auto* schedulerResultExt = result.MutableExtension(TSchedulerJobResultExt::scheduler_job_result_ext);

        SaveErrorChunkId(schedulerResultExt);
        UserJobWriteController_->PopulateStderrResult(schedulerResultExt);

        if (jobResultError) {
            try {
                DumpFailContexts(schedulerResultExt);
            } catch (const std::exception& ex) {
                YT_LOG_ERROR(ex, "Failed to dump input context");
            }
        } else {
            UserJobWriteController_->PopulateResult(schedulerResultExt);
        }

        if (UserJobSpec_.has_core_table_spec()) {
            auto coreDumped = jobResultError && jobResultError->Attributes().Get("core_dumped", false /* defaultValue */);
            std::optional<TDuration> finalizationTimeout;
            if (coreDumped) {
                finalizationTimeout = Config_->CoreWatcher->FinalizationTimeout;
                YT_LOG_INFO("Job seems to produce core dump, core watcher will wait for it (FinalizationTimeout: %v)",
                    finalizationTimeout);
            }
            auto coreResult = CoreWatcher_->Finalize(finalizationTimeout);

            YT_LOG_INFO("Core watcher finalized (CoreDumpCount: %v)",
                coreResult.CoreInfos.size());

            if (!coreResult.CoreInfos.empty()) {
                for (const auto& coreInfo : coreResult.CoreInfos) {
                    YT_LOG_DEBUG("Core file (Pid: %v, ExecutableName: %v, Size: %v)",
                        coreInfo.process_id(),
                        coreInfo.executable_name(),
                        coreInfo.size());
                }
                if (UserJobSpec_.fail_job_on_core_dump()) {
                    innerErrors.push_back(TError(EErrorCode::UserJobProducedCoreFiles, "User job produced core files")
                        << TErrorAttribute("core_infos", coreResult.CoreInfos));
                }
            }

            CoreInfos_ = coreResult.CoreInfos;

            ToProto(schedulerResultExt->mutable_core_infos(), coreResult.CoreInfos);
            YT_VERIFY(coreResult.BoundaryKeys.empty() || coreResult.BoundaryKeys.sorted());
            ToProto(schedulerResultExt->mutable_core_table_boundary_keys(), coreResult.BoundaryKeys);
        }

        if (ShellManager_) {
            WaitFor(BIND(&IShellManager::GracefulShutdown, ShellManager_, TError("Job completed"))
                .AsyncVia(Host_->GetControlInvoker())
                .Run())
                .ThrowOnError();
        }

        auto jobError = innerErrors.empty()
            ? TError()
            : TError(EErrorCode::UserJobFailed, "User job failed") << innerErrors;

        ToProto(result.mutable_error(), jobError);

        return result;
    }

    virtual void Cleanup() override
    {
        bool expected = true;
        if (Prepared_.compare_exchange_strong(expected, false)) {
            // Job has been prepared.
            CleanupUserProcesses();
        }
    }

    virtual void PrepareArtifacts() override
    {
        YT_LOG_INFO("Started preparing artifacts");

        // Prepare user artifacts.
        for (const auto& file : UserJobSpec_.files()) {
            if (!file.bypass_artifact_cache() && !file.copy_file()) {
                continue;
            }

            PrepareArtifact(
                file.file_name(),
                file.executable() ? 0777 : 0666);
        }

        // We need to give read access to sandbox directory to yt_node/yt_job_proxy effective user (usually yt:yt)
        // and to job user (e.g. yt_slot_N). Since they can have different groups, we fallback to giving read
        // access to everyone.
        // job proxy requires read access e.g. for getting tmpfs size.
        // Write access is for job user only, who becomes an owner.
        if (UserId_) {
            auto sandboxPath = NFS::CombinePaths(
                Host_->GetPreparationPath(),
                SandboxDirectoryNames[ESandboxKind::User]);

            auto config = New<TChownChmodConfig>();
            config->Permissions = 0755;
            config->Path = sandboxPath;
            config->UserId = static_cast<uid_t>(*UserId_);
            RunTool<TChownChmodTool>(config);
        }

        YT_LOG_INFO("Artifacts prepared");
    }

    void PrepareArtifact(
        const TString& artifactName,
        int permissions)
    {
        auto Logger = this->Logger
            .WithTag("ArtifactName: %v", artifactName);

        YT_LOG_DEBUG("Preparing artifact");

        auto sandboxPath = NFS::CombinePaths(
            Host_->GetPreparationPath(),
            SandboxDirectoryNames[ESandboxKind::User]);
        auto artifactPath = NFS::CombinePaths(sandboxPath, artifactName);

        auto onError = [&] (const TError& error) {
            Host_->OnArtifactPreparationFailed(artifactName, artifactPath, error);
        };

        try {
            auto pipePath = CreateNamedPipePath();
            auto pipe = TNamedPipe::Create(pipePath, /*permissions*/ 0755);

            TFile pipeFile(pipePath, OpenExisting | RdOnly | Seq | CloseOnExec);
            TFile artifactFile(artifactPath, CreateAlways | WrOnly | Seq | CloseOnExec);

            Host_->PrepareArtifact(artifactName, pipePath);

            YT_LOG_DEBUG("Materializing artifact");

            constexpr ssize_t SpliceCopyBlockSize = 16_MB;
            Splice(pipeFile, artifactFile, SpliceCopyBlockSize);

            NFS::SetPermissions(artifactPath, permissions);

            YT_LOG_DEBUG("Artifact materialized");
        } catch (const TSystemError& ex) {
            // For util functions.
            onError(TError::FromSystem(ex));
        } catch (const std::exception& ex) {
            onError(TError(ex));
        }
    }

    virtual double GetProgress() const override
    {
        return UserJobReadController_->GetProgress();
    }

    virtual i64 GetStderrSize() const override
    {
        if (!Prepared_) {
            return 0;
        }
        auto result = WaitFor(BIND([=] { return ErrorOutput_->GetCurrentSize(); })
            .AsyncVia(ReadStderrInvoker_)
            .Run());
        THROW_ERROR_EXCEPTION_IF_FAILED(result, "Error collecting job stderr size");
        return result.Value();
    }

    virtual std::vector<TChunkId> GetFailedChunkIds() const override
    {
        return UserJobReadController_->GetFailedChunkIds();
    }

    virtual TInterruptDescriptor GetInterruptDescriptor() const override
    {
        return UserJobReadController_->GetInterruptDescriptor();
    }

private:
    const NLogging::TLogger Logger;

    const TJobId JobId_;

    const std::unique_ptr<TUserJobWriteController> UserJobWriteController_;
    IUserJobReadControllerPtr UserJobReadController_;

    const TUserJobSpec& UserJobSpec_;

    const TJobProxyConfigPtr Config_;
    const NScheduler::TJobIOConfigPtr JobIOConfig_;
    const IUserJobEnvironmentPtr UserJobEnvironment_;

    std::vector<int> Ports_;

    TPromise<void> JobErrorPromise_;

    const EJobEnvironmentType JobEnvironmentType_;

    const TThreadPoolPtr PipeIOPool_;
    const TActionQueuePtr AuxQueue_;
    const IInvokerPtr ReadStderrInvoker_;

    TProcessBasePtr Process_;

    TString InputPipePath_;

    std::optional<int> UserId_;

    std::atomic<bool> Prepared_ = false;
    std::atomic<bool> Woodpecker_ = false;
    std::atomic<bool> JobStarted_ = false;
    std::atomic<bool> InterruptionSignalSent_ = false;

    const TTmpfsManagerPtr TmpfsManager_;

    const TMemoryTrackerPtr MemoryTracker_;
    TDuration MemoryWatchdogPeriod_;

    std::vector<std::unique_ptr<IOutputStream>> TableOutputs_;

    // Writes stderr data to Cypress file.
    std::unique_ptr<TStderrWriter> ErrorOutput_;
    std::unique_ptr<TProfileWriter> ProfileOutput_;

    // Core infos.
    TCoreInfos CoreInfos_;

    // StderrCombined_ is set only if stderr table is specified.
    // It redirects data to both ErrorOutput_ and stderr table writer.
    std::unique_ptr<TTeeOutput> StderrCombined_;

    IShellManagerPtr ShellManager_;

    std::vector<TNamedPipeConfigPtr> PipeConfigs_;

#ifdef _asan_enabled_
    std::unique_ptr<TAsanWarningFilter> AsanWarningFilter_;
#endif

    std::unique_ptr<TTableOutput> StatisticsOutput_;
    std::unique_ptr<IYsonConsumer> StatisticsConsumer_;

    std::vector<IConnectionReaderPtr> TablePipeReaders_;
    std::vector<IConnectionWriterPtr> TablePipeWriters_;
    IConnectionReaderPtr StatisticsPipeReader_;
    IConnectionReaderPtr StderrPipeReader_;
    IConnectionReaderPtr ProfilePipeReader_;

    std::vector<ISchemalessFormatWriterPtr> FormatWriters_;

    // Actually InputActions_ has only one element,
    // but use vector to reuse runAction code
    std::vector<TCallback<void()>> InputActions_;
    std::vector<TCallback<void()>> OutputActions_;
    std::vector<TCallback<void()>> StderrActions_;
    std::vector<TCallback<void()>> FinalizeActions_;

    TFuture<void> ProcessFinished_;
    std::vector<TString> Environment_;

    TPeriodicExecutorPtr MemoryWatchdogExecutor_;
    TPeriodicExecutorPtr BlockIOWatchdogExecutor_;
    TPeriodicExecutorPtr InputPipeBlinker_;

    TPromise<void> ExecutorPreparedPromise_ = NewPromise<void>();

    YT_DECLARE_SPINLOCK(TAdaptiveLock, StatisticsLock_);
    TStatistics CustomStatistics_;

    TCoreWatcherPtr CoreWatcher_;

    std::optional<TString> FailContext_;
    std::optional<TString> Profile_;

    std::atomic<bool> NotFullyConsumed_ = false;

    void Prepare()
    {
        PreparePipes();
        PrepareEnvironment();
        PrepareExecutorConfig();

        Process_->AddArguments({"--config", Host_->AdjustPath(GetExecutorConfigPath())});
        Process_->SetWorkingDirectory(NFS::CombinePaths(Host_->GetSlotPath(), SandboxDirectoryNames[ESandboxKind::User]));

        if (JobEnvironmentType_ == EJobEnvironmentType::Porto) {
#ifdef _linux_
            auto portoJobEnvironmentConfig = ConvertTo<TPortoJobEnvironmentConfigPtr>(Config_->JobEnvironment);
            auto portoExecutor = NContainers::CreatePortoExecutor(portoJobEnvironmentConfig->PortoExecutor, "job-shell");

            std::vector<TString> shellEnvironment;
            shellEnvironment.reserve(Environment_.size());
            std::vector<TString> visibleEnvironment;
            visibleEnvironment.reserve(Environment_.size());

            for (const auto& variable : Environment_) {
                if (variable.StartsWith("YT_SECURE_VAULT") && !UserJobSpec_.enable_secure_vault_variables_in_job_shell()) {
                    continue;
                }
                if (variable.StartsWith("YT_")) {
                    shellEnvironment.push_back(variable);
                }
                visibleEnvironment.push_back(variable);
            }

            auto shellManagerUid = UserId_;
            if (Config_->TestPollJobShell) {
                shellManagerUid = std::nullopt;
                shellEnvironment.push_back("PS1=\"test_job@shell:\\W$ \"");
            }

            std::optional<int> shellManagerGid;
            // YT-13790.
            if (Host_->GetConfig()->RootPath) {
                shellManagerGid = 1001;
            }

            ShellManager_ = CreateShellManager(
                portoExecutor,
                UserJobEnvironment_->GetUserJobInstance(),
                Host_->GetPreparationPath(),
                Host_->GetSlotPath(),
                shellManagerUid,
                shellManagerGid,
                Format("Job environment:\n%v\n", JoinToString(visibleEnvironment, AsStringBuf("\n"))),
                std::move(shellEnvironment)
            );
#endif
        }
    }

    void CleanupUserProcesses() const
    {
        BIND(&TUserJob::DoCleanupUserProcesses, MakeWeak(this))
            .Via(PipeIOPool_->GetInvoker())
            .Run();
    }

    void DoCleanupUserProcesses() const
    {
        if (UserJobEnvironment_) {
            UserJobEnvironment_->CleanProcesses();
        }
    }

    IOutputStream* CreateStatisticsOutput()
    {
        StatisticsConsumer_.reset(new TStatisticsConsumer(
            BIND(&TUserJob::AddCustomStatistics, Unretained(this))));
        auto parser = CreateParserForFormat(
            TFormat(EFormatType::Yson),
            EDataType::Tabular,
            StatisticsConsumer_.get());
        StatisticsOutput_.reset(new TTableOutput(std::move(parser)));
        return StatisticsOutput_.get();
    }

    TMultiChunkWriterOptionsPtr CreateFileOptions()
    {
        auto options = New<TMultiChunkWriterOptions>();
        options->Account = UserJobSpec_.has_file_account()
            ? UserJobSpec_.file_account()
            : NSecurityClient::TmpAccountName;
        options->ReplicationFactor = 1;
        options->ChunksVital = false;
        return options;
    }

    IOutputStream* CreateErrorOutput()
    {
        IOutputStream* result;

        ErrorOutput_.reset(new TStderrWriter(
            UserJobSpec_.max_stderr_size()));

        auto* stderrTableWriter = UserJobWriteController_->GetStderrTableWriter();
        if (stderrTableWriter) {
            StderrCombined_.reset(new TTeeOutput(ErrorOutput_.get(), stderrTableWriter));
            result = StderrCombined_.get();
        } else {
            result = ErrorOutput_.get();
        }

#ifdef _asan_enabled_
        AsanWarningFilter_.reset(new TAsanWarningFilter(result));
        result = AsanWarningFilter_.get();
#endif

        return result;
    }

    IOutputStream* CreateProfileOutput()
    {
        ProfileOutput_.reset(new TProfileWriter(
            UserJobSpec_.max_profile_size()));

        return ProfileOutput_.get();
    }

    void SaveErrorChunkId(TSchedulerJobResultExt* schedulerResultExt)
    {
        if (!ErrorOutput_) {
            return;
        }

        auto errorChunkId = ErrorOutput_->GetChunkId();
        if (errorChunkId) {
            ToProto(schedulerResultExt->mutable_stderr_chunk_id(), errorChunkId);
            YT_LOG_INFO("Stderr chunk generated (ChunkId: %v)", errorChunkId);
        }
    }

    void DumpFailContexts(TSchedulerJobResultExt* schedulerResultExt)
    {
        auto contexts = WaitFor(UserJobReadController_->GetInputContext())
            .ValueOrThrow();

        size_t size = 0;
        for (const auto& context : contexts) {
            size += context.Size();
        }

        FailContext_ = TString();
        FailContext_->reserve(size);
        for (const auto& context : contexts) {
            FailContext_->append(context.Begin(), context.Size());
        }

        auto contextChunkIds = DoDumpInputContext(contexts);

        YT_VERIFY(contextChunkIds.size() <= 1);
        if (!contextChunkIds.empty()) {
            ToProto(schedulerResultExt->mutable_fail_context_chunk_id(), contextChunkIds.front());
        }
    }

    virtual std::vector<TChunkId> DumpInputContext() override
    {
        ValidatePrepared();

        auto result = WaitFor(UserJobReadController_->GetInputContext());
        THROW_ERROR_EXCEPTION_IF_FAILED(result, "Error collecting job input context");
        const auto& contexts = result.Value();

        auto chunks = DoDumpInputContext(contexts);
        YT_VERIFY(chunks.size() == 1);

        if (chunks.front() == NullChunkId) {
            THROW_ERROR_EXCEPTION("Cannot dump job context: reading has not started yet");
        }

        return chunks;
    }

    std::vector<TChunkId> DoDumpInputContext(const std::vector<TBlob>& contexts)
    {
        std::vector<TChunkId> result;

        auto transactionId = FromProto<TTransactionId>(UserJobSpec_.debug_output_transaction_id());
        for (int index = 0; index < std::ssize(contexts); ++index) {
            TFileChunkOutput contextOutput(
                JobIOConfig_->ErrorFileWriter,
                CreateFileOptions(),
                Host_->GetClient(),
                transactionId,
                Host_->GetTrafficMeter(),
                Host_->GetOutBandwidthThrottler());

            const auto& context = contexts[index];
            contextOutput.Write(context.Begin(), context.Size());
            contextOutput.Finish();

            auto contextChunkId = contextOutput.GetChunkId();
            YT_LOG_INFO("Input context chunk generated (ChunkId: %v, InputIndex: %v)",
                contextChunkId,
                index);

            result.push_back(contextChunkId);
        }

        return result;
    }

    virtual std::optional<TString> GetFailContext() override
    {
        ValidatePrepared();

        return FailContext_;
    }

    virtual TString GetStderr() override
    {
        ValidatePrepared();

        auto result = WaitFor(BIND([=] () { return ErrorOutput_->GetCurrentData(); })
            .AsyncVia(ReadStderrInvoker_)
            .Run());
        THROW_ERROR_EXCEPTION_IF_FAILED(result, "Error collecting job stderr");
        return result.Value();
    }

    virtual const TCoreInfos& GetCoreInfos() const override
    {
        return CoreInfos_;
    }

    virtual std::optional<TJobProfile> GetProfile() override
    {
        ValidatePrepared();
        if (!ProfileOutput_) {
            return {};
        }

        auto result = WaitFor(BIND([=] () {
            auto profilePair = ProfileOutput_->GetProfile();
            TJobProfile profile;
            profile.Type = profilePair.first;
            profile.Blob = profilePair.second;
            return profile;
        })
            .AsyncVia(ReadStderrInvoker_)
            .Run());
        THROW_ERROR_EXCEPTION_IF_FAILED(result, "Error collecting job profile");
        return result.Value();
    }

    virtual TYsonString PollJobShell(
        const TJobShellDescriptor& jobShellDescriptor,
        const TYsonString& parameters) override
    {
        if (!ShellManager_) {
            THROW_ERROR_EXCEPTION("Job shell polling is not supported in non-Porto environment");
        }
        return ShellManager_->PollJobShell(jobShellDescriptor, parameters);
    }

    virtual void Interrupt() override
    {
        ValidatePrepared();

        if (!InterruptionSignalSent_.exchange(true) && UserJobSpec_.has_interruption_signal()) {
            try {
                std::vector<int> pids;
                if (UserJobEnvironment_) {
#ifdef _linux_
                    pids = UserJobEnvironment_->GetUserJobInstance()->GetPids();
#endif
                } else {
                    // Fallback for non-sudo tests run.
                    auto pid = Process_->GetProcessId();
                    pids = GetPidsUnderParent(pid);
                }

                auto signal = UserJobSpec_.interruption_signal();

                YT_LOG_DEBUG("Sending interruption signal to user job (SignalName: %v, UserJobPids: %v)",
                    signal,
                    pids);

                auto signalerConfig = New<TSignalerConfig>();
                signalerConfig->Pids = pids;
                signalerConfig->SignalName = signal;
                RunTool<TSignalerTool>(signalerConfig);
            } catch (const std::exception& ex) {
                YT_LOG_WARNING(ex, "Failed to send interruption signal to user job");
            }
        }

        UserJobReadController_->InterruptReader();
    }

    virtual void Fail() override
    {
        auto error = TError("Job failed by external request");
        JobErrorPromise_.TrySet(error);
        CleanupUserProcesses();
    }

    void ValidatePrepared()
    {
        if (!Prepared_) {
            THROW_ERROR_EXCEPTION(EErrorCode::JobNotPrepared, "Cannot operate on job: job has not been prepared yet");
        }
    }

    void UploadStderrFile()
    {
        if (JobErrorPromise_.IsSet() || UserJobSpec_.upload_stderr_if_completed()) {
            ErrorOutput_->Upload(
                JobIOConfig_->ErrorFileWriter,
                CreateFileOptions(),
                Host_->GetClient(),
                FromProto<TTransactionId>(UserJobSpec_.debug_output_transaction_id()),
                Host_->GetTrafficMeter(),
                Host_->GetOutBandwidthThrottler());
        }
    }

    void PrepareOutputTablePipes()
    {
        auto format = ConvertTo<TFormat>(TYsonString(UserJobSpec_.output_format()));
        auto typeConversionConfig = ConvertTo<TTypeConversionConfigPtr>(format.Attributes());
        auto valueConsumers = UserJobWriteController_->CreateValueConsumers(typeConversionConfig);
        auto parsers = CreateParsersForFormat(format, valueConsumers);

        auto outputStreamCount = UserJobWriteController_->GetOutputStreamCount();
        TableOutputs_.reserve(outputStreamCount);
        for (int i = 0; i < outputStreamCount; ++i) {
            TableOutputs_.emplace_back(std::make_unique<TTableOutput>(std::move(parsers[i])));

            int jobDescriptor = UserJobSpec_.use_yamr_descriptors()
                ? 3 + i
                : 3 * i + 1;

            // In case of YAMR jobs dup 1 and 3 fd for YAMR compatibility
            auto wrappingError = TError("Error writing to output table %v", i);
            auto reader = (UserJobSpec_.use_yamr_descriptors() && jobDescriptor == 3)
                ? PrepareOutputPipe({1, jobDescriptor}, TableOutputs_[i].get(), &OutputActions_, wrappingError)
                : PrepareOutputPipe({jobDescriptor}, TableOutputs_[i].get(), &OutputActions_, wrappingError);
            TablePipeReaders_.push_back(reader);
        }

        FinalizeActions_.push_back(BIND([=] () {
            auto checkErrors = [&] (const std::vector<TFuture<void>>& asyncErrors) {
                auto error = WaitFor(AllSucceeded(asyncErrors));
                THROW_ERROR_EXCEPTION_IF_FAILED(error, "Error closing table output");
            };

            std::vector<TFuture<void>> flushResults;
            for (const auto& valueConsumer : UserJobWriteController_->GetAllValueConsumers()) {
                flushResults.push_back(valueConsumer->Flush());
            }
            checkErrors(flushResults);

            std::vector<TFuture<void>> closeResults;
            for (auto writer : UserJobWriteController_->GetWriters()) {
                closeResults.push_back(writer->Close());
            }
            checkErrors(closeResults);
        }));
    }

    IConnectionReaderPtr PrepareOutputPipe(
        const std::vector<int>& jobDescriptors,
        IOutputStream* output,
        std::vector<TCallback<void()>>* actions,
        const TError& wrappingError)
    {
        auto pipe = TNamedPipe::Create(CreateNamedPipePath(), 0666);

        for (auto jobDescriptor : jobDescriptors) {
            // Since inside job container we see another rootfs, we must adjust pipe path.
            auto pipeConfig = New<TNamedPipeConfig>(Host_->AdjustPath(pipe->GetPath()), jobDescriptor, true);
            PipeConfigs_.emplace_back(std::move(pipeConfig));
        }

        auto asyncInput = pipe->CreateAsyncReader();

        actions->push_back(BIND([=] () {
            try {
                auto input = CreateSyncAdapter(asyncInput);
                PipeInputToOutput(input.get(), output, BufferSize);
            } catch (const std::exception& ex) {
                auto error = wrappingError
                    << ex;
                YT_LOG_ERROR(error);

                // We abort asyncInput for stderr.
                // Almost all readers are aborted in `OnIOErrorOrFinished', but stderr doesn't,
                // because we want to read and save as much stderr as possible even if job is failing.
                // But if stderr transferring fiber itself fails, child process may hang
                // if it wants to write more stderr. So we abort input (and therefore close the pipe) here.
                if (asyncInput == StderrPipeReader_) {
                    asyncInput->Abort();
                }

                THROW_ERROR error;
            }
        }));

        return asyncInput;
    }

    void PrepareInputTablePipe()
    {
        int jobDescriptor = 0;
        InputPipePath_= CreateNamedPipePath();
        auto pipe = TNamedPipe::Create(InputPipePath_, 0666);
        auto pipeConfig = New<TNamedPipeConfig>(Host_->AdjustPath(pipe->GetPath()), jobDescriptor, false);
        PipeConfigs_.emplace_back(std::move(pipeConfig));
        auto format = ConvertTo<TFormat>(TYsonString(UserJobSpec_.input_format()));

        auto reader = pipe->CreateAsyncReader();
        auto asyncOutput = pipe->CreateAsyncWriter();

        TablePipeWriters_.push_back(asyncOutput);

        auto transferInput = UserJobReadController_->PrepareJobInputTransfer(asyncOutput);
        InputActions_.push_back(BIND([=] {
            try {
                auto transferComplete = transferInput();
                WaitFor(transferComplete)
                    .ThrowOnError();
            } catch (const std::exception& ex) {
                THROW_ERROR_EXCEPTION("Table input pipe failed")
                    << TErrorAttribute("fd", jobDescriptor)
                    << ex;
            }
        }));

        FinalizeActions_.push_back(BIND([=] {
            bool throwOnFailure = UserJobSpec_.check_input_fully_consumed();

            try {
                auto buffer = TSharedMutableRef::Allocate(1, false);
                auto future = reader->Read(buffer);
                TErrorOr<size_t> result = WaitFor(future);
                if (!result.IsOK()) {
                    THROW_ERROR_EXCEPTION("Failed to check input stream after user process")
                        << TErrorAttribute("fd", jobDescriptor)
                        << result;
                }
                // Try to read some data from the pipe.
                if (result.Value() > 0) {
                    THROW_ERROR_EXCEPTION("Input stream was not fully consumed by user process")
                        << TErrorAttribute("fd", jobDescriptor);
                }
            } catch (...) {
                reader->Abort();
                NotFullyConsumed_.store(true);
                if (throwOnFailure) {
                    throw;
                }
            }
        }));
    }

    void PreparePipes()
    {
        YT_LOG_DEBUG("Initializing pipes");

        // We use the following convention for designating input and output file descriptors
        // in job processes:
        // fd == 3 * (N - 1) for the N-th input table (if exists)
        // fd == 3 * (N - 1) + 1 for the N-th output table (if exists)
        // fd == 2 for the error stream
        // e. g.
        // 0 - first input table
        // 1 - first output table
        // 2 - error stream
        // 3 - second input
        // 4 - second output
        // etc.
        //
        // A special option (ToDo(psushin): which one?) enables concatenating
        // all input streams into fd == 0.

        // Configure stderr pipe.
        StderrPipeReader_ = PrepareOutputPipe(
            {STDERR_FILENO},
            CreateErrorOutput(),
            &StderrActions_,
            TError("Error writing to stderr"));

        PrepareOutputTablePipes();

        if (!UserJobSpec_.use_yamr_descriptors()) {
            StatisticsPipeReader_ = PrepareOutputPipe(
                {JobStatisticsFD},
                CreateStatisticsOutput(),
                &OutputActions_,
                TError("Error writing custom job statistics"));

            ProfilePipeReader_ = PrepareOutputPipe(
                {JobProfileFD},
                CreateProfileOutput(),
                &StderrActions_,
                TError("Error writing job profile"));
        }

        PrepareInputTablePipe();

        YT_LOG_DEBUG("Pipes initialized");
    }

    void PrepareEnvironment()
    {
        TPatternFormatter formatter;
        formatter.AddProperty(
            "SandboxPath",
            NFS::CombinePaths(Host_->GetSlotPath(), SandboxDirectoryNames[ESandboxKind::User]));

        if (UserJobSpec_.has_network_project_id()) {
            Environment_.push_back(Format("YT_NETWORK_PROJECT_ID=%v", UserJobSpec_.network_project_id()));
        }

        for (int i = 0; i < UserJobSpec_.environment_size(); ++i) {
            Environment_.emplace_back(formatter.Format(UserJobSpec_.environment(i)));
        }

        if (Host_->GetConfig()->TestRootFS && Host_->GetConfig()->RootPath) {
            Environment_.push_back(Format("YT_ROOT_FS=%v", *Host_->GetConfig()->RootPath));
        }

        for (int index = 0; index < std::ssize(Ports_); ++index) {
            Environment_.push_back(Format("YT_PORT_%v=%v", index, Ports_[index]));
        }

        if (UserJobEnvironment_) {
            const auto& environment = UserJobEnvironment_->GetEnvironmentVariables();
            Environment_.insert(Environment_.end(), environment.begin(), environment.end());
        }
    }

    void AddCustomStatistics(const INodePtr& sample)
    {
        auto guard = Guard(StatisticsLock_);
        CustomStatistics_.AddSample("/custom", sample);

        int customStatisticsCount = 0;
        for (const auto& [path, summary] : CustomStatistics_.Data()) {
            if (HasPrefix(path, "/custom")) {
                if (path.size() > MaxCustomStatisticsPathLength) {
                    THROW_ERROR_EXCEPTION(
                        "Custom statistics path is too long: %v > %v",
                        path.size(),
                        MaxCustomStatisticsPathLength);
                }
                ++customStatisticsCount;
            }

            // ToDo(psushin): validate custom statistics path does not contain $.
        }

        if (customStatisticsCount > UserJobSpec_.custom_statistics_count_limit()) {
            THROW_ERROR_EXCEPTION(
                "Custom statistics count exceeded: %v > %v",
                customStatisticsCount,
                UserJobSpec_.custom_statistics_count_limit());
        }
    }

    virtual TStatistics GetStatistics() const override
    {
        TStatistics statistics;
        {
            auto guard = Guard(StatisticsLock_);
            statistics = CustomStatistics_;
        }

        if (const auto& dataStatistics = UserJobReadController_->GetDataStatistics()) {
            statistics.AddSample("/data/input", *dataStatistics);
        }

        statistics.AddSample("/data/input/not_fully_consumed", NotFullyConsumed_.load() ? 1 : 0);

        if (const auto& codecStatistics = UserJobReadController_->GetDecompressionStatistics()) {
            DumpCodecStatistics(*codecStatistics, "/codec/cpu/decode", &statistics);
        }

        DumpChunkReaderStatistics(&statistics, "/chunk_reader_statistics", ChunkReadOptions_.ChunkReaderStatistics);

        auto writers = UserJobWriteController_->GetWriters();
        for (size_t index = 0; index < writers.size(); ++index) {
            const auto& writer = writers[index];
            statistics.AddSample("/data/output/" + ToYPathLiteral(index), writer->GetDataStatistics());
            DumpCodecStatistics(writer->GetCompressionStatistics(), "/codec/cpu/encode/" + ToYPathLiteral(index), &statistics);
        }

        // Job environment statistics.
        if (UserJobEnvironment_ && Prepared_) {
            try {
                auto cpuStatistics = UserJobEnvironment_->GetCpuStatistics();
                statistics.AddSample("/user_job/cpu", cpuStatistics);
            } catch (const std::exception& ex) {
                YT_LOG_WARNING(ex, "Unable to get CPU statistics for user job");
            }

            try {
                auto blockIOStatistics = UserJobEnvironment_->GetBlockIOStatistics();
                statistics.AddSample("/user_job/block_io", blockIOStatistics);
            } catch (const std::exception& ex) {
                YT_LOG_WARNING(ex, "Unable to get block io statistics for user job");
            }

            statistics.AddSample("/user_job/woodpecker", Woodpecker_ ? 1 : 0);
        }

        try {
            TmpfsManager_->DumpTmpfsStatistics(&statistics, "/user_job");
        } catch (const std::exception& ex) {
            YT_LOG_WARNING(ex, "Failed to dump user job tmpfs statistics");
        }

        try {
            MemoryTracker_->DumpMemoryUsageStatistics(&statistics, "/user_job");
        } catch (const std::exception& ex) {
            YT_LOG_WARNING(ex, "Failed to dump user job memory usage statistics");
        }

        YT_VERIFY(UserJobSpec_.memory_limit() > 0);
        statistics.AddSample("/user_job/memory_limit", UserJobSpec_.memory_limit());
        statistics.AddSample("/user_job/memory_reserve", UserJobSpec_.memory_reserve());

        statistics.AddSample(
            "/user_job/memory_reserve_factor_x10000",
            static_cast<int>((1e4 * UserJobSpec_.memory_reserve()) / UserJobSpec_.memory_limit()));

        // Pipe statistics.
        if (Prepared_) {
            auto inputStatistics = TablePipeWriters_[0]->GetWriteStatistics();
            statistics.AddSample(
                "/user_job/pipes/input/idle_time",
                inputStatistics.IdleDuration);
            statistics.AddSample(
                "/user_job/pipes/input/busy_time",
                inputStatistics.BusyDuration);
            statistics.AddSample(
                "/user_job/pipes/input/bytes",
                TablePipeWriters_[0]->GetWriteByteCount());

            TDuration totalOutputIdleDuration;
            TDuration totalOutputBusyDuration;
            i64 totalOutputBytes = 0;
            for (int i = 0; i < std::ssize(TablePipeReaders_); ++i) {
                const auto& tablePipeReader = TablePipeReaders_[i];
                auto outputStatistics = tablePipeReader->GetReadStatistics();

                statistics.AddSample(
                    Format("/user_job/pipes/output/%v/idle_time", NYPath::ToYPathLiteral(i)),
                    outputStatistics.IdleDuration);
                totalOutputIdleDuration += outputStatistics.IdleDuration;

                statistics.AddSample(
                    Format("/user_job/pipes/output/%v/busy_time", NYPath::ToYPathLiteral(i)),
                    outputStatistics.BusyDuration);
                totalOutputBusyDuration += outputStatistics.BusyDuration;

                statistics.AddSample(
                    Format("/user_job/pipes/output/%v/bytes", NYPath::ToYPathLiteral(i)),
                    tablePipeReader->GetReadByteCount());
                totalOutputBytes += tablePipeReader->GetReadByteCount();
            }

            statistics.AddSample("/user_job/pipes/output/total/idle_time", totalOutputIdleDuration);
            statistics.AddSample("/user_job/pipes/output/total/busy_time", totalOutputBusyDuration);
            statistics.AddSample("/user_job/pipes/output/total/bytes", totalOutputBytes);
        }

        return statistics;
    }

    virtual TCpuStatistics GetCpuStatistics() const override
    {
        return UserJobEnvironment_ ? UserJobEnvironment_->GetCpuStatistics() : TCpuStatistics();
    }

    void OnIOErrorOrFinished(const TError& error, const TString& message)
    {
        if (error.IsOK() || error.FindMatching(NNet::EErrorCode::Aborted)) {
            return;
        }

        if (!JobErrorPromise_.TrySet(error)) {
            return;
        }

        YT_LOG_ERROR(error, "%v", message);

        CleanupUserProcesses();

        for (const auto& reader : TablePipeReaders_) {
            reader->Abort();
        }

        for (const auto& writer : TablePipeWriters_) {
            writer->Abort();
        }

        if (StatisticsPipeReader_) {
            StatisticsPipeReader_->Abort();
        }

        if (!JobStarted_) {
            // If start action didn't finish successfully, stderr could have stayed closed,
            // and output action may hang.
            // But if job is started we want to save as much stderr as possible
            // so we don't close stderr in that case.
            StderrPipeReader_->Abort();

            if (ProfilePipeReader_) {
                ProfilePipeReader_->Abort();
            }
        }
    }

    TString GetExecutorConfigPath() const
    {
        const static TString ExecutorConfigFileName = "executor_config.yson";

        return CombinePaths(NFs::CurrentWorkingDirectory(), ExecutorConfigFileName);
    }

    void PrepareExecutorConfig()
    {
        auto executorConfig = New<TUserJobExecutorConfig>();

        executorConfig->Command = UserJobSpec_.shell_command();
        executorConfig->JobId = ToString(JobId_);

        if (UserJobSpec_.has_core_table_spec() || UserJobSpec_.force_core_dump()) {
#ifdef _asan_enabled_
            YT_LOG_WARNING("Core dumps are not allowed in ASAN build");
#else
            executorConfig->EnableCoreDump = true;
#endif
        }

        if (UserId_) {
            executorConfig->Uid = *UserId_;
        }

        executorConfig->Pipes = PipeConfigs_;
        executorConfig->Environment = Environment_;

        {
            auto connectionConfig = New<TUserJobSynchronizerConnectionConfig>();
            auto processWorkingDirectory = NFS::CombinePaths(Host_->GetPreparationPath(), SandboxDirectoryNames[ESandboxKind::User]);
            connectionConfig->BusClientConfig->UnixDomainSocketPath = GetRelativePath(processWorkingDirectory, *Host_->GetConfig()->BusServer->UnixDomainSocketPath);
            executorConfig->UserJobSynchronizerConnectionConfig = connectionConfig;
        }

        auto executorConfigPath = GetExecutorConfigPath();
        try {
            TFile configFile(executorConfigPath, CreateAlways | WrOnly | Seq | CloseOnExec);
            TUnbufferedFileOutput output(configFile);
            NYson::TYsonWriter writer(&output, EYsonFormat::Pretty);
            Serialize(executorConfig, &writer);
            writer.Flush();
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Failed to write executor config into %v", executorConfigPath)
                << ex;
        }
    }

    void DoJobIO()
    {
        auto onIOError = BIND([=] (const TError& error) {
            OnIOErrorOrFinished(error, "Job input/output error, aborting");
        });

        auto onStartIOError = BIND([=] (const TError& error) {
            OnIOErrorOrFinished(error, "Executor input/output error, aborting");
        });

        auto onProcessFinished = BIND([=, this_ = MakeStrong(this)] (const TError& userJobError) {
            YT_LOG_DEBUG("Process finished (UserJobError: %v)", userJobError);

            OnIOErrorOrFinished(userJobError, "Job control process has finished, aborting");

            // If process has crashed before sending notification we stuck
            // on waiting executor promise, so set it here.
            // Do this after JobProxyError is set (if necessary).
            ExecutorPreparedPromise_.TrySet(TError());
        });

        auto runActions = [&] (
            const std::vector<TCallback<void()>>& actions,
            const NYT::TCallback<void(const TError&)>& onError,
            IInvokerPtr invoker)
        {
            std::vector<TFuture<void>> result;
            for (const auto& action : actions) {
                auto asyncError = BIND(action)
                    .AsyncVia(invoker)
                    .Run();
                result.emplace_back(asyncError.Apply(onError));
            }
            return result;
        };

        auto processFinished = ProcessFinished_.Apply(onProcessFinished);

        // Wait until executor opens and dup named pipes.
        YT_LOG_DEBUG("Wait for signal from executor");
        WaitFor(ExecutorPreparedPromise_.ToFuture())
            .ThrowOnError();

        MemoryWatchdogExecutor_->Start();

        if (!JobErrorPromise_.IsSet()) {
            Host_->OnPrepared();
            // Now writing pipe is definitely ready, so we can start blinking.
            InputPipeBlinker_->Start();
            JobStarted_ = true;
        } else {
            YT_LOG_ERROR(JobErrorPromise_.Get(), "Failed to prepare executor");
            return;
        }
        YT_LOG_INFO("Start actions finished");
        auto inputFutures = runActions(InputActions_, onIOError, PipeIOPool_->GetInvoker());
        auto outputFutures = runActions(OutputActions_, onIOError, PipeIOPool_->GetInvoker());
        auto stderrFutures = runActions(StderrActions_, onIOError, ReadStderrInvoker_);

        // First, wait for all job output pipes.
        // If job successfully completes or dies prematurely, they close automatically.
        WaitFor(AllSet(outputFutures))
            .ThrowOnError();
        YT_LOG_INFO("Output actions finished");

        WaitFor(AllSet(stderrFutures))
            .ThrowOnError();
        YT_LOG_INFO("Error actions finished");

        // Then, wait for job process to finish.
        // Theoretically, process could have explicitely closed its output pipes
        // but still be doing some computations.
        YT_VERIFY(WaitFor(processFinished).IsOK());
        YT_LOG_INFO("Job process finished (Error: %v)", JobErrorPromise_.ToFuture().TryGet());

        // Abort input pipes unconditionally.
        // If the job didn't read input to the end, pipe writer could be blocked,
        // because we didn't close the reader end (see check_input_fully_consumed).
        for (const auto& writer : TablePipeWriters_) {
            writer->Abort();
        }

        // Now make sure that input pipes are also completed.
        WaitFor(AllSet(inputFutures))
            .ThrowOnError();
        YT_LOG_INFO("Input actions finished");
    }

    void FinalizeJobIO()
    {
        for (const auto& action : FinalizeActions_) {
            try {
                action.Run();
            } catch (const std::exception& ex) {
                JobErrorPromise_.TrySet(ex);
            }
        }
    }

    void CheckMemoryUsage()
    {
        i64 memoryUsage;
        try {
            memoryUsage = MemoryTracker_->GetMemoryUsage();
        } catch (const std::exception& ex) {
            YT_LOG_WARNING(ex, "Failed to get user job memory usage");
            return;
        }

        auto memoryLimit = UserJobSpec_.memory_limit();
        YT_LOG_DEBUG("Checking memory usage (MemoryUsage: %v, MemoryLimit: %v)",
            memoryUsage,
            memoryLimit);

        if (memoryUsage > memoryLimit) {
            YT_LOG_DEBUG("Memory limit exceeded");
            auto error = TError(
                NJobProxy::EErrorCode::MemoryLimitExceeded,
                "Memory limit exceeded")
                << TErrorAttribute("usage", memoryUsage)
                << TErrorAttribute("limit", memoryLimit);
            JobErrorPromise_.TrySet(error);
            CleanupUserProcesses();
        }

        Host_->SetUserJobMemoryUsage(memoryUsage);
    }

    void CheckBlockIOUsage()
    {
        if (!UserJobEnvironment_) {
            return;
        }

        TBlockIOStatistics blockIOStats;
        try {
            blockIOStats = UserJobEnvironment_->GetBlockIOStatistics();
        } catch (const std::exception& ex) {
            YT_LOG_WARNING(ex, "Unable to get block io statistics to find a woodpecker");
            return;
        }

        if (UserJobSpec_.has_iops_threshold() &&
            blockIOStats.IOTotal > static_cast<ui64>(UserJobSpec_.iops_threshold()) &&
            !Woodpecker_)
        {
            YT_LOG_DEBUG("Woodpecker detected (IORead: %v, IOTotal: %v, Threshold: %v)",
                blockIOStats.IORead,
                blockIOStats.IOTotal,
                UserJobSpec_.iops_threshold());
            Woodpecker_ = true;

            if (UserJobSpec_.has_iops_throttler_limit()) {
                YT_LOG_DEBUG("Set IO throttle (Iops: %v)", UserJobSpec_.iops_throttler_limit());
                UserJobEnvironment_->SetIOThrottle(UserJobSpec_.iops_throttler_limit());
            }
        }
    }

    void OnJobTimeLimitExceeded()
    {
        auto error = TError(
            NJobProxy::EErrorCode::JobTimeLimitExceeded,
            "Job time limit exceeded")
            << TErrorAttribute("limit", UserJobSpec_.job_time_limit());
        JobErrorPromise_.TrySet(error);
        CleanupUserProcesses();
    }

    // NB(psushin): YT-5629.
    void BlinkInputPipe() const
    {
        // This method is called after preparation and before finalization.
        // Reader must be opened and ready, so open must succeed.
        // Still an error can occur in case of external forced sandbox clearance (e.g. in integration tests).
        auto fd = HandleEintr(::open, InputPipePath_.c_str(), O_WRONLY |  O_CLOEXEC | O_NONBLOCK);
        if (fd >= 0) {
            ::close(fd);
        } else {
            YT_LOG_WARNING(TError::FromSystem(), "Failed to blink input pipe (Path: %v)", InputPipePath_);
        }
    }

    static NContainers::EEnablePorto TranslateEnablePorto(NScheduler::EEnablePorto value)
    {
        switch (value) {
            case NScheduler::EEnablePorto::None:    return NContainers::EEnablePorto::None;
            case NScheduler::EEnablePorto::Isolate: return NContainers::EEnablePorto::Isolate;
            default:                                YT_ABORT();
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

IJobPtr CreateUserJob(
    IJobHost* host,
    const TUserJobSpec& userJobSpec,
    TJobId jobId,
    const std::vector<int>& ports,
    std::unique_ptr<TUserJobWriteController> userJobWriteController)
{
    return New<TUserJob>(
        host,
        userJobSpec,
        jobId,
        std::move(ports),
        std::move(userJobWriteController));
}

#else

IJobPtr CreateUserJob(
    IJobHost* host,
    const TUserJobSpec& UserJobSpec_,
    TJobId jobId,
    const std::vector<int>& ports,
    std::unique_ptr<TUserJobWriteController> userJobWriteController)
{
    THROW_ERROR_EXCEPTION("Streaming jobs are supported only under Unix");
}

#endif

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy
