#include "config.h"

#include <yt/yt/ytlib/api/native/config.h>

#include <yt/yt/client/security_client/public.h>

#include <yt/yt/library/auth/auth.h>

#include <yt/yt/core/ytree/fluent.h>

#include <array>
#include <utility>

namespace NYT::NYqlAgent {

using namespace NSecurityClient;
using namespace NAuth;
using namespace NYTree;

namespace {

////////////////////////////////////////////////////////////////////////////////

constexpr auto DefaultGatewaySettings = std::to_array<std::pair<TStringBuf, TStringBuf>>({
    {"DefaultCalcMemoryLimit", "1G"},
    {"EvaluationTableSizeLimit", "1M"},
    {"DefaultMaxJobFails", "5"},
    {"DefaultMemoryLimit", "512m"},
    {"MapJoinLimit", "2048m"},
    {"ClientMapTimeout", "10s"},
    {"MapJoinShardCount", "4"},
    {"CommonJoinCoreLimit", "128m"},
    {"CombineCoreLimit", "128m"},
    {"SwitchLimit", "128m"},
    {"JoinMergeTablesLimit", "64"},
    {"DataSizePerJob", "1g"},
    {"MaxJobCount", "16384"},
    {"PublishedCompressionCodec", "zstd_5"},
    {"TemporaryCompressionCodec", "zstd_5"},
    {"PublishedErasureCodec", "none"},
    {"TemporaryErasureCodec", "none"},
    {"OptimizeFor", "scan"},
    {"PythonCpu", "4.0"},
    {"JavascriptCpu", "4.0"},
    {"ErasureCodecCpu", "5.0"},
    {"AutoMerge", "relaxed"},
    {"QueryCacheMode", "normal"},
    {"QueryCacheSalt", "YQL-10935"},
    {"UseSkiff", "1"},
    {"MaxInputTables", "1000"},
    {"MaxInputTablesForSortedMerge", "100"},
    {"MaxOutputTables", "50"},
    {"InferSchemaTableCountThreshold", "50"},
    {"MaxExtraJobMemoryToFuseOperations", "3g"},
    {"UseColumnarStatistics", "auto"},
    {"ParallelOperationsLimit", "16"},
    {"ReleaseTempData", "immediate"},
    {"LookupJoinLimit", "1M"},
    {"LookupJoinMaxRows", "900"},
    {"MaxReplicationFactorToFuseOperations", "20.0"},
    {"LLVMMemSize", "256M"},
    {"LLVMPerNodeMemSize", "10K"},
    {"JoinEnableStarJoin", "true"},
    {"FolderInlineItemsLimit", "200"},
    {"FolderInlineDataLimit", "500K"},
    {"WideFlowLimit", "101"},
    {"NativeYtTypeCompatibility", "complex,date,null,void,date,float,json,decimal"},
    {"MapJoinUseFlow", "1"},
    {"HybridDqDataSizeLimitForOrdered", "384M"},
    {"HybridDqDataSizeLimitForUnordered", "8G"},
    {"UseYqlRowSpecCompactForm", "false"},
    {"_UseKeyBoundApi", "false"},
    {"UseNewPredicateExtraction", "true"},
    {"PruneKeyFilterLambda", "true"},
    {"JoinCommonUseMapMultiOut", "true"},
    {"UseAggPhases", "true"},
    {"EnforceJobUtc", "true"},
    {"_ForceJobSizeAdjuster", "true"},
    {"_EnableWriteReorder", "true"},
    {"_EnableYtPartitioning", "true"},
    {"HybridDqExecution", "true"},
    {"DQRPCReaderInflight", "1"},
});

constexpr auto DefaultDqGatewaySettings = std::to_array<std::pair<TStringBuf, TStringBuf>>({
    {"EnableComputeActor", "1"},
    {"ComputeActorType", "async"},
    {"EnableStrip", "true"},
    {"EnableInsert", "true"},
    {"ChannelBufferSize", "1000000"},
    {"PullRequestTimeoutMs", "3000000"},
    {"PingTimeoutMs", "30000"},
    {"MaxTasksPerOperation", "100"},
    {"MaxTasksPerStage", "30"},
    {"AnalyzeQuery", "true"},
    {"EnableFullResultWrite", "true"},
    {"_FallbackOnRuntimeErrors", "DQ computation exceeds the memory limit,requirement data.GetRaw().size(),_Unwind_Resume,Cannot load time zone"},
    {"MemoryLimit", "3G"},
    {"_EnablePrecompute", "1"},
    {"UseAggPhases", "true"},
    {"UseWideChannels", "true"},
});

constexpr auto DefaultClusterSettings = std::to_array<std::pair<TStringBuf, TStringBuf>>({
    {"QueryCacheChunkLimit", "100000"},
    {"_UseKeyBoundApi", "true"},
});

////////////////////////////////////////////////////////////////////////////////

IListNodePtr MergeDefaultSettings(const IListNodePtr& settings, const auto& defaults)
{
    auto result = CloneNode(settings)->AsList();

    THashSet<TString> presentSettings;
    for (const auto& setting : settings->GetChildren()) {
        presentSettings.insert(setting->AsMap()->GetChildOrThrow("name")->GetValue<TString>());
    }

    for (const auto& [name, value] : defaults) {
        if (presentSettings.contains(name)) {
            continue;
        }
        auto setting = BuildYsonNodeFluently()
            .BeginMap()
                .Item("name").Value(name)
                .Item("value").Value(value)
            .EndMap();
        result->AddChild(std::move(setting));
    }

    return result;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

void TVanillaJobFile::Register(TRegistrar registrar)
{
    registrar.Parameter("name", &TThis::Name)
        .NonEmpty();
    registrar.Parameter("local_path", &TThis::LocalPath)
        .NonEmpty();
}

void TDqYtBackend::Register(TRegistrar registrar)
{
    registrar.Parameter("cluster_name", &TThis::ClusterName)
        .Default();
    registrar.Parameter("jobs_per_operation", &TThis::JobsPerOperation)
        .Default(5);
    registrar.Parameter("max_jobs", &TThis::MaxJobs)
        .Default(150);
    registrar.Parameter("vanilla_job_lite", &TThis::VanillaJobLite)
        .Default();
    registrar.Parameter("vanilla_job_command", &TThis::VanillaJobCommand)
        .Default("./dq_vanilla_job");
    registrar.Parameter("vanilla_job_file", &TThis::VanillaJobFiles)
        .Default();
    registrar.Parameter("prefix", &TThis::Prefix)
        .Default("//sys/yql_agent/dq/data");
    registrar.Parameter("upload_replication_factor", &TThis::UploadReplicationFactor)
        .Default(7);
    registrar.Parameter("token_file", &TThis::TokenFile)
        .Default();
    registrar.Parameter("user", &TThis::User)
        .Default();
    registrar.Parameter("pool", &TThis::Pool)
        .Default();
    registrar.Parameter("pool_trees", &TThis::PoolTrees)
        .Default({});
    registrar.Parameter("owner", &TThis::Owner)
        .Default({YqlAgentUserName});
    registrar.Parameter("cpu_limit", &TThis::CpuLimit)
        .Default(6);
    registrar.Parameter("worker_capacity", &TThis::WorkerCapacity)
        .Default(24);
    registrar.Parameter("memory_limit", &TThis::MemoryLimit)
        .Default(64424509440);
    registrar.Parameter("cache_size", &TThis::CacheSize)
        .Default(6000000000);
    registrar.Parameter("use_tmp_fs", &TThis::UseTmpFs)
        .Default(true);
    registrar.Parameter("network_project", &TThis::NetworkProject)
        .Default("");
    registrar.Parameter("can_use_compute_actor", &TThis::CanUseComputeActor)
        .Default(true);
    registrar.Parameter("enforce_job_utc", &TThis::EnforceJobUtc)
        .Default(true);
}

void TDqYtCoordinator::Register(TRegistrar registrar)
{
    registrar.Parameter("cluster_name", &TThis::ClusterName)
        .Default();
    registrar.Parameter("prefix", &TThis::Prefix)
        .Default("//sys/yql_agent/dq_coord");
    registrar.Parameter("token_file", &TThis::TokenFile)
        .Default();
    registrar.Parameter("user", &TThis::User)
        .Default();
    registrar.Parameter("debug_log_file", &TThis::DebugLogFile)
        .Default();
}

void TDqManagerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("interconnect_port", &TThis::InterconnectPort)
        .Default();
    registrar.Parameter("grpc_port", &TThis::GrpcPort)
        .Default();
    registrar.Parameter("actor_threads", &TThis::ActorThreads)
        .Default(4);
    registrar.Parameter("use_ipv4", &TThis::UseIPv4)
        .Default(false);

    registrar.Parameter("yt_backends", &TThis::YtBackends)
        .Default();
    registrar.Parameter("yt_coordinator", &TThis::YtCoordinator)
        .DefaultNew();
    registrar.Parameter("interconnect_settings", &TThis::ICSettings)
        .Default(GetEphemeralNodeFactory()->CreateMap());
}

////////////////////////////////////////////////////////////////////////////////

IListNodePtr TYqlPluginConfig::MergeClusterDefaultSettings(const IListNodePtr& clusterConfigSettings)
{
    return MergeDefaultSettings(clusterConfigSettings, DefaultClusterSettings);
}

void TYqlPluginConfig::Register(TRegistrar registrar)
{
    auto defaultRemoteFilePatterns = BuildYsonNodeFluently()
        .BeginList()
            .Item().BeginMap()
                .Item("pattern").Value("yt://([a-zA-Z0-9\\-_]+)/([^&@?]+)$")
                .Item("cluster").Value("$1")
                .Item("path").Value("$2")
            .EndMap()
        .EndList();

    registrar.Parameter("gateway_config", &TThis::GatewayConfig)
        .Default(GetEphemeralNodeFactory()->CreateMap());
    registrar.Parameter("dq_gateway_config", &TThis::DqGatewayConfig)
        .Default(GetEphemeralNodeFactory()->CreateMap());
    registrar.Parameter("file_storage_config", &TThis::FileStorageConfig)
        .Default(GetEphemeralNodeFactory()->CreateMap());
    registrar.Parameter("operation_attributes", &TThis::OperationAttributes)
        .Default(GetEphemeralNodeFactory()->CreateMap());
    registrar.Parameter("yt_token_path", &TThis::YTTokenPath)
        .Default();
    registrar.Parameter("yql_plugin_shared_library", &TThis::YqlPluginSharedLibrary)
        .Default();
    registrar.Parameter("dq_manager_config", &TThis::DqManagerConfig)
        .DefaultNew();
    registrar.Parameter("enable_dq", &TThis::EnableDq)
        .Default(false);

    registrar.Postprocessor([=] (TThis* config) {
        auto gatewayConfig = config->GatewayConfig->AsMap();
        gatewayConfig->AddChild("remote_file_patterns", defaultRemoteFilePatterns);
        gatewayConfig->AddChild("mr_job_bin", BuildYsonNodeFluently().Value("./mrjob"));
        gatewayConfig->AddChild("yt_log_level", BuildYsonNodeFluently().Value("YL_DEBUG"));
        gatewayConfig->AddChild("execute_udf_locally_if_possible", BuildYsonNodeFluently().Value(true));

        auto fileStorageConfig = config->FileStorageConfig->AsMap();
        fileStorageConfig->AddChild("max_files", BuildYsonNodeFluently().Value(1 << 13));
        fileStorageConfig->AddChild("max_size_mb", BuildYsonNodeFluently().Value(1 << 14));
        fileStorageConfig->AddChild("retry_count", BuildYsonNodeFluently().Value(3));

        auto gatewaySettings = gatewayConfig->FindChild("default_settings");
        if (gatewaySettings) {
            gatewayConfig->RemoveChild(gatewaySettings);
        } else {
            gatewaySettings = GetEphemeralNodeFactory()->CreateList();
        }
        gatewaySettings = MergeDefaultSettings(gatewaySettings->AsList(), DefaultGatewaySettings);
        YT_VERIFY(gatewayConfig->AddChild("default_settings", std::move(gatewaySettings)));

        gatewayConfig->AddChild("cluster_mapping", GetEphemeralNodeFactory()->CreateList());
        for (const auto& cluster : gatewayConfig->GetChildOrThrow("cluster_mapping")->AsList()->GetChildren()) {
            auto clusterMap = cluster->AsMap();
            auto settings = clusterMap->FindChild("settings");
            if (settings) {
                clusterMap->RemoveChild(settings);
            } else {
                settings = GetEphemeralNodeFactory()->CreateList();
            }
            YT_VERIFY(clusterMap->AddChild("settings", MergeClusterDefaultSettings(settings->AsList())));
        }

        auto dqGatewayConfig = config->DqGatewayConfig->AsMap();
        auto dqGatewaySettings = dqGatewayConfig->FindChild("default_settings");
        if (dqGatewaySettings) {
            dqGatewayConfig->RemoveChild(dqGatewaySettings);
        } else {
            dqGatewaySettings = GetEphemeralNodeFactory()->CreateList();
        }
        dqGatewaySettings = MergeDefaultSettings(dqGatewaySettings->AsList(), DefaultDqGatewaySettings);
        YT_VERIFY(dqGatewayConfig->AddChild("default_settings", std::move(dqGatewaySettings)));

        dqGatewayConfig->AddChild("default_auto_percentage", BuildYsonNodeFluently().Value(100));

        auto icSettingsConfig = config->DqManagerConfig->ICSettings->AsMap();
        auto closeOnIdleMs = icSettingsConfig->FindChild("close_on_idle_ms");
        if (!closeOnIdleMs) {
            icSettingsConfig->AddChild("close_on_idle_ms", BuildYsonNodeFluently().Value(0));
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TYqlAgentConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("bus_client", &TThis::BusClient)
        .DefaultNew();
    registrar.Parameter("yql_thread_count", &TThis::YqlThreadCount)
        .Default(256);
}

////////////////////////////////////////////////////////////////////////////////

void TYqlAgentDynamicConfig::Register(TRegistrar /*registrar*/)
{ }

////////////////////////////////////////////////////////////////////////////////

void TYqlAgentServerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("yql_agent", &TThis::YqlAgent)
        .DefaultNew();
    registrar.Parameter("abort_on_unrecognized_options", &TThis::AbortOnUnrecognizedOptions)
        .Default(false);
    registrar.Parameter("user", &TThis::User)
        .Default(YqlAgentUserName);
    registrar.Parameter("cypress_annotations", &TThis::CypressAnnotations)
        .Default(BuildYsonNodeFluently()
            .BeginMap()
            .EndMap()
        ->AsMap());
    registrar.Parameter("root", &TThis::Root)
        .Default("//sys/yql_agent");
    registrar.Parameter("dynamic_config_manager", &TThis::DynamicConfigManager)
        .DefaultNew();
    registrar.Parameter("dynamic_config_path", &TThis::DynamicConfigPath)
        .Default();

    registrar.Postprocessor([] (TThis* config) {
        if (auto& dynamicConfigPath = config->DynamicConfigPath; dynamicConfigPath.empty()) {
            dynamicConfigPath = config->Root + "/config";
        }
    });
};

////////////////////////////////////////////////////////////////////////////////

void TYqlAgentServerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("yql_agent", &TThis::YqlAgent)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYqlAgent
