#include "config.h"
#include "helpers.h"
#include "job_resources.h"

#include <yt/yt/ytlib/scheduler/helpers.h>
#include <yt/yt/ytlib/scheduler/proto/job.pb.h>

#include <yt/yt/client/security_client/acl.h>
#include <yt/yt/client/security_client/helpers.h>

#include <yt/yt/core/ytree/convert.h>

#include <yt/yt/core/misc/fs.h>

#include <util/string/split.h>

#include <util/folder/path.h>

namespace NYT::NScheduler {

using namespace NYson;
using namespace NYTree;
using namespace NSecurityClient;
using namespace NTableClient;
using namespace NTransactionClient;

using NVectorHdrf::EIntegralGuaranteeType;

extern const TString OperationAliasPrefix;

////////////////////////////////////////////////////////////////////////////////

static void ValidateOperationAcl(const TSerializableAccessControlList& acl)
{
    for (const auto& ace : acl.Entries) {
        if (ace.Action != ESecurityAction::Allow && ace.Action != ESecurityAction::Deny) {
            THROW_ERROR_EXCEPTION("Action %Qlv is forbidden to specify in operation ACE",
                ace.Action)
                << TErrorAttribute("ace", ace);
        }
        if (Any(ace.Permissions & ~(EPermission::Read | EPermission::Manage | EPermission::Administer))) {
            THROW_ERROR_EXCEPTION("Only %Qlv, %Qlv and %Qlv permissions are allowed in operation ACL, got %v",
                EPermission::Read,
                EPermission::Manage,
                EPermission::Administer,
                ConvertToYsonString(ace.Permissions, EYsonFormat::Text))
                << TErrorAttribute("ace", ace);
        }
    }
}

static void ProcessAclAndOwnersParameters(TSerializableAccessControlList* acl, std::vector<TString>* owners)
{
    if (!acl->Entries.empty() && !owners->empty()) {
        // COMPAT(levysotsky): Priority is given to |acl| currently.
        // An error should be thrown here when all the clusters are updated.
    } else if (!owners->empty()) {
        acl->Entries.emplace_back(
            ESecurityAction::Allow,
            *owners,
            EPermissionSet(EPermission::Read | EPermission::Manage));
        owners->clear();
    }
}

static void ValidateNoOutputStreams(const TUserJobSpecPtr& spec, EOperationType operationType)
{
    if (!spec->OutputStreams.empty()) {
        THROW_ERROR_EXCEPTION("\"output_streams\" are currently not allowed in %Qlv operations",
            operationType);
    }
}

////////////////////////////////////////////////////////////////////////////////

static const int MaxAllowedProfilingTagCount = 200;

TPoolName::TPoolName(TString pool, std::optional<TString> parent)
{
    if (parent) {
        Pool = *parent +  Delimiter + pool;
        ParentPool = std::move(parent);
    } else {
        Pool = std::move(pool);
    }
}

const char TPoolName::Delimiter = '$';

const TString& TPoolName::GetPool() const
{
    return Pool;
}

const std::optional<TString>& TPoolName::GetParentPool() const
{
    return ParentPool;
}

const TString& TPoolName::GetSpecifiedPoolName() const
{
    return ParentPool ? *ParentPool : Pool;
}

TPoolName TPoolName::FromString(const TString& value)
{
    std::vector<TString> parts;
    StringSplitter(value).Split(Delimiter).AddTo(&parts);
    switch (parts.size()) {
        case 1:
            return TPoolName(value, std::nullopt);
        case 2:
            return TPoolName(parts[1], parts[0]);
        default:
            THROW_ERROR_EXCEPTION("Malformed pool name: delimiter %Qv is found more than once in %Qv",
                TPoolName::Delimiter,
                value);
    }
}

TString TPoolName::ToString() const
{
    return Pool;
}

void Deserialize(TPoolName& value, INodePtr node)
{
    value = TPoolName::FromString(node->AsString()->GetValue());
}

void Deserialize(TPoolName& value, TYsonPullParserCursor* cursor)
{
    MaybeSkipAttributes(cursor);
    EnsureYsonToken("TPoolName", *cursor, EYsonItemType::StringValue);
    value = TPoolName::FromString(ExtractTo<TString>(cursor));
}

void Serialize(const TPoolName& value, IYsonConsumer* consumer)
{
    consumer->OnStringScalar(value.ToString());
}

////////////////////////////////////////////////////////////////////////////////

void TJobIOConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("table_reader", &TThis::TableReader)
        .DefaultNew();
    registrar.Parameter("table_writer", &TThis::TableWriter)
        .DefaultNew();
    registrar.Parameter("dynamic_table_writer", &TThis::DynamicTableWriter)
        .DefaultNew();

    registrar.Parameter("control_attributes", &TThis::ControlAttributes)
        .DefaultNew();

    registrar.Parameter("error_file_writer", &TThis::ErrorFileWriter)
        .DefaultNew();

    registrar.Parameter("buffer_row_count", &TThis::BufferRowCount)
        .Default(10 * 1000)
        .GreaterThan(0);

    registrar.Parameter("pipe_io_pool_size", &TThis::PipeIOPoolSize)
        .Default(1)
        .GreaterThan(0);

    registrar.Parameter("block_cache", &TThis::BlockCache)
        .DefaultNew();

    registrar.Parameter("testing_options", &TThis::Testing)
        .DefaultNew();

    registrar.Preprocessor([] (TJobIOConfig* config) {
        config->ErrorFileWriter->UploadReplicationFactor = 1;

        config->DynamicTableWriter->DesiredChunkSize = 100_MB;
        config->DynamicTableWriter->BlockSize = 256_KB;
    });
}

////////////////////////////////////////////////////////////////////////////////

void TTestingOperationOptions::Register(TRegistrar registrar)
{
    registrar.Parameter("controller_scheduling_delay", &TThis::ControllerSchedulingDelay)
        .Default();
    registrar.Parameter("controller_scheduling_delay_type", &TThis::ControllerSchedulingDelayType)
        .Default(EDelayType::Sync);
    registrar.Parameter("schedule_job_delay", &TThis::ScheduleJobDelay)
        .Default();
    registrar.Parameter("schedule_job_delay_type", &TThis::ScheduleJobDelayType)
        .Default();
    registrar.Parameter("delay_inside_revive", &TThis::DelayInsideRevive)
        .Default();
    registrar.Parameter("delay_inside_initialize", &TThis::DelayInsideInitialize)
        .Default();
    registrar.Parameter("delay_inside_prepare", &TThis::DelayInsidePrepare)
        .Default();
    registrar.Parameter("delay_inside_suspend", &TThis::DelayInsideSuspend)
        .Default();
    registrar.Parameter("delay_inside_materialize", &TThis::DelayInsideMaterialize)
        .Default();
    registrar.Parameter("delay_inside_operation_commit", &TThis::DelayInsideOperationCommit)
        .Default();
    registrar.Parameter("delay_after_materialize", &TThis::DelayAfterMaterialize)
        .Default();
    registrar.Parameter("delay_inside_abort", &TThis::DelayInsideAbort)
        .Default();
    registrar.Parameter("delay_inside_register_jobs_from_revived_operation", &TThis::DelayInsideRegisterJobsFromRevivedOperation)
        .Default();
    registrar.Parameter("delay_inside_validate_runtime_parameters", &TThis::DelayInsideValidateRuntimeParameters)
        .Default();
    registrar.Parameter("delay_before_start", &TThis::DelayBeforeStart)
        .Default();
    registrar.Parameter("delay_inside_operation_commit_stage", &TThis::DelayInsideOperationCommitStage)
        .Default();
    registrar.Parameter("no_delay_on_second_entrance_to_commit", &TThis::NoDelayOnSecondEntranceToCommit)
        .Default(false);
    registrar.Parameter("controller_failure", &TThis::ControllerFailure)
        .Default();
    registrar.Parameter("get_job_spec_delay", &TThis::GetJobSpecDelay)
        .Default();
    registrar.Parameter("fail_get_job_spec", &TThis::FailGetJobSpec)
        .Default(false);
    registrar.Parameter("testing_speculative_launch_mode", &TThis::TestingSpeculativeLaunchMode)
        .Default(ETestingSpeculativeLaunchMode::None);
    registrar.Parameter("allocation_size", &TThis::AllocationSize)
        .GreaterThanOrEqual(0)
        .LessThanOrEqual(100_GB)
        .Default();
    registrar.Parameter("cancellation_stage", &TThis::CancelationStage)
        .Default();
    registrar.Parameter("build_job_spec_proto_delay", &TThis::BuildJobSpecProtoDelay)
        .Default();
    registrar.Parameter("test_job_speculation_timeout", &TThis::TestJobSpeculationTimeout)
        .Default(false);
    registrar.Parameter("crash_controller_agent", &TThis::CrashControllerAgent)
        .Default(false);
    registrar.Parameter("throw_exception_during_operation_abort", &TThis::ThrowExceptionDuringOprationAbort)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

void TJobSplitterConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable_job_splitting", &TThis::EnableJobSplitting)
        .Default(true);

    registrar.Parameter("enable_job_speculation", &TThis::EnableJobSpeculation)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

void TAutoMergeConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("job_io", &TThis::JobIO)
        .DefaultNew();
    registrar.Parameter("max_intermediate_chunk_count", &TThis::MaxIntermediateChunkCount)
        .Default()
        .GreaterThanOrEqual(1);
    registrar.Parameter("chunk_count_per_merge_job", &TThis::ChunkCountPerMergeJob)
        .Default()
        .GreaterThanOrEqual(1);
    registrar.Parameter("chunk_size_threshold", &TThis::ChunkSizeThreshold)
        .Default(128_MB)
        .GreaterThanOrEqual(1);
    registrar.Parameter("mode", &TThis::Mode)
        .Default(EAutoMergeMode::Disabled);
    registrar.Parameter("enable_shallow_merge", &TThis::EnableShallowMerge)
        .Default(false);
    registrar.Parameter("allow_unknown_extensions",  &TAutoMergeConfig::AllowUnknownExtensions)
        .Default(false);
    registrar.Parameter("max_block_count", &TThis::MaxBlockCount)
        .Default();

    registrar.Parameter("use_intermediate_data_account", &TThis::UseIntermediateDataAccount)
        .Default(false);
    registrar.Parameter("shallow_merge_min_data_weight_per_chunk", &TThis::ShallowMergeMinDataWeightPerChunk)
        .Default(64_KB);

    registrar.Postprocessor([] (TAutoMergeConfig* config) {
        if (config->Mode == EAutoMergeMode::Manual) {
            if (!config->MaxIntermediateChunkCount || !config->ChunkCountPerMergeJob) {
                THROW_ERROR_EXCEPTION(
                    "Maximum intermediate chunk count and chunk count per merge job "
                    "should both be present when using relaxed mode of auto merge");
            }
            if (*config->MaxIntermediateChunkCount < *config->ChunkCountPerMergeJob) {
                THROW_ERROR_EXCEPTION("Maximum intermediate chunk count cannot be less than chunk count per merge job")
                    << TErrorAttribute("max_intermediate_chunk_count", *config->MaxIntermediateChunkCount)
                    << TErrorAttribute("chunk_count_per_merge_job", *config->ChunkCountPerMergeJob);
            }
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TTentativeTreeEligibilityConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("sample_job_count", &TThis::SampleJobCount)
        .Default(10)
        .GreaterThan(0);

    registrar.Parameter("max_tentative_job_duration_ratio", &TThis::MaxTentativeJobDurationRatio)
        .Default(10.0)
        .GreaterThan(0.0);

    registrar.Parameter("min_job_duration", &TThis::MinJobDuration)
        .Default(TDuration::Seconds(30));

    registrar.Parameter("ignore_missing_pool_trees", &TThis::IgnoreMissingPoolTrees)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

void TSamplingConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("sampling_rate", &TThis::SamplingRate)
        .Default();

    registrar.Parameter("max_total_slice_count", &TThis::MaxTotalSliceCount)
        .Default();

    registrar.Parameter("io_block_size", &TThis::IOBlockSize)
        .Default(16_MB);

    registrar.Postprocessor([] (TSamplingConfig* config) {
        if (config->SamplingRate && (*config->SamplingRate < 0.0 || *config->SamplingRate > 1.0)) {
            THROW_ERROR_EXCEPTION("Sampling rate should be in range [0.0, 1.0]")
                << TErrorAttribute("sampling_rate", config->SamplingRate);
        }
        if (config->MaxTotalSliceCount && *config->MaxTotalSliceCount <= 0) {
            THROW_ERROR_EXCEPTION("max_total_slice_count should be positive")
                << TErrorAttribute("max_total_slice_count", config->MaxTotalSliceCount);
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TTmpfsVolumeConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("size", &TThis::Size);
    registrar.Parameter("path", &TThis::Path);
}

void ToProto(NScheduler::NProto::TTmpfsVolume* protoTmpfsVolume, const TTmpfsVolumeConfig& tmpfsVolumeConfig)
{
    protoTmpfsVolume->set_size(tmpfsVolumeConfig.Size);
    protoTmpfsVolume->set_path(tmpfsVolumeConfig.Path);
}

void FromProto(TTmpfsVolumeConfig* tmpfsVolumeConfig, const NScheduler::NProto::TTmpfsVolume& protoTmpfsVolume)
{
    tmpfsVolumeConfig->Size = protoTmpfsVolume.size();
    tmpfsVolumeConfig->Path = protoTmpfsVolume.path();
}

////////////////////////////////////////////////////////////////////////////////

void TDiskRequestConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("disk_space", &TThis::DiskSpace);
    registrar.Parameter("inode_count", &TThis::InodeCount)
        .Default();
    registrar.Parameter("medium_name", &TThis::MediumName)
        .Default(std::nullopt);
    registrar.Parameter("account", &TThis::Account)
        .Default(std::nullopt);

    registrar.Postprocessor([&] (TDiskRequestConfig* config) {
        if (config->Account && !config->MediumName) {
            THROW_ERROR_EXCEPTION("\"medium_name\" is required in disk request if account is specified");
        }
    });
}

void ToProto(
    NProto::TDiskRequest* protoDiskRequest,
    const TDiskRequestConfig& diskRequestConfig)
{
    protoDiskRequest->set_disk_space(diskRequestConfig.DiskSpace);
    if (diskRequestConfig.InodeCount) {
        protoDiskRequest->set_inode_count(*diskRequestConfig.InodeCount);
    }
    if (diskRequestConfig.MediumName) {
        YT_VERIFY(diskRequestConfig.MediumIndex);
        protoDiskRequest->set_medium_index(*diskRequestConfig.MediumIndex);
    }
}

////////////////////////////////////////////////////////////////////////////////

void TJobShell::Register(TRegistrar registrar)
{
    registrar.Parameter("name", &TThis::Name);

    registrar.Parameter("subcontainer", &TThis::Subcontainer)
        .Default();

    registrar.Parameter("owners", &TThis::Owners)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

void TUserJobMonitoringConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable", &TThis::Enable)
        .Default(false);

    registrar.Parameter("sensor_names", &TThis::SensorNames)
        .Default(GetDefaultSensorNames());
}

const std::vector<TString>& TUserJobMonitoringConfig::GetDefaultSensorNames()
{
    static const std::vector<TString> DefaultSensorNames = {
        "cpu/user",
        "cpu/system",
        "cpu/wait",
        "cpu/throttled",
        "cpu/context_switches",
        "current_memory/rss",
        "tmpfs_size",
        "disk/usage",
        "disk/limit",
        "gpu/utilization_gpu",
        "gpu/utilization_memory",
        "gpu/utilization_power",
        "gpu/utilization_clock_sm",
        "gpu/memory",
        "gpu/power",
        "gpu/clock_sm",
    };
    return DefaultSensorNames;
}

////////////////////////////////////////////////////////////////////////////////

void TColumnarStatisticsConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enabled", &TThis::Enabled)
        .Default(false);

    registrar.Parameter("mode", &TThis::Mode)
        .Default(EColumnarStatisticsFetcherMode::Fallback);
}

////////////////////////////////////////////////////////////////////////////////

void TOperationSpecBase::Register(TRegistrar registrar)
{
    registrar.UnrecognizedStrategy(NYTree::EUnrecognizedStrategy::KeepRecursive);

    registrar.Parameter("intermediate_data_account", &TThis::IntermediateDataAccount)
        .Default("intermediate");
    registrar.Parameter("intermediate_compression_codec", &TThis::IntermediateCompressionCodec)
        .Default(NCompression::ECodec::Lz4);
    registrar.Parameter("intermediate_data_replication_factor", &TThis::IntermediateDataReplicationFactor)
        .Default(2);
    registrar.Parameter("intermediate_data_sync_on_close", &TThis::IntermediateDataSyncOnClose)
        .Default(false);
    registrar.Parameter("intermediate_data_medium", &TThis::IntermediateDataMediumName)
        .Default(NChunkClient::DefaultStoreMediumName);

    registrar.Parameter("debug_artifacts_account", &TThis::DebugArtifactsAccount)
        .Alias("job_node_account")
        .Default(NSecurityClient::TmpAccountName);

    registrar.Parameter("unavailable_chunk_strategy", &TThis::UnavailableChunkStrategy)
        .Default(EUnavailableChunkAction::Wait);
    registrar.Parameter("unavailable_chunk_tactics", &TThis::UnavailableChunkTactics)
        .Default(EUnavailableChunkAction::Wait);

    registrar.Parameter("max_data_weight_per_job", &TThis::MaxDataWeightPerJob)
        .Alias("max_data_size_per_job")
        .Default(200_GB)
        .GreaterThan(0);
    registrar.Parameter("max_primary_data_weight_per_job", &TThis::MaxPrimaryDataWeightPerJob)
        .Default(std::numeric_limits<i64>::max())
        .GreaterThan(0);

    registrar.Parameter("max_failed_job_count", &TThis::MaxFailedJobCount)
        .Default(10)
        .GreaterThanOrEqual(0)
        .LessThanOrEqual(10000);
    registrar.Parameter("max_stderr_count", &TThis::MaxStderrCount)
        .Default(10)
        .GreaterThanOrEqual(0)
        .LessThanOrEqual(150);
    registrar.Parameter("max_core_info_count", &TThis::MaxCoreInfoCount)
        .Default(10)
        .GreaterThanOrEqual(0)
        .LessThanOrEqual(150);

    registrar.Parameter("job_proxy_memory_overcommit_limit", &TThis::JobProxyMemoryOvercommitLimit)
        .Default()
        .GreaterThanOrEqual(0);

    registrar.Parameter("job_proxy_ref_counted_tracker_log_period", &TThis::JobProxyRefCountedTrackerLogPeriod)
        .Default(TDuration::Seconds(5));

    registrar.Parameter("title", &TThis::Title)
        .Default();

    registrar.Parameter("time_limit", &TThis::TimeLimit)
        .Default();

    registrar.Parameter("time_limit_job_fail_timeout", &TThis::TimeLimitJobFailTimeout)
        .Default(TDuration::Minutes(2));

    registrar.Parameter("owners", &TThis::Owners)
        .Default();

    registrar.Parameter("acl", &TThis::Acl)
        .Default();

    registrar.Parameter("add_authenticated_user_to_acl", &TThis::AddAuthenticatedUserToAcl)
        .Default(true);

    registrar.Parameter("secure_vault", &TThis::SecureVault)
        .Default();

    registrar.Parameter("enable_secure_vault_variables_in_job_shell", &TThis::EnableSecureVaultVariablesInJobShell)
        .Default(true);

    registrar.Parameter("suspend_operation_if_account_limit_exceeded", &TThis::SuspendOperationIfAccountLimitExceeded)
        .Default(false);

    registrar.Parameter("suspend_operation_after_materialization", &TThis::SuspendOperationAfterMaterialization)
        .Default(false);

    registrar.Parameter("min_locality_input_data_weight", &TThis::MinLocalityInputDataWeight)
        .GreaterThanOrEqual(0)
        .Default(1_GB);

    registrar.Parameter("auto_merge", &TThis::AutoMerge)
        .DefaultNew();

    registrar.Parameter("job_proxy_memory_digest", &TThis::JobProxyMemoryDigest)
        .DefaultCtor([] { 
            auto config = New<TLogDigestConfig>();
            config->LowerBound = 0.5;
            config->UpperBound = 2.0;
            config->DefaultValue = 1.0;
            return config;
        });

    registrar.Parameter("fail_on_job_restart", &TThis::FailOnJobRestart)
        .Default(false);

    registrar.Parameter("enable_job_splitting", &TThis::EnableJobSplitting)
        .Default(true);

    registrar.Parameter("slice_erasure_chunks_by_parts", &TThis::SliceErasureChunksByParts)
        .Default(false);

    registrar.Parameter("enable_legacy_live_preview", &TThis::EnableLegacyLivePreview)
        .Default();

    registrar.Parameter("started_by", &TThis::StartedBy)
        .Default();
    registrar.Parameter("annotations", &TThis::Annotations)
        .Default();

    // COMPAT(gritukan): Drop it in favor of `Annotations["description"]'.
    registrar.Parameter("description", &TThis::Description)
        .Default();

    registrar.Parameter("use_columnar_statistics", &TThis::UseColumnarStatistics)
        .Default(false);

    registrar.Parameter("ban_nodes_with_failed_jobs", &TThis::BanNodesWithFailedJobs)
        .Default(false);
    registrar.Parameter("ignore_job_failures_at_banned_nodes", &TThis::IgnoreJobFailuresAtBannedNodes)
        .Default(false);
    registrar.Parameter("fail_on_all_nodes_banned", &TThis::FailOnAllNodesBanned)
        .Default(true);

    registrar.Parameter("sampling", &TThis::Sampling)
        .DefaultNew();

    registrar.Parameter("alias", &TThis::Alias)
        .Default();

    registrar.Parameter("omit_inaccessible_columns", &TThis::OmitInaccessibleColumns)
        .Default(false);

    registrar.Parameter("additional_security_tags", &TThis::AdditionalSecurityTags)
        .Default();

    registrar.Parameter("waiting_job_timeout", &TThis::WaitingJobTimeout)
        .Default(std::nullopt)
        .GreaterThanOrEqual(TDuration::Seconds(10))
        .LessThanOrEqual(TDuration::Minutes(10));

    registrar.Parameter("job_speculation_timeout", &TThis::JobSpeculationTimeout)
        .Default()
        .GreaterThan(TDuration::Zero());

    registrar.Parameter("atomicity", &TThis::Atomicity)
        .Default(EAtomicity::Full);

    registrar.Parameter("job_cpu_monitor", &TThis::JobCpuMonitor)
        .DefaultNew();

    registrar.Parameter("enable_dynamic_store_read", &TThis::EnableDynamicStoreRead)
        .Default();

    registrar.Parameter("controller_agent_tag", &TThis::ControllerAgentTag)
        .Default("default");

    registrar.Parameter("job_shells", &TThis::JobShells)
        .Default();

    registrar.Parameter("job_splitter", &TThis::JobSplitter)
        .DefaultNew();

    registrar.Parameter("experiment_overrides", &TThis::ExperimentOverrides)
        .Default();

    registrar.Parameter("enable_trace_logging", &TThis::EnableTraceLogging)
        .Default(false);

    registrar.Parameter("input_table_columnar_statistics", &TThis::InputTableColumnarStatistics)
        .DefaultNew();
    registrar.Parameter("user_file_columnar_statistics", &TThis::UserFileColumnarStatistics)
        .DefaultNew();

    registrar.Parameter("enabled_profilers", &TThis::EnabledProfilers)
        .Default();
    registrar.Parameter("profiling_probability", &TThis::ProfilingProbability)
        .Default();
    registrar.Parameter("force_job_proxy_tracing", &TThis::ForceJobProxyTracing)
        .Default(false);
    registrar.Parameter("suspend_on_job_failure", &TThis::SuspendOnJobFailure)
        .Default(false);

    registrar.Parameter("job_testing_options", &TThis::JobTestingOptions)
        .Default();

    registrar.Parameter("enable_squashfs", &TThis::EnableSquashFS)
        .Default();

    registrar.Postprocessor([] (TOperationSpecBase* spec) {
        if (spec->UnavailableChunkStrategy == EUnavailableChunkAction::Wait &&
            spec->UnavailableChunkTactics == EUnavailableChunkAction::Skip)
        {
            THROW_ERROR_EXCEPTION("Your tactics conflicts with your strategy, Luke!");
        }

        if (spec->SecureVault) {
            for (const auto& name : spec->SecureVault->GetKeys()) {
                ValidateEnvironmentVariableName(name);
            }
        }

        if (spec->Alias && !spec->Alias->StartsWith(OperationAliasPrefix)) {
            THROW_ERROR_EXCEPTION("Operation alias should start with %Qv", OperationAliasPrefix)
                << TErrorAttribute("operation_alias", spec->Alias);
        }

        constexpr int MaxAnnotationsYsonTextLength = 10_KB;
        if (ConvertToYsonString(spec->Annotations, EYsonFormat::Text).AsStringBuf().size() > MaxAnnotationsYsonTextLength) {
            THROW_ERROR_EXCEPTION("Length of annotations YSON text representation should not exceed %v",
                MaxAnnotationsYsonTextLength);
        }

        ValidateOperationAcl(spec->Acl);
        ProcessAclAndOwnersParameters(&spec->Acl, &spec->Owners);
        ValidateSecurityTags(spec->AdditionalSecurityTags);

        {
            THashSet<TString> jobShellNames;
            for (const auto& jobShell : spec->JobShells) {
                if (!jobShellNames.emplace(jobShell->Name).second) {
                    THROW_ERROR_EXCEPTION("Job shell names should be distinct")
                        << TErrorAttribute("duplicate_shell_name", jobShell->Name);
                }
            }
        }

        if (spec->UseColumnarStatistics) {
            spec->InputTableColumnarStatistics->Enabled = true;
            spec->UserFileColumnarStatistics->Enabled = true;
        }

        if (spec->ProfilingProbability && *spec->ProfilingProbability * spec->EnabledProfilers.size() > 1.0) {
            THROW_ERROR_EXCEPTION("Profiling probability is too high");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TTaskOutputStreamConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("schema", &TThis::Schema)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

void TUserJobSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("task_title", &TThis::TaskTitle)
        .Default();
    registrar.Parameter("file_paths", &TThis::FilePaths)
        .Default();
    registrar.Parameter("layer_paths", &TThis::LayerPaths)
        .Default();
    registrar.Parameter("format", &TThis::Format)
        .Default();
    registrar.Parameter("input_format", &TThis::InputFormat)
        .Default();
    registrar.Parameter("output_format", &TThis::OutputFormat)
        .Default();
    registrar.Parameter("output_streams", &TThis::OutputStreams)
        .Default();
    registrar.Parameter("enable_input_table_index", &TThis::EnableInputTableIndex)
        .Default();
    registrar.Parameter("environment", &TThis::Environment)
        .Default();
    registrar.Parameter("cpu_limit", &TThis::CpuLimit)
        .Default(1)
        .GreaterThanOrEqual(0);
    registrar.Parameter("gpu_limit", &TThis::GpuLimit)
        .Default(0)
        .GreaterThanOrEqual(0);
    registrar.Parameter("port_count", &TThis::PortCount)
        .Default(0)
        .GreaterThanOrEqual(0)
        .LessThanOrEqual(50);
    registrar.Parameter("job_time_limit", &TThis::JobTimeLimit)
        .Alias("exec_time_limit")
        .Default()
        .GreaterThanOrEqual(TDuration::Seconds(1));
    registrar.Parameter("prepare_time_limit", &TThis::PrepareTimeLimit)
        .Default(TDuration::Minutes(45))
        .GreaterThanOrEqual(TDuration::Minutes(1));
    registrar.Parameter("memory_limit", &TThis::MemoryLimit)
        .Default(512_MB)
        .GreaterThan(0)
        .LessThanOrEqual(1_TB);
    registrar.Parameter("memory_reserve_factor", &TThis::MemoryReserveFactor)
        .Default();
    registrar.Parameter("user_job_memory_digest_default_value", &TThis::UserJobMemoryDigestDefaultValue)
        .Default(0.5)
        .GreaterThan(0.)
        .LessThanOrEqual(1.);
    registrar.Parameter("user_job_memory_digest_lower_bound", &TThis::UserJobMemoryDigestLowerBound)
        .Default(0.05)
        .GreaterThan(0.)
        .LessThanOrEqual(1.);
    registrar.Parameter("include_memory_mapped_files", &TThis::IncludeMemoryMappedFiles)
        .Default(true);
    registrar.Parameter("use_yamr_descriptors", &TThis::UseYamrDescriptors)
        .Default(false);
    registrar.Parameter("check_input_fully_consumed", &TThis::CheckInputFullyConsumed)
        .Default(false);
    registrar.Parameter("max_stderr_size", &TThis::MaxStderrSize)
        .Default(5_MB)
        .GreaterThan(0)
        .LessThanOrEqual(1_GB);

    registrar.Parameter("custom_statistics_count_limit", &TThis::CustomStatisticsCountLimit)
        .Default(128)
        .GreaterThan(0)
        .LessThanOrEqual(1024);
    registrar.Parameter("tmpfs_size", &TThis::TmpfsSize)
        .Default()
        .GreaterThan(0);
    registrar.Parameter("tmpfs_path", &TThis::TmpfsPath)
        .Default();
    registrar.Parameter("tmpfs_volumes", &TThis::TmpfsVolumes)
        .Default();
    registrar.Parameter("disk_space_limit", &TThis::DiskSpaceLimit)
        .Default()
        .GreaterThanOrEqual(0);
    registrar.Parameter("inode_limit", &TThis::InodeLimit)
        .Default()
        .GreaterThanOrEqual(0);
    registrar.Parameter("disk_request", &TThis::DiskRequest)
        .Default();
    registrar.Parameter("copy_files", &TThis::CopyFiles)
        .Default(false);
    registrar.Parameter("deterministic", &TThis::Deterministic)
        .Default(false);
    registrar.Parameter("use_porto_memory_tracking", &TThis::UsePortoMemoryTracking)
        .Default(false);
    registrar.Parameter("set_container_cpu_limit", &TThis::SetContainerCpuLimit)
        .Default(false);
    registrar.Parameter("force_core_dump", &TThis::ForceCoreDump)
        .Default(false);
    registrar.Parameter("interruption_signal", &TThis::InterruptionSignal)
        .Default();
    registrar.Parameter("signal_root_process_only", &TThis::SignalRootProcessOnly)
        .Default(false);
    registrar.Parameter("restart_exit_code", &TThis::RestartExitCode)
        .Default();
    registrar.Parameter("enable_gpu_layers", &TThis::EnableGpuLayers)
        .Default(true);
    registrar.Parameter("cuda_toolkit_version", &TThis::CudaToolkitVersion)
        .Default();
    registrar.Parameter("gpu_check_layer_name", &TThis::GpuCheckLayerName)
        .Default();
    registrar.Parameter("gpu_check_binary_path", &TThis::GpuCheckBinaryPath)
        .Default();
    registrar.Parameter("gpu_check_binary_args", &TThis::GpuCheckBinaryArgs)
        .Default();
    registrar.Parameter("job_speculation_timeout", &TThis::JobSpeculationTimeout)
        .Default()
        .GreaterThan(TDuration::Zero());
    registrar.Parameter("network_project", &TThis::NetworkProject)
        .Default();
    registrar.Parameter("enable_porto", &TThis::EnablePorto)
        .Default();
    registrar.Parameter("fail_job_on_core_dump", &TThis::FailJobOnCoreDump)
        .Default(true);
    registrar.Parameter("make_rootfs_writable", &TThis::MakeRootFSWritable)
        .Default(false);
    registrar.Parameter("use_smaps_memory_tracker", &TThis::UseSMapsMemoryTracker)
        .Default(false);
    registrar.Parameter("monitoring", &TThis::Monitoring)
        .DefaultNew();

    registrar.Parameter("system_layer_path", &TThis::SystemLayerPath)
        .Default();

    registrar.Parameter("supported_profilers", &TThis::SupportedProfilers)
        .Default();

    registrar.Postprocessor([] (TUserJobSpec* spec) {
        if ((spec->TmpfsSize || spec->TmpfsPath) && !spec->TmpfsVolumes.empty()) {
            THROW_ERROR_EXCEPTION(
                "Options \"tmpfs_size\" and \"tmpfs_path\" cannot be specified "
                "simultaneously with \"tmpfs_volumes\"")
                << TErrorAttribute("tmpfs_size", spec->TmpfsSize)
                << TErrorAttribute("tmpfs_path", spec->TmpfsPath)
                << TErrorAttribute("tmpfs_volumes", spec->TmpfsVolumes);
        }

        if (spec->TmpfsPath) {
            auto volume = New<TTmpfsVolumeConfig>();
            volume->Size = spec->TmpfsSize ? *spec->TmpfsSize : spec->MemoryLimit;
            volume->Path = *spec->TmpfsPath;
            spec->TmpfsVolumes.push_back(volume);
            spec->TmpfsPath = std::nullopt;
            spec->TmpfsSize = std::nullopt;
        }

        i64 totalTmpfsSize = 0;
        for (const auto& volume : spec->TmpfsVolumes) {
            if (!NFS::IsPathRelativeAndInvolvesNoTraversal(volume->Path)) {
                THROW_ERROR_EXCEPTION("Tmpfs path %v does not point inside the sandbox directory",
                    volume->Path);
            }
            totalTmpfsSize += volume->Size;
        }

        // Memory reserve should greater than or equal to tmpfs_size (see YT-5518 for more details).
        if (totalTmpfsSize > spec->MemoryLimit) {
            THROW_ERROR_EXCEPTION("Total size of tmpfs volumes must be less than or equal to memory limit")
                << TErrorAttribute("tmpfs_size", totalTmpfsSize)
                << TErrorAttribute("memory_limit", spec->MemoryLimit);
        }

        for (int i = 0; i < std::ssize(spec->TmpfsVolumes); ++i) {
            for (int j = 0; j < std::ssize(spec->TmpfsVolumes); ++j) {
                if (i == j) {
                    continue;
                }

                auto lhsFsPath = TFsPath(spec->TmpfsVolumes[i]->Path);
                auto rhsFsPath = TFsPath(spec->TmpfsVolumes[j]->Path);
                if (lhsFsPath.IsSubpathOf(rhsFsPath)) {
                    THROW_ERROR_EXCEPTION("Path of tmpfs volume %Qv is prefix of other tmpfs volume %Qv",
                        spec->TmpfsVolumes[i]->Path,
                        spec->TmpfsVolumes[j]->Path);
                }
            }
        }

        if (spec->MemoryReserveFactor) {
            spec->UserJobMemoryDigestLowerBound = spec->UserJobMemoryDigestDefaultValue = *spec->MemoryReserveFactor;
        }

        auto memoryDigestLowerLimit = static_cast<double>(totalTmpfsSize) / spec->MemoryLimit;
        spec->UserJobMemoryDigestDefaultValue = std::min(
            1.0,
            std::max(spec->UserJobMemoryDigestDefaultValue, memoryDigestLowerLimit)
        );
        spec->UserJobMemoryDigestLowerBound = std::min(
            1.0,
            std::max(spec->UserJobMemoryDigestLowerBound, memoryDigestLowerLimit)
        );
        spec->UserJobMemoryDigestDefaultValue = std::max(spec->UserJobMemoryDigestLowerBound, spec->UserJobMemoryDigestDefaultValue);

        for (const auto& [variableName, _] : spec->Environment) {
            ValidateEnvironmentVariableName(variableName);
        }

        for (auto& path : spec->FilePaths) {
            path = path.Normalize();
        }

        if (!spec->DiskSpaceLimit && spec->InodeLimit) {
            THROW_ERROR_EXCEPTION("Option \"inode_limit\" can be specified only with \"disk_space_limit\"");
        }

        if (spec->DiskSpaceLimit && spec->DiskRequest) {
            THROW_ERROR_EXCEPTION(
                "Options \"disk_space_limit\" and \"inode_limit\" cannot be specified "
                "together with \"disk_request\"")
                << TErrorAttribute("disk_space_limit", spec->DiskSpaceLimit)
                << TErrorAttribute("inode_limit", spec->InodeLimit)
                << TErrorAttribute("disk_request", spec->DiskRequest);
        }

        if (spec->DiskSpaceLimit) {
            spec->DiskRequest = New<TDiskRequestConfig>();
            spec->DiskRequest->DiskSpace = *spec->DiskSpaceLimit;
            spec->DiskRequest->InodeCount = spec->InodeLimit;
            spec->DiskSpaceLimit = std::nullopt;
            spec->InodeLimit = std::nullopt;
        }

        if (spec->MakeRootFSWritable && spec->LayerPaths.empty()) {
            THROW_ERROR_EXCEPTION("Option \"make_rootfs_writable\" cannot be set without specifying \"layer_paths\"");
        }
    });
}

void TUserJobSpec::InitEnableInputTableIndex(int inputTableCount, TJobIOConfigPtr jobIOConfig)
{
    if (!EnableInputTableIndex) {
        EnableInputTableIndex = (inputTableCount != 1);
    }

    jobIOConfig->ControlAttributes->EnableTableIndex = *EnableInputTableIndex;
}

////////////////////////////////////////////////////////////////////////////////

void TOptionalUserJobSpec::Register(TRegistrar registrar)
{
    registrar.BaseClassParameter("command", &TUserJobSpec::Command)
        .Default();
}

bool TOptionalUserJobSpec::IsNontrivial() const
{
    return Command != TString();
}

////////////////////////////////////////////////////////////////////////////////

void TMandatoryUserJobSpec::Register(TRegistrar registrar)
{
    registrar.BaseClassParameter("command", &TUserJobSpec::Command);
}

////////////////////////////////////////////////////////////////////////////////

void TVanillaTaskSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("job_count", &TThis::JobCount)
        .GreaterThanOrEqual(1);
    registrar.Parameter("job_io", &TThis::JobIO)
        .DefaultNew();
    registrar.Parameter("output_table_paths", &TThis::OutputTablePaths)
        .Default();
    registrar.Parameter("restart_completed_jobs", &TThis::RestartCompletedJobs)
        .Default(false);

    registrar.Postprocessor([] (TVanillaTaskSpec* spec) {
        spec->OutputTablePaths = NYT::NYPath::Normalize(spec->OutputTablePaths);
    });
}

////////////////////////////////////////////////////////////////////////////////

void TInputlyQueryableSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("input_query", &TThis::InputQuery)
        .Default();
    registrar.Parameter("input_schema", &TThis::InputSchema)
        .Default();

    registrar.Postprocessor([] (TInputlyQueryableSpec* spec) {
        if (spec->InputSchema && !spec->InputQuery) {
            THROW_ERROR_EXCEPTION("Found \"input_schema\" without \"input_query\" in operation spec");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TOperationWithUserJobSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("stderr_table_path", &TThis::StderrTablePath)
        .Default();
    registrar.Parameter("stderr_table_writer", &TThis::StderrTableWriter)
        // TODO(babenko): deprecate this
        .Alias("stderr_table_writer_config")
        .DefaultNew();

    registrar.Parameter("core_table_path", &TThis::CoreTablePath)
        .Default();
    registrar.Parameter("core_table_writer", &TThis::CoreTableWriter)
        // TODO(babenko): deprecate this
        .Alias("core_table_writer_config")
        .DefaultNew();

    registrar.Parameter("enable_cuda_gpu_core_dump", &TThis::EnableCudaGpuCoreDump)
        .Default(false);

    registrar.Postprocessor([] (TOperationWithUserJobSpec* spec) {
        if (spec->StderrTablePath) {
            *spec->StderrTablePath = spec->StderrTablePath->Normalize();
        }

        if (spec->CoreTablePath) {
            *spec->CoreTablePath = spec->CoreTablePath->Normalize();
        }

        if (spec->EnableCudaGpuCoreDump && !spec->CoreTablePath) {
            THROW_ERROR_EXCEPTION("\"enable_cuda_gpu_core_dump\" option requires \"core_table_path\" options to be set");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TSimpleOperationSpecBase::Register(TRegistrar registrar)
{
    registrar.Parameter("data_weight_per_job", &TThis::DataWeightPerJob)
        .Alias("data_size_per_job")
        .Default()
        .GreaterThan(0);
    registrar.Parameter("job_count", &TThis::JobCount)
        .Default()
        .GreaterThan(0);
    registrar.Parameter("max_job_count", &TThis::MaxJobCount)
        .Default()
        .GreaterThan(0);
    registrar.Parameter("force_job_size_adjuster", &TThis::ForceJobSizeAdjuster)
        .Default(false);
    registrar.Parameter("locality_timeout", &TThis::LocalityTimeout)
        .Default(TDuration::Seconds(5));
    registrar.Parameter("job_io", &TThis::JobIO)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

void TUnorderedOperationSpecBase::Register(TRegistrar registrar)
{
    registrar.Parameter("input_table_paths", &TThis::InputTablePaths)
        .NonEmpty();

    registrar.Preprocessor([] (TUnorderedOperationSpecBase* spec) {
        spec->JobIO->TableReader->MaxBufferSize = 256_MB;
    });

    registrar.Preprocessor([] (TUnorderedOperationSpecBase* spec) {
        spec->InputTablePaths = NYT::NYPath::Normalize(spec->InputTablePaths);
    });
}

////////////////////////////////////////////////////////////////////////////////

void TMapOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("mapper", &TThis::Mapper)
        .DefaultNew();
    registrar.Parameter("output_table_paths", &TThis::OutputTablePaths)
        .Default();
    registrar.Parameter("ordered", &TThis::Ordered)
        .Default(false);

    registrar.Postprocessor([] (TMapOperationSpec* spec) {
        spec->OutputTablePaths = NYT::NYPath::Normalize(spec->OutputTablePaths);

        spec->Mapper->InitEnableInputTableIndex(spec->InputTablePaths.size(), spec->JobIO);
        spec->Mapper->TaskTitle = "Mapper";

        ValidateNoOutputStreams(spec->Mapper, EOperationType::Map);
    });
}

////////////////////////////////////////////////////////////////////////////////

void TUnorderedMergeOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("output_table_path", &TThis::OutputTablePath);
    registrar.Parameter("combine_chunks", &TThis::CombineChunks)
        .Default(false);
    registrar.Parameter("force_transform", &TThis::ForceTransform)
        .Default(false);
    registrar.Parameter("schema_inference_mode", &TThis::SchemaInferenceMode)
        .Default(ESchemaInferenceMode::Auto);

    registrar.Postprocessor([] (TUnorderedMergeOperationSpec* spec) {
        spec->OutputTablePath = spec->OutputTablePath.Normalize();
    });
}

////////////////////////////////////////////////////////////////////////////////

void TMergeOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("input_table_paths", &TThis::InputTablePaths)
        .NonEmpty();
    registrar.Parameter("output_table_path", &TThis::OutputTablePath);
    registrar.Parameter("mode", &TThis::Mode)
        .Default(EMergeMode::Unordered);
    registrar.Parameter("combine_chunks", &TThis::CombineChunks)
        .Default(false);
    registrar.Parameter("force_transform", &TThis::ForceTransform)
        .Default(false);
    registrar.Parameter("merge_by", &TThis::MergeBy)
        .Default();
    registrar.Parameter("schema_inference_mode", &TThis::SchemaInferenceMode)
        .Default(ESchemaInferenceMode::Auto);

    registrar.Postprocessor([] (TMergeOperationSpec* spec) {
        spec->InputTablePaths = NYT::NYPath::Normalize(spec->InputTablePaths);
        spec->OutputTablePath = spec->OutputTablePath.Normalize();
    });
}

////////////////////////////////////////////////////////////////////////////////

void TEraseOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("table_path", &TThis::TablePath);
    registrar.Parameter("combine_chunks", &TThis::CombineChunks)
        .Default(false);
    registrar.Parameter("schema_inference_mode", &TThis::SchemaInferenceMode)
        .Default(ESchemaInferenceMode::Auto);

    registrar.Postprocessor([&] (TEraseOperationSpec* spec) {
        spec->TablePath = spec->TablePath.Normalize();
    });
}

////////////////////////////////////////////////////////////////////////////////

void TSortedOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("use_new_sorted_pool", &TThis::UseNewSortedPool)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

void TReduceOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("reducer", &TThis::Reducer)
        .DefaultNew();
    registrar.Parameter("input_table_paths", &TThis::InputTablePaths)
        .NonEmpty();
    registrar.Parameter("output_table_paths", &TThis::OutputTablePaths)
        .Default();

    registrar.Parameter("reduce_by", &TThis::ReduceBy)
        .Default();
    registrar.Parameter("sort_by", &TThis::SortBy)
        .Default();
    registrar.Parameter("join_by", &TThis::JoinBy)
        .Default();

    registrar.Parameter("enable_key_guarantee", &TThis::EnableKeyGuarantee)
        .Default();

    registrar.Parameter("pivot_keys", &TThis::PivotKeys)
        .Default();

    registrar.Parameter("validate_key_column_types", &TThis::ValidateKeyColumnTypes)
        .Default(true);

    registrar.Parameter("consider_only_primary_size", &TThis::ConsiderOnlyPrimarySize)
        .Default(false);

    registrar.Parameter("slice_foreign_chunks", &TThis::SliceForeignChunks)
        .Default(false);

    registrar.Postprocessor([] (TReduceOperationSpec* spec) {
        NTableClient::ValidateSortColumns(spec->JoinBy);
        NTableClient::ValidateSortColumns(spec->ReduceBy);
        NTableClient::ValidateSortColumns(spec->SortBy);

        spec->InputTablePaths = NYT::NYPath::Normalize(spec->InputTablePaths);
        spec->OutputTablePaths = NYT::NYPath::Normalize(spec->OutputTablePaths);

        bool hasPrimary = false;
        for (const auto& path : spec->InputTablePaths) {
            hasPrimary |= path.GetPrimary();
        }
        if (hasPrimary) {
            for (auto& path: spec->InputTablePaths) {
                path.Attributes().Set("foreign", !path.GetPrimary());
                path.Attributes().Remove("primary");
            }
        }

        spec->Reducer->InitEnableInputTableIndex(spec->InputTablePaths.size(), spec->JobIO);
        spec->Reducer->TaskTitle = "Reducer";

        ValidateNoOutputStreams(spec->Reducer, EOperationType::Reduce);
    });
}

////////////////////////////////////////////////////////////////////////////////

void TSortOperationSpecBase::Register(TRegistrar registrar)
{
    registrar.Parameter("input_table_paths", &TThis::InputTablePaths)
        .NonEmpty();
    registrar.Parameter("partition_count", &TThis::PartitionCount)
        .Default()
        .GreaterThan(0);
    registrar.Parameter("max_partition_factor", &TThis::MaxPartitionFactor)
        .Default()
        .GreaterThan(1);
    registrar.Parameter("partition_data_weight", &TThis::PartitionDataWeight)
        .Alias("partition_data_size")
        .Default()
        .GreaterThan(0);
    registrar.Parameter("data_weight_per_sort_job", &TThis::DataWeightPerShuffleJob)
        .Alias("data_size_per_sort_job")
        .Default(2_GB)
        .GreaterThan(0);
    registrar.Parameter("data_weight_per_intermediate_partition_job", &TThis::DataWeightPerIntermediatePartitionJob)
        .Default(2_GB)
        .GreaterThan(0);
    registrar.Parameter("max_chunk_slice_per_shuffle_job", &TThis::MaxChunkSlicePerShuffleJob)
        .Default(8000)
        .GreaterThan(0);
    registrar.Parameter("max_chunk_slice_per_intermediate_partition_job", &TThis::MaxChunkSlicePerIntermediatePartitionJob)
        .Default(8000)
        .GreaterThan(0);
    registrar.Parameter("shuffle_start_threshold", &TThis::ShuffleStartThreshold)
        .Default(0.75)
        .InRange(0.0, 1.0);
    registrar.Parameter("merge_start_threshold", &TThis::MergeStartThreshold)
        .Default(0.9)
        .InRange(0.0, 1.0);
    registrar.Parameter("sort_locality_timeout", &TThis::SortLocalityTimeout)
        .Default(TDuration::Seconds(5));
    registrar.Parameter("shuffle_network_limit", &TThis::ShuffleNetworkLimit)
        .Default(0);
    registrar.Parameter("max_shuffle_data_slice_count", &TThis::MaxShuffleDataSliceCount)
        .GreaterThan(0)
        .Default(10'000'000);
    registrar.Parameter("max_shuffle_job_count", &TThis::MaxShuffleJobCount)
        .GreaterThan(0)
        .Default(200'000);
    registrar.Parameter("max_merge_data_slice_count", &TThis::MaxMergeDataSliceCount)
        .GreaterThan(0)
        .Default(10'000'000);
    registrar.Parameter("sort_by", &TThis::SortBy)
        .Default();
    registrar.Parameter("enable_partitioned_data_balancing", &TThis::EnablePartitionedDataBalancing)
        .Default(true);
    registrar.Parameter("enable_intermediate_output_recalculation", &TThis::EnableIntermediateOutputRecalculation)
        .Default(true);
    registrar.Parameter("pivot_keys", &TThis::PivotKeys)
        .Default();
    registrar.Parameter("use_new_partitions_heuristic", &TThis::UseNewPartitionsHeuristic)
        .Default(false);
    registrar.Parameter("partition_size_factor", &TThis::PartitionSizeFactor)
        .GreaterThan(0)
        .LessThanOrEqual(1)
        .Default(0.7);
    registrar.Parameter("use_new_sorted_pool", &TThis::UseNewSortedPool)
        .Default(false);
    registrar.Parameter("new_partitions_heuristic_probability", &TThis::NewPartitionsHeuristicProbability)
        .Default(0)
        .InRange(0, 256);

    registrar.Postprocessor([] (TSortOperationSpecBase* spec) {
        NTableClient::ValidateSortColumns(spec->SortBy);

        spec->InputTablePaths = NYT::NYPath::Normalize(spec->InputTablePaths);

        // Validate pivot_keys.
        for (const auto& pivotKey : spec->PivotKeys) {
            try {
                for (const auto& value : pivotKey) {
                    ValidateDataValueType(value.Type);
                }
            } catch (const std::exception& ex) {
                THROW_ERROR_EXCEPTION(TError("Pivot keys are invalid") << ex);
            }

            if (pivotKey.GetCount() > std::ssize(spec->SortBy)) {
                THROW_ERROR_EXCEPTION("Pivot key cannot be longer than sort_by")
                    << TErrorAttribute("key", pivotKey)
                    << TErrorAttribute("sort_by", spec->SortBy);
            }
        }

        auto sortComparator = GetComparator(spec->SortBy);
        for (int index = 0; index + 1 < std::ssize(spec->PivotKeys); ++index) {
            const auto& upperBound = TKeyBound::FromRow() < spec->PivotKeys[index];
            const auto& nextUpperBound = TKeyBound::FromRow() < spec->PivotKeys[index + 1];
            if (sortComparator.CompareKeyBounds(upperBound, nextUpperBound) >= 0) {
                THROW_ERROR_EXCEPTION("Pivot keys should should form a strictly increasing sequence")
                    << TErrorAttribute("pivot_key", spec->PivotKeys[index])
                    << TErrorAttribute("next_pivot_key", spec->PivotKeys[index + 1])
                    << TErrorAttribute("comparator", sortComparator);
            }
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TSortOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("output_table_path", &TThis::OutputTablePath);
    registrar.Parameter("samples_per_partition", &TThis::SamplesPerPartition)
        .Default(1000)
        .GreaterThan(1);
    registrar.BaseClassParameter("partition_job_io", &TSortOperationSpec::PartitionJobIO)
        .DefaultNew();
    registrar.BaseClassParameter("sort_job_io", &TSortOperationSpec::SortJobIO)
        .DefaultNew();
    registrar.BaseClassParameter("merge_job_io", &TSortOperationSpec::MergeJobIO)
        .DefaultNew();

    // Provide custom names for shared settings.
    registrar.BaseClassParameter("partition_job_count", &TSortOperationSpec::PartitionJobCount)
        .Default()
        .GreaterThan(0);
    registrar.BaseClassParameter("data_weight_per_partition_job", &TSortOperationSpec::DataWeightPerPartitionJob)
        .Alias("data_size_per_partition_job")
        .Default()
        .GreaterThan(0);
    registrar.BaseClassParameter("simple_sort_locality_timeout", &TSortOperationSpec::SimpleSortLocalityTimeout)
        .Default(TDuration::Seconds(5));
    registrar.BaseClassParameter("simple_merge_locality_timeout", &TSortOperationSpec::SimpleMergeLocalityTimeout)
        .Default(TDuration::Seconds(5));
    registrar.BaseClassParameter("partition_locality_timeout", &TSortOperationSpec::PartitionLocalityTimeout)
        .Default(TDuration::Seconds(5));
    registrar.BaseClassParameter("merge_locality_timeout", &TSortOperationSpec::MergeLocalityTimeout)
        .Default(TDuration::Minutes(1));

    registrar.Parameter("max_input_data_weight", &TThis::MaxInputDataWeight)
        .GreaterThan(0)
        .Default(5_PB);

    registrar.Parameter("schema_inference_mode", &TThis::SchemaInferenceMode)
        .Default(ESchemaInferenceMode::Auto);

    registrar.BaseClassParameter("data_weight_per_sorted_merge_job", &TSortOperationSpec::DataWeightPerSortedJob)
        .Alias("data_size_per_sorted_merge_job")
        .Default();

    registrar.Preprocessor([] (TSortOperationSpec* spec) {
        spec->PartitionJobIO->TableReader->MaxBufferSize = 1_GB;
        spec->PartitionJobIO->TableWriter->MaxBufferSize = 2_GB;

        spec->SortJobIO->TableReader->MaxBufferSize = 1_GB;
        spec->SortJobIO->TableReader->RetryCount = 3;
        spec->SortJobIO->TableReader->PassCount = 50;

        // Output slices must be small enough to make reasonable jobs in sorted chunk pool.
        spec->SortJobIO->TableWriter->DesiredChunkWeight = 256_MB;
        spec->MergeJobIO->TableReader->RetryCount = 3;
        spec->MergeJobIO->TableReader->PassCount = 50;

        spec->MapSelectivityFactor = 1.0;
    });

    registrar.Postprocessor([&] (TSortOperationSpec* spec) {
        spec->OutputTablePath = spec->OutputTablePath.Normalize();

        if (spec->SortBy.empty()) {
            THROW_ERROR_EXCEPTION("\"sort_by\" option should be set in Sort operations");
        }

        if (spec->Sampling && spec->Sampling->SamplingRate) {
            THROW_ERROR_EXCEPTION("Sampling in sort operation is not supported");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TMapReduceOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("output_table_paths", &TThis::OutputTablePaths)
        .NonEmpty();
    registrar.Parameter("reduce_by", &TThis::ReduceBy)
        .Default();
    // Mapper can be absent -- leave it null by default.
    registrar.Parameter("mapper", &TThis::Mapper)
        .Default();
    // ReduceCombiner can be absent -- leave it null by default.
    registrar.Parameter("reduce_combiner", &TThis::ReduceCombiner)
        .Default();
    registrar.Parameter("reducer", &TThis::Reducer)
        .DefaultNew();
    registrar.BaseClassParameter("map_job_io", &TMapReduceOperationSpec::PartitionJobIO)
        .DefaultNew();
    registrar.BaseClassParameter("sort_job_io", &TMapReduceOperationSpec::SortJobIO)
        .DefaultNew();
    registrar.BaseClassParameter("reduce_job_io", &TMapReduceOperationSpec::MergeJobIO)
        .DefaultNew();

    registrar.Parameter("mapper_output_table_count", &TThis::MapperOutputTableCount)
        .Default(0)
        .GreaterThanOrEqual(0);

    // Provide custom names for shared settings.
    registrar.BaseClassParameter("map_job_count", &TMapReduceOperationSpec::PartitionJobCount)
        .Default()
        .GreaterThan(0);
    registrar.BaseClassParameter("data_weight_per_map_job", &TMapReduceOperationSpec::DataWeightPerPartitionJob)
        .Alias("data_size_per_map_job")
        .Default()
        .GreaterThan(0);
    registrar.BaseClassParameter("map_locality_timeout", &TMapReduceOperationSpec::PartitionLocalityTimeout)
        .Default(TDuration::Seconds(5));
    registrar.BaseClassParameter("reduce_locality_timeout", &TMapReduceOperationSpec::MergeLocalityTimeout)
        .Default(TDuration::Minutes(1));
    registrar.BaseClassParameter("map_selectivity_factor", &TMapReduceOperationSpec::MapSelectivityFactor)
        .Default(1.0)
        .GreaterThan(0);

    registrar.BaseClassParameter("data_weight_per_reduce_job", &TMapReduceOperationSpec::DataWeightPerSortedJob)
        .Alias("data_size_per_reduce_job")
        .Default();

    registrar.Parameter("force_reduce_combiners", &TThis::ForceReduceCombiners)
        .Default(false);

    registrar.Parameter("ordered", &TThis::Ordered)
        .Default(false);

    registrar.Parameter("enable_table_index_if_has_trivial_mapper", &TThis::EnableTableIndexIfHasTrivialMapper)
        .Default(false);

    // The following settings are inherited from base but make no sense for map-reduce:
    //   SimpleSortLocalityTimeout
    //   SimpleMergeLocalityTimeout
    //   MapSelectivityFactor

    registrar.Preprocessor([] (TMapReduceOperationSpec* spec) {
        spec->PartitionJobIO->TableReader->MaxBufferSize = 256_MB;
        spec->PartitionJobIO->TableWriter->MaxBufferSize = 2_GB;

        spec->SortJobIO->TableReader->MaxBufferSize = 1_GB;
        // Output slices must be small enough to make reasonable jobs in sorted chunk pool.
        spec->SortJobIO->TableWriter->DesiredChunkWeight = 256_MB;

        spec->SortJobIO->TableReader->RetryCount = 3;
        spec->SortJobIO->TableReader->PassCount = 50;

        spec->MergeJobIO->TableReader->RetryCount = 3;
        spec->MergeJobIO->TableReader->PassCount = 50 ;
    });

    registrar.Postprocessor([] (TMapReduceOperationSpec* spec) {
        auto throwError = [] (NTableClient::EControlAttribute attribute, const TString& jobType) {
            THROW_ERROR_EXCEPTION(
                "%Qlv control attribute is not supported by %Qlv jobs in map-reduce operation",
                attribute,
                jobType);
        };
        auto validateControlAttributes = [&] (const NFormats::TControlAttributesConfigPtr& attributes, const TString& jobType) {
            if (attributes->EnableRowIndex) {
                throwError(NTableClient::EControlAttribute::RowIndex, jobType);
            }
            if (attributes->EnableRangeIndex) {
                throwError(NTableClient::EControlAttribute::RangeIndex, jobType);
            }
            if (attributes->EnableTabletIndex) {
                throwError(NTableClient::EControlAttribute::TabletIndex, jobType);
            }
        };
        if (spec->ForceReduceCombiners && !spec->HasNontrivialReduceCombiner()) {
            THROW_ERROR_EXCEPTION("Found \"force_reduce_combiners\" without nontrivial \"reduce_combiner\" in operation spec");
        }
        validateControlAttributes(spec->MergeJobIO->ControlAttributes, "reduce");
        validateControlAttributes(spec->SortJobIO->ControlAttributes, "reduce_combiner");

        if (spec->HasNontrivialReduceCombiner()) {
            if (spec->MergeJobIO->ControlAttributes->EnableTableIndex != spec->SortJobIO->ControlAttributes->EnableTableIndex) {
                THROW_ERROR_EXCEPTION("%Qlv control attribute must be the same for \"reduce\" and \"reduce_combiner\" jobs",
                    NTableClient::EControlAttribute::TableIndex);
            }
        }

        if (!spec->ReduceBy.empty()) {
            NTableClient::ValidateKeyColumns(spec->ReduceBy);
        }

        if (spec->ReduceBy.empty()) {
            spec->ReduceBy = GetColumnNames(spec->SortBy);
        }

        if (spec->SortBy.empty()) {
            for (const auto& reduceColumn : spec->ReduceBy) {
                spec->SortBy.push_back(TColumnSortSchema{
                    .Name = reduceColumn,
                    .SortOrder = ESortOrder::Ascending
                });
            }
        }

        if (spec->ReduceBy.empty() && spec->SortBy.empty()) {
            THROW_ERROR_EXCEPTION("At least one of the \"sort_by\" or \"reduce_by\" fields shold be specified");
        }

        if (spec->HasNontrivialMapper()) {
            for (const auto& stream : spec->Mapper->OutputStreams) {
                if (stream->Schema->GetSortColumns() != spec->SortBy) {
                    THROW_ERROR_EXCEPTION("Schemas of mapper output streams should have exactly "
                        "\"sort_by\" sort column prefix")
                        << TErrorAttribute("violating_schema", stream->Schema)
                        << TErrorAttribute("sort_by", spec->SortBy);
                }
                auto sortColumnNames = GetColumnNames(spec->SortBy);
                const auto& firstStream = spec->Mapper->OutputStreams.front();
                if (*stream->Schema->Filter(sortColumnNames) != *firstStream->Schema->Filter(sortColumnNames)) {
                    THROW_ERROR_EXCEPTION("Key columns of mapper output streams should have the same names and types")
                        << TErrorAttribute("lhs_schema", firstStream->Schema)
                        << TErrorAttribute("rhs_schema", stream->Schema);
                }
            }
        }

        spec->InputTablePaths = NYT::NYPath::Normalize(spec->InputTablePaths);
        spec->OutputTablePaths = NYT::NYPath::Normalize(spec->OutputTablePaths);

        if (!spec->HasNontrivialMapper() && spec->EnableTableIndexIfHasTrivialMapper) {
            spec->Reducer->EnableInputTableIndex = true;
            if (spec->HasNontrivialReduceCombiner()) {
                spec->ReduceCombiner->EnableInputTableIndex = true;
            }
        }

        if (spec->HasNontrivialMapper()) {
            spec->Mapper->InitEnableInputTableIndex(spec->InputTablePaths.size(), spec->PartitionJobIO);
            spec->Mapper->TaskTitle = "Mapper";
        }

        auto intermediateStreamCount = 1;
        if (spec->HasNontrivialMapper()) {
            if (!spec->Mapper->OutputStreams.empty()) {
                intermediateStreamCount = spec->Mapper->OutputStreams.size();
            }
        } else {
            if (spec->Reducer->EnableInputTableIndex.value_or(false) ||
                spec->MergeJobIO->ControlAttributes->EnableTableIndex)
            {
                intermediateStreamCount = spec->InputTablePaths.size();
            }
        }

        if (spec->HasNontrivialReduceCombiner()) {
            if (intermediateStreamCount > 1) {
                THROW_ERROR_EXCEPTION("Nontrivial reduce combiner is not allowed with several intermediate streams");
            }
            spec->ReduceCombiner->InitEnableInputTableIndex(intermediateStreamCount, spec->SortJobIO);
            spec->ReduceCombiner->TaskTitle = "Reduce combiner";
        }

        spec->Reducer->InitEnableInputTableIndex(intermediateStreamCount, spec->MergeJobIO);
        spec->Reducer->TaskTitle = "Reducer";

        if (intermediateStreamCount > 1) {
            if (!spec->HasNontrivialMapper()) {
                spec->PartitionJobIO->ControlAttributes->EnableTableIndex = true;
            }
            spec->MergeJobIO->ControlAttributes->EnableTableIndex = true;
            spec->SortJobIO->ControlAttributes->EnableTableIndex = true;
        }

        if (spec->Sampling && spec->Sampling->SamplingRate) {
            spec->MapSelectivityFactor *= *spec->Sampling->SamplingRate;
        }

        if (spec->MapperOutputTableCount >= std::ssize(spec->OutputTablePaths) ||
            (spec->HasNontrivialMapper() && !spec->Mapper->OutputStreams.empty() && spec->MapperOutputTableCount >= std::ssize(spec->Mapper->OutputStreams)))
        {
            THROW_ERROR_EXCEPTION(
                "There should be at least one non-mapper output table; maybe you need Map operation instead?")
                << TErrorAttribute("mapper_output_table_count", spec->MapperOutputTableCount)
                << TErrorAttribute("output_table_count", spec->OutputTablePaths.size())
                << TErrorAttribute("mapper_output_stream_count", spec->Mapper->OutputStreams.size());
        }
    });
}

bool TMapReduceOperationSpec::HasNontrivialMapper() const
{
    return Mapper && Mapper->IsNontrivial();
}

bool TMapReduceOperationSpec::HasNontrivialReduceCombiner() const
{
    return ReduceCombiner && ReduceCombiner->IsNontrivial();
}

bool TMapReduceOperationSpec::HasSchemafulIntermediateStreams() const
{
    if (HasNontrivialMapper()) {
        return !Mapper->OutputStreams.empty();
    }
    return MergeJobIO->ControlAttributes->EnableTableIndex;
}

////////////////////////////////////////////////////////////////////////////////

void TRemoteCopyOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("cluster_name", &TThis::ClusterName)
        .Default();
    registrar.Parameter("input_table_paths", &TThis::InputTablePaths)
        .NonEmpty();
    registrar.Parameter("output_table_path", &TThis::OutputTablePath);
    registrar.Parameter("network_name", &TThis::NetworkName)
        .Default();
    registrar.Parameter("networks", &TThis::Networks)
        .Alias("network_names")
        .Default();
    registrar.Parameter("cluster_connection", &TThis::ClusterConnection)
        .Default();
    registrar.Parameter("max_chunk_count_per_job", &TThis::MaxChunkCountPerJob)
        .Default(1000);
    registrar.Parameter("copy_attributes", &TThis::CopyAttributes)
        .Default(false);
    registrar.Parameter("attribute_keys", &TThis::AttributeKeys)
        .Default();
    registrar.Parameter("concurrency", &TThis::Concurrency)
        .Default(4);
    registrar.Parameter("block_buffer_size", &TThis::BlockBufferSize)
        .Default(64_MB);
    registrar.Parameter("schema_inference_mode", &TThis::SchemaInferenceMode)
        .Default(ESchemaInferenceMode::Auto);
    registrar.Parameter("delay_in_copy_chunk", &TThis::DelayInCopyChunk)
        .Default(TDuration::Zero());
    registrar.Parameter("erasure_chunk_repair_delay", &TThis::ErasureChunkRepairDelay)
        .Default(TDuration::Minutes(15));
    registrar.Parameter("repair_erasure_chunks", &TThis::RepairErasureChunks)
        .Default(false);

    registrar.Preprocessor([] (TRemoteCopyOperationSpec* spec) {
        // NB: in remote copy operation chunks are never decompressed,
        // so the data weight does not affect anything.
        spec->MaxDataWeightPerJob = std::numeric_limits<i64>::max();
    });
    registrar.Postprocessor([] (TRemoteCopyOperationSpec* spec) {
        if (spec->InputTablePaths.size() > 1) {
            THROW_ERROR_EXCEPTION("Multiple tables in remote copy are not supported");
        }

        spec->InputTablePaths = NYPath::Normalize(spec->InputTablePaths);
        spec->OutputTablePath = spec->OutputTablePath.Normalize();

        if (spec->NetworkName) {
            if (spec->Networks) {
                THROW_ERROR_EXCEPTION("Options \"network_name\" and \"networks\"/\"network_names\" cannot be specified simultaneously");
            }
            spec->Networks = {*spec->NetworkName};
            spec->NetworkName.reset();
        }

        if (!spec->ClusterName && !spec->ClusterConnection) {
            THROW_ERROR_EXCEPTION("Neither cluster name nor cluster connection specified");
        }

        if (spec->Sampling && spec->Sampling->SamplingRate) {
            THROW_ERROR_EXCEPTION("You do not want sampling in remote copy operation :)");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TVanillaOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("tasks", &TThis::Tasks)
        .NonEmpty();

    registrar.Postprocessor([] (TVanillaOperationSpec* spec) {
        for (const auto& [taskName, taskSpec] : spec->Tasks) {
            if (taskName.empty()) {
                THROW_ERROR_EXCEPTION("Empty task names are not allowed");
            }

            taskSpec->TaskTitle = taskName;

            ValidateNoOutputStreams(taskSpec, EOperationType::Vanilla);
        }

        if (spec->Sampling && spec->Sampling->SamplingRate) {
            THROW_ERROR_EXCEPTION("You do not want sampling in vanilla operation :)");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TJobResourcesConfig::Register(TRegistrar registrar)
{
    registrar.BaseClassParameter("user_slots", &TJobResourcesConfig::UserSlots)
        .Default()
        .GreaterThanOrEqual(0);
    registrar.BaseClassParameter("cpu", &TJobResourcesConfig::Cpu)
        .Default()
        .GreaterThanOrEqual(0);
    registrar.BaseClassParameter("network", &TJobResourcesConfig::Network)
        .Default()
        .GreaterThanOrEqual(0);
    registrar.BaseClassParameter("memory", &TJobResourcesConfig::Memory)
        .Default()
        .GreaterThanOrEqual(0);
    registrar.BaseClassParameter("gpu", &TJobResourcesConfig::Gpu)
        .Default()
        .GreaterThanOrEqual(0);
}

TJobResourcesConfigPtr TJobResourcesConfig::Clone()
{
    auto result = New<TJobResourcesConfig>();
    ForEachResource([&result, this] (auto NVectorHdrf::TJobResourcesConfig::* resourceDataMember, EJobResourceType /*resourceType*/) {
        result.Get()->*resourceDataMember = this->*resourceDataMember;
    });
    return result;
}

TJobResourcesConfigPtr TJobResourcesConfig::operator-()
{
    auto result = New<TJobResourcesConfig>();
    ForEachResource([&result, this] (auto NVectorHdrf::TJobResourcesConfig::* resourceDataMember, EJobResourceType /*resourceType*/) {
        if ((this->*resourceDataMember).has_value()) {
            result.Get()->*resourceDataMember = -*(this->*resourceDataMember);
        }
    });
    return result;
}

////////////////////////////////////////////////////////////////////////////////

void TCommonPreemptionConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable_aggressive_starvation", &TThis::EnableAggressiveStarvation)
        .Default();
}

void TPoolPreemptionConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("fair_share_starvation_timeout", &TThis::FairShareStarvationTimeout)
        .Alias("fair_share_preemption_timeout")
        .Default();
    registrar.Parameter("fair_share_starvation_tolerance", &TThis::FairShareStarvationTolerance)
        .InRange(0.0, 1.0)
        .Default();

    registrar.Parameter("allow_aggressive_preemption", &TThis::AllowAggressivePreemption)
        .Alias("allow_aggressive_starvation_preemption")
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

void TSchedulableConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("weight", &TThis::Weight)
        .Default()
        .InRange(MinSchedulableWeight, MaxSchedulableWeight);

    registrar.Parameter("max_share_ratio", &TThis::MaxShareRatio)
        .Default()
        .InRange(0.0, 1.0);
    registrar.Parameter("resource_limits", &TThis::ResourceLimits)
        .DefaultNew();

    registrar.Parameter("strong_guarantee_resources", &TThis::StrongGuaranteeResources)
        .Alias("min_share_resources")
        .DefaultNew();
    registrar.Parameter("scheduling_tag_filter", &TThis::SchedulingTagFilter)
        .Alias("scheduling_tag")
        .Default();

    registrar.Postprocessor([&] (TSchedulableConfig* config) {
        if (config->SchedulingTagFilter.Size() > MaxSchedulingTagRuleCount) {
            THROW_ERROR_EXCEPTION("Specifying more than %v tokens in scheduling tag filter is not allowed",
                 MaxSchedulingTagRuleCount);
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TExtendedSchedulableConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("pool", &TThis::Pool)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

void TEphemeralSubpoolConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("mode", &TThis::Mode)
        .Default(ESchedulingMode::FairShare);

    registrar.Parameter("max_running_operation_count", &TThis::MaxRunningOperationCount)
        .Default(10);

    registrar.Parameter("max_operation_count", &TThis::MaxOperationCount)
        .Default(10);

    registrar.Parameter("resource_limits", &TThis::ResourceLimits)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

void TPoolIntegralGuaranteesConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("guarantee_type", &TThis::GuaranteeType)
        .Default(EIntegralGuaranteeType::None);

    registrar.Parameter("burst_guarantee_resources", &TThis::BurstGuaranteeResources)
        .DefaultNew();

    registrar.Parameter("resource_flow", &TThis::ResourceFlow)
        .DefaultNew();

    registrar.Parameter("relaxed_share_multiplier_limit", &TThis::RelaxedShareMultiplierLimit)
        .InRange(1, 10)
        .Default();

    registrar.Parameter("can_accept_free_volume", &TThis::CanAcceptFreeVolume)
        .Default(true);

    registrar.Parameter("should_distribute_free_volume_among_children", &TThis::ShouldDistributeFreeVolumeAmongChildren)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

void TPoolConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("mode", &TThis::Mode)
        .Default(ESchedulingMode::FairShare);

    registrar.Parameter("max_running_operation_count", &TThis::MaxRunningOperationCount)
        .Default()
        .GreaterThanOrEqual(0);

    registrar.Parameter("max_operation_count", &TThis::MaxOperationCount)
        .Default()
        .GreaterThanOrEqual(0);

    registrar.Parameter("fifo_sort_parameters", &TThis::FifoSortParameters)
        .Default({EFifoSortParameter::Weight, EFifoSortParameter::StartTime})
        .NonEmpty();

    registrar.Parameter("forbid_immediate_operations", &TThis::ForbidImmediateOperations)
        .Default(false);

    registrar.Parameter("create_ephemeral_subpools", &TThis::CreateEphemeralSubpools)
        .Default(false);

    registrar.Parameter("ephemeral_subpool_config", &TThis::EphemeralSubpoolConfig)
        .DefaultNew();

    registrar.Parameter("infer_children_weights_from_historic_usage", &TThis::InferChildrenWeightsFromHistoricUsage)
        .Default(false);
    registrar.Parameter("historic_usage_config", &TThis::HistoricUsageConfig)
        .DefaultNew();

    registrar.Parameter("allowed_profiling_tags", &TThis::AllowedProfilingTags)
        .Default();

    registrar.Parameter("enable_by_user_profiling", &TThis::EnableByUserProfiling)
        .Default();

    registrar.Parameter("abc", &TThis::Abc)
        .Default();

    registrar.Parameter("integral_guarantees", &TThis::IntegralGuarantees)
        .DefaultNew();

    registrar.Parameter("enable_detailed_logs", &TThis::EnableDetailedLogs)
        .Default(false);

    registrar.Parameter("config_preset", &TThis::ConfigPreset)
        .Default();

    registrar.Parameter("enable_fair_share_truncation_in_fifo_pool", &TThis::EnableFairShareTruncationInFifoPool)
        .Alias("truncate_fifo_pool_unsatisfied_child_fair_share")
        .Default();

    registrar.Parameter("metering_tags", &TThis::MeteringTags)
        .Default();
}

void TPoolConfig::Validate(const TString& poolName)
{
    Postprocess();

    if (MaxOperationCount && MaxRunningOperationCount && *MaxOperationCount < *MaxRunningOperationCount) {
        THROW_ERROR_EXCEPTION("%Qv must be greater that or equal to %Qv, but %v < %v",
            "max_operation_count",
            "max_running_operation_count",
            *MaxOperationCount,
            *MaxRunningOperationCount)
            << TErrorAttribute("pool_name", poolName);
    }
    if (AllowedProfilingTags.size() > MaxAllowedProfilingTagCount) {
        THROW_ERROR_EXCEPTION("Limit for the number of allowed profiling tags exceeded")
            << TErrorAttribute("allowed_profiling_tag_count", AllowedProfilingTags.size())
            << TErrorAttribute("max_allowed_profiling_tag_count", MaxAllowedProfilingTagCount)
            << TErrorAttribute("pool_name", poolName);
    }
    if (IntegralGuarantees->BurstGuaranteeResources->IsNonTrivial() &&
        IntegralGuarantees->GuaranteeType == EIntegralGuaranteeType::Relaxed)
    {
        THROW_ERROR_EXCEPTION("Burst guarantees cannot be specified for integral guarantee type \"relaxed\"")
            << TErrorAttribute("integral_guarantee_type", IntegralGuarantees->GuaranteeType)
            << TErrorAttribute("burst_guarantee_resources", IntegralGuarantees->BurstGuaranteeResources)
            << TErrorAttribute("pool_name", poolName);
    }
    if (!MeteringTags.empty() && !Abc) {
        THROW_ERROR_EXCEPTION("Metering tags can be specified only for pool with specified abc attribute")
            << TErrorAttribute("pool_name", poolName);
    }
}

////////////////////////////////////////////////////////////////////////////////

void TFairShareStrategyPackingConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable", &TThis::Enable)
        .Default(false);

    registrar.Parameter("metric", &TThis::Metric)
        .Default(EPackingMetricType::AngleLength);

    registrar.Parameter("max_better_past_snapshots", &TThis::MaxBetterPastSnapshots)
        .Default(2);
    registrar.Parameter("absolute_metric_value_tolerance", &TThis::AbsoluteMetricValueTolerance)
        .Default(0.05)
        .GreaterThanOrEqual(0.0);
    registrar.Parameter("relative_metric_value_tolerance", &TThis::RelativeMetricValueTolerance)
        .Default(1.5)
        .GreaterThanOrEqual(1.0);
    registrar.Parameter("min_window_size_for_schedule", &TThis::MinWindowSizeForSchedule)
        .Default(0)
        .GreaterThanOrEqual(0);
    registrar.Parameter("max_heartbeat_window_size", &TThis::MaxHearbeatWindowSize)
        .Default(10);
    registrar.Parameter("max_heartbeat_age", &TThis::MaxHeartbeatAge)
        .Default(TDuration::Hours(1));
}

////////////////////////////////////////////////////////////////////////////////

static constexpr int MaxSchedulingSegmentModuleCount = 8;

void TStrategyOperationSpec::Register(TRegistrar registrar)
{
    registrar.Parameter("pool", &TThis::Pool)
        .Default();
    registrar.Parameter("scheduling_options_per_pool_tree", &TThis::SchedulingOptionsPerPoolTree)
        .Alias("fair_share_options_per_pool_tree")
        .Default();
    registrar.Parameter("pool_trees", &TThis::PoolTrees)
        .Default();
    registrar.Parameter("max_concurrent_schedule_job_calls", &TThis::MaxConcurrentControllerScheduleJobCalls)
        .Alias("max_concurrent_controller_schedule_job_calls")
        .Default();
    registrar.Parameter("schedule_in_single_tree", &TThis::ScheduleInSingleTree)
        .Default(false);
    registrar.Parameter("tentative_pool_trees", &TThis::TentativePoolTrees)
        .Default();
    registrar.Parameter("use_default_tentative_pool_trees", &TThis::UseDefaultTentativePoolTrees)
        .Default(false);
    registrar.Parameter("tentative_tree_eligibility", &TThis::TentativeTreeEligibility)
        .DefaultNew();
    registrar.Parameter("update_preemptable_jobs_list_logging_period", &TThis::UpdatePreemptableJobsListLoggingPeriod)
        .Default(1000);
    registrar.Parameter("custom_profiling_tag", &TThis::CustomProfilingTag)
        .Default();
    registrar.Parameter("max_unpreemptable_job_count", &TThis::MaxUnpreemptableRunningJobCount)
        .Default();
    registrar.Parameter("max_speculative_job_count_per_task", &TThis::MaxSpeculativeJobCountPerTask)
        .Default(10);
    registrar.Parameter("max_probing_job_count_per_task", &TThis::MaxProbingJobCountPerTask)
        .Default(10);
    registrar.Parameter("probing_ratio", &TThis::ProbingRatio)
        .InRange(0, 1)
        .Default();
    registrar.Parameter("probing_pool_tree", &TThis::ProbingPoolTree)
        .Default();

    // TODO(ignat): move it to preemption settings.
    registrar.Parameter("preemption_mode", &TThis::PreemptionMode)
        .Default(EPreemptionMode::Normal);
    registrar.Parameter("scheduling_segment", &TThis::SchedulingSegment)
        .Default();
    registrar.Parameter("scheduling_segment_modules", &TThis::SchedulingSegmentModules)
        .Alias("scheduling_segment_data_centers")
        .Default();
    registrar.Parameter("enable_limiting_ancestor_check", &TThis::EnableLimitingAncestorCheck)
        .Default(true);
    registrar.Parameter("is_gang", &TThis::IsGang)
        .Default(false);
    registrar.Parameter("testing", &TThis::TestingOperationOptions)
        .DefaultNew();

    registrar.Postprocessor([] (TStrategyOperationSpec* spec) {
        if (spec->SchedulingSegmentModules && spec->SchedulingSegmentModules->size() >= MaxSchedulingSegmentModuleCount) {
            THROW_ERROR_EXCEPTION("%Qv size must be not greater than %v",
                "scheduling_segment_modules",
                MaxSchedulingSegmentModuleCount);
        }
        if (spec->ScheduleInSingleTree && (spec->TentativePoolTrees || spec->UseDefaultTentativePoolTrees)) {
            THROW_ERROR_EXCEPTION("%Qv option cannot be used simultaneously with tentative pool trees (check %Qv and %Qv)",
                "schedule_in_single_tree",
                "tentative_pool_trees",
                "use_default_tentative_pool_trees");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TOperationFairShareTreeRuntimeParameters::Register(TRegistrar registrar)
{
    registrar.Parameter("weight", &TThis::Weight)
        .Optional()
        .InRange(MinSchedulableWeight, MaxSchedulableWeight);
    registrar.Parameter("pool", &TThis::Pool);
    registrar.Parameter("resource_limits", &TThis::ResourceLimits)
        .DefaultNew();
    registrar.Parameter("enable_detailed_logs", &TThis::EnableDetailedLogs)
        .Default(false);
    registrar.Parameter("tentative", &TThis::Tentative)
        .Default(false);
    registrar.Parameter("probing", &TThis::Probing)
        .Default(false);
    registrar.Parameter("scheduling_segment_module", &TThis::SchedulingSegmentModule)
        .Alias("scheduling_segment_data_center")
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

void SerializeHeavyRuntimeParameters(NYTree::TFluentMap fluent, const TOperationRuntimeParameters& parameters)
{
    fluent
        .OptionalItem("annotations", parameters.Annotations);
}

void Serialize(const TOperationRuntimeParameters& parameters, IYsonConsumer* consumer, bool serializeHeavy)
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("owners").Value(parameters.Owners)
            .Item("acl").Value(parameters.Acl)
            .Item("scheduling_options_per_pool_tree").Value(parameters.SchedulingOptionsPerPoolTree)
            .DoIf(serializeHeavy, [&] (auto fluent) {
                SerializeHeavyRuntimeParameters(fluent, parameters);
            })
            .Item("erased_trees").Value(parameters.ErasedTrees)
            .Item("controller_agent_tag").Value(parameters.ControllerAgentTag)
        .EndMap();
}

void Serialize(const TOperationRuntimeParametersPtr& parameters, IYsonConsumer* consumer, bool serializeHeavy)
{
    if (!parameters) {
        consumer->OnEntity();
    } else {
        Serialize(*parameters, consumer, serializeHeavy);
    }
}

void Deserialize(TOperationRuntimeParameters& parameters, INodePtr node)
{
    auto mapNode = node->AsMap();
    if (auto owners = mapNode->FindChild("owners")) {
        Deserialize(parameters.Owners, owners);
    }
    if (auto acl = mapNode->FindChild("acl")) {
        Deserialize(parameters.Acl, acl);
    }
    parameters.SchedulingOptionsPerPoolTree = ConvertTo<THashMap<TString, TOperationFairShareTreeRuntimeParametersPtr>>(
        mapNode->GetChildOrThrow("scheduling_options_per_pool_tree"));
    if (auto annotations = mapNode->FindChild("annotations")) {
        Deserialize(parameters.Annotations, annotations);
    }
    if (auto erasedTrees = mapNode->FindChild("erased_trees")) {
        Deserialize(parameters.ErasedTrees, erasedTrees);
    }
    if (auto controllerAgentTag = mapNode->FindChild("controller_agent_tag")) {
        Deserialize(parameters.ControllerAgentTag, controllerAgentTag);
    } else {
        parameters.ControllerAgentTag = "default";
    }
    ValidateOperationAcl(parameters.Acl);
    ProcessAclAndOwnersParameters(&parameters.Acl, &parameters.Owners);
}

void Deserialize(TOperationRuntimeParameters& parameters, TYsonPullParserCursor* cursor)
{
    Deserialize(parameters, ExtractTo<INodePtr>(cursor));
}

////////////////////////////////////////////////////////////////////////////////

void TOperationFairShareTreeRuntimeParametersUpdate::Register(TRegistrar registrar)
{
    registrar.Parameter("weight", &TThis::Weight)
        .Optional()
        .InRange(MinSchedulableWeight, MaxSchedulableWeight);
    registrar.Parameter("pool", &TThis::Pool)
        .Optional();
    registrar.Parameter("resource_limits", &TThis::ResourceLimits)
        .Default();
    registrar.Parameter("enable_detailed_logs", &TThis::EnableDetailedLogs)
        .Optional();

    registrar.Postprocessor([] (TOperationFairShareTreeRuntimeParametersUpdate* update) {
        if (update->Pool.has_value()) {
            ValidatePoolName(update->Pool->ToString());
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TOperationRuntimeParametersUpdate::Register(TRegistrar registrar)
{
    registrar.Parameter("pool", &TThis::Pool)
        .Optional();
    registrar.Parameter("weight", &TThis::Weight)
        .Optional()
        .InRange(MinSchedulableWeight, MaxSchedulableWeight);
    registrar.Parameter("acl", &TThis::Acl)
        .Optional();
    registrar.Parameter("scheduling_options_per_pool_tree", &TThis::SchedulingOptionsPerPoolTree)
        .Default();
    registrar.Parameter("annotations", &TThis::Annotations)
        .Optional();
    registrar.Parameter("controller_agent_tag", &TThis::ControllerAgentTag)
        .Optional();

    registrar.Postprocessor([] (TOperationRuntimeParametersUpdate* update) {
        if (update->Acl.has_value()) {
            ValidateOperationAcl(*update->Acl);
        }
        if (update->Pool.has_value()) {
            ValidatePoolName(*update->Pool);
        }
    });
}

bool TOperationRuntimeParametersUpdate::ContainsPool() const
{
    auto result = Pool.has_value();
    for (const auto& [_, treeOptions] : SchedulingOptionsPerPoolTree) {
        result |= treeOptions->Pool.has_value();
    }
    return result;
}

EPermissionSet TOperationRuntimeParametersUpdate::GetRequiredPermissions() const
{
    auto requiredPermissions = EPermissionSet(EPermission::Manage);

    for (const auto& [poolTree, treeParams] : SchedulingOptionsPerPoolTree) {
        if (treeParams->EnableDetailedLogs.has_value()) {
            requiredPermissions |= EPermission::Administer;
            break;
        }
    }

    return requiredPermissions;
}

////////////////////////////////////////////////////////////////////////////////

TOperationFairShareTreeRuntimeParametersPtr UpdateFairShareTreeRuntimeParameters(
    const TOperationFairShareTreeRuntimeParametersPtr& origin,
    const TOperationFairShareTreeRuntimeParametersUpdatePtr& update)
{
    try {
        if (!origin) {
            return NYTree::ConvertTo<TOperationFairShareTreeRuntimeParametersPtr>(ConvertToNode(update));
        }
        return UpdateYsonStruct(origin, ConvertToNode(update));
    } catch (const std::exception& exception) {
        THROW_ERROR_EXCEPTION("Error updating operation fair share tree runtime parameters")
            << exception;
    }
}

void TSchedulerConnectionConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("rpc_timeout", &TThis::RpcTimeout)
        .Default(TDuration::Seconds(60));
    registrar.Parameter("rpc_acknowledgement_timeout", &TThis::RpcAcknowledgementTimeout)
        .Default(TDuration::Seconds(15));
}

////////////////////////////////////////////////////////////////////////////////

void TJobCpuMonitorConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable_cpu_reclaim", &TThis::EnableCpuReclaim)
        .Default(false);

    registrar.Parameter("check_period", &TThis::CheckPeriod)
        .Default(TDuration::Seconds(1));

    registrar.Parameter("start_delay", &TThis::StartDelay)
        .Default(TDuration::Zero());

    registrar.Parameter("smoothing_factor", &TThis::SmoothingFactor)
        .InRange(0, 1)
        .Default(0.1);

    registrar.Parameter("relative_upper_bound", &TThis::RelativeUpperBound)
        .InRange(0, 1)
        .Default(0.9);

    registrar.Parameter("relative_lower_bound", &TThis::RelativeLowerBound)
        .InRange(0, 1)
        .Default(0.6);

    registrar.Parameter("increase_coefficient", &TThis::IncreaseCoefficient)
        .InRange(1, 2)
        .Default(1.45);

    registrar.Parameter("decrease_coefficient", &TThis::DecreaseCoefficient)
        .InRange(0, 1)
        .Default(0.97);

    registrar.Parameter("vote_window_size", &TThis::VoteWindowSize)
        .GreaterThan(0)
        .Default(5);

    registrar.Parameter("vote_decision_threshold", &TThis::VoteDecisionThreshold)
        .GreaterThan(0)
        .Default(3);

    registrar.Parameter("min_cpu_limit", &TThis::MinCpuLimit)
        .InRange(0, 1)
        .Default(1);
}

////////////////////////////////////////////////////////////////////////////////

TPoolPresetConfig::TPoolPresetConfig()
{
    SetUnrecognizedStrategy(NYTree::EUnrecognizedStrategy::KeepRecursive);
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_DYNAMIC_PHOENIX_TYPE(TEraseOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TMapOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TMapReduceOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TMergeOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TSortedOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TReduceOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TOperationSpecBase);
DEFINE_DYNAMIC_PHOENIX_TYPE(TOrderedMergeOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TRemoteCopyOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TSimpleOperationSpecBase);
DEFINE_DYNAMIC_PHOENIX_TYPE(TSortedMergeOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TSortOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TSortOperationSpecBase);
DEFINE_DYNAMIC_PHOENIX_TYPE(TStrategyOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TUnorderedMergeOperationSpec);
DEFINE_DYNAMIC_PHOENIX_TYPE(TUnorderedOperationSpecBase);
DEFINE_DYNAMIC_PHOENIX_TYPE(TVanillaOperationSpec);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
