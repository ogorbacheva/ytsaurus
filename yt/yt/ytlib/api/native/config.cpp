#include "config.h"

#include <yt/yt/ytlib/hive/config.h>

#include <yt/yt/ytlib/node_tracker_client/config.h>

#include <yt/yt/ytlib/scheduler/config.h>

#include <yt/yt/ytlib/table_client/config.h>

#include <yt/yt/ytlib/tablet_client/config.h>

#include <yt/yt/ytlib/queue_client/config.h>

#include <yt/yt/ytlib/transaction_client/config.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/client/transaction_client/config.h>

namespace NYT::NApi::NNative {

using namespace NObjectClient;
using namespace NTransactionClient;

////////////////////////////////////////////////////////////////////////////////

void TMasterConnectionConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("rpc_timeout", &TThis::RpcTimeout)
        .Default(TDuration::Seconds(30));

    registrar.Preprocessor([] (TThis* config) {
        config->RetryAttempts = 100;
        config->RetryTimeout = TDuration::Minutes(3);
    });
}

////////////////////////////////////////////////////////////////////////////////

void TMasterCacheConnectionConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable_master_cache_discovery", &TThis::EnableMasterCacheDiscovery)
        .Default(true);
    registrar.Parameter("master_cache_discovery_period", &TThis::MasterCacheDiscoveryPeriod)
        .Default(TDuration::Minutes(1));
    registrar.Parameter("master_cache_discovery_period_splay", &TThis::MasterCacheDiscoveryPeriodSplay)
        .Default(TDuration::Seconds(10));

    registrar.Postprocessor([] (TThis* config) {
        if (config->EnableMasterCacheDiscovery && config->Endpoints) {
            THROW_ERROR_EXCEPTION("Cannot specify \"endpoints\" when master cache discovery is enabled");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TClockServersConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("rpc_timeout", &TThis::RpcTimeout)
        .Default(TDuration::Seconds(30));
}

////////////////////////////////////////////////////////////////////////////////

void TConnectionConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("networks", &TThis::Networks)
        .Default();
    registrar.Parameter("timestamp_provider", &TThis::TimestampProvider)
        .Default();
    registrar.Parameter("cell_directory", &TThis::CellDirectory)
        .DefaultNew();
    registrar.Parameter("cell_directory_synchronizer", &TThis::CellDirectorySynchronizer)
        .DefaultNew();
    registrar.Parameter("chaos_cell_directory_synchronizer", &TThis::ChaosCellDirectorySynchronizer)
        .DefaultNew();
    registrar.Parameter("clock_servers", &TThis::ClockServers)
        .Default();
    registrar.Parameter("master_cell_directory_synchronizer", &TThis::MasterCellDirectorySynchronizer)
        .DefaultNew();
    registrar.Parameter("scheduler", &TThis::Scheduler)
        .DefaultNew();
    registrar.Parameter("queue_agent", &TThis::QueueAgent)
        .DefaultNew();
    registrar.Parameter("transaction_manager", &TThis::TransactionManager)
        .DefaultNew();
    registrar.Parameter("clock_manager", &TThis::ClockManager)
        .DefaultNew();
    registrar.Parameter("block_cache", &TThis::BlockCache)
        .DefaultNew();
    registrar.Parameter("chunk_meta_cache", &TThis::ChunkMetaCache)
        .DefaultNew();
    registrar.Parameter("chunk_replica_cache", &TThis::ChunkReplicaCache)
        .DefaultNew();
    registrar.Parameter("cluster_directory_synchronizer", &TThis::ClusterDirectorySynchronizer)
        .DefaultNew();
    registrar.Parameter("medium_directory_synchronizer", &TThis::MediumDirectorySynchronizer)
        .DefaultNew();
    registrar.Parameter("node_directory_synchronizer", &TThis::NodeDirectorySynchronizer)
        .DefaultNew();
    registrar.Parameter("chunk_slice_fetcher", &TThis::ChunkSliceFetcher)
        .DefaultNew();

    registrar.Parameter("query_evaluator", &TThis::QueryEvaluator)
        .DefaultNew();
    registrar.Parameter("default_select_rows_timeout", &TThis::DefaultSelectRowsTimeout)
        // COMPAT(babenko)
        .Alias("query_timeout")
        .Default(TDuration::Seconds(60));
    registrar.Parameter("select_rows_response_codec", &TThis::SelectRowsResponseCodec)
        // COMPAT(babenko)
        .Alias("query_response_codec")
        .Default(NCompression::ECodec::Lz4);
    registrar.Parameter("default_input_row_limit", &TThis::DefaultInputRowLimit)
        .GreaterThan(0)
        .Default(1000000);
    registrar.Parameter("default_output_row_limit", &TThis::DefaultOutputRowLimit)
        .GreaterThan(0)
        .Default(1000000);

    registrar.Parameter("column_evaluator_cache", &TThis::ColumnEvaluatorCache)
        .DefaultNew();

    registrar.Parameter("write_rows_timeout", &TThis::WriteRowsTimeout)
        // COMPAT(babenko)
        .Alias("write_timeout")
        .Default(TDuration::Seconds(60));
    registrar.Parameter("write_rows_request_codec", &TThis::WriteRowsRequestCodec)
        // COMPAT(babenko)
        .Alias("write_request_codec")
        .Default(NCompression::ECodec::Lz4);
    registrar.Parameter("max_rows_per_write_request", &TThis::MaxRowsPerWriteRequest)
        .GreaterThan(0)
        .Default(1000);
    registrar.Parameter("max_data_weight_per_write_request", &TThis::MaxDataWeightPerWriteRequest)
        .GreaterThan(0)
        .Default(64_MB);
    registrar.Parameter("max_rows_per_transaction", &TThis::MaxRowsPerTransaction)
        .GreaterThan(0)
        .Default(100000);

    registrar.Parameter("default_lookup_rows_timeout", &TThis::DefaultLookupRowsTimeout)
        // COMPAT(babenko)
        .Alias("lookup_timeout")
        .Default(TDuration::Seconds(60));
    registrar.Parameter("lookup_rows_request_codec", &TThis::LookupRowsRequestCodec)
        .Alias("lookup_request_codec")
        .Default(NCompression::ECodec::Lz4);
    registrar.Parameter("lookup_rows_response_codec", &TThis::LookupRowsResponseCodec)
        .Alias("lookup_response_codec")
        .Default(NCompression::ECodec::Lz4);
    registrar.Parameter("max_rows_per_lookup_request", &TThis::MaxRowsPerLookupRequest)
        .Alias("max_rows_per_read_request")
        .GreaterThan(0)
        .Default(1000);

    registrar.Parameter("default_get_tablet_errors_limit", &TThis::DefaultGetTabletErrorsLimit)
        .Default(5)
        .GreaterThan(0);

    registrar.Parameter("udf_registry_path", &TThis::UdfRegistryPath)
        .Default("//tmp/udfs");
    registrar.Parameter("function_registry_cache", &TThis::FunctionRegistryCache)
        .DefaultNew();
    registrar.Parameter("function_impl_cache", &TThis::FunctionImplCache)
        .DefaultNew();

    registrar.Parameter("thread_pool_size", &TThis::ThreadPoolSize)
        .Default(4);

    registrar.Parameter("bus_client", &TThis::BusClient)
        .DefaultNew();
    registrar.Parameter("idle_channel_ttl", &TThis::IdleChannelTtl)
        .Default(TDuration::Minutes(5));

    registrar.Parameter("default_get_in_sync_replicas_timeout", &TThis::DefaultGetInSyncReplicasTimeout)
        .Default(TDuration::Seconds(15));
    registrar.Parameter("default_get_tablet_infos_timeout", &TThis::DefaultGetTabletInfosTimeout)
        .Default(TDuration::Seconds(15));
    registrar.Parameter("default_trim_table_timeout", &TThis::DefaultTrimTableTimeout)
        .Default(TDuration::Seconds(15));
    registrar.Parameter("default_get_operation_retry_interval", &TThis::DefaultGetOperationRetryInterval)
        .Default(TDuration::Seconds(3));
    registrar.Parameter("default_get_operation_timeout", &TThis::DefaultGetOperationTimeout)
        .Default(TDuration::Minutes(5));
    registrar.Parameter("default_list_jobs_timeout", &TThis::DefaultListJobsTimeout)
        .Default(TDuration::Seconds(60));
    registrar.Parameter("default_get_job_timeout", &TThis::DefaultGetJobTimeout)
        .Default(TDuration::Seconds(60));
    registrar.Parameter("default_list_operations_timeout", &TThis::DefaultListOperationsTimeout)
        .Default(TDuration::Seconds(60));
    registrar.Parameter("default_pull_rows_timeout", &TThis::DefaultPullRowsTimeout)
        .Default(TDuration::Seconds(60));
    registrar.Parameter("default_sync_alien_cells_timeout", &TThis::DefaultSyncAlienCellsTimeout)
        .Default(TDuration::Seconds(60));
    registrar.Parameter("default_chaos_node_serivce_timeout", &TThis::DefaultChaosNodeServiceTimeout)
        .Default(TDuration::Seconds(15));

    registrar.Parameter("cypress_write_yson_nesting_level_limit", &TThis::CypressWriteYsonNestingLevelLimit)
        .Default(NYson::OriginalNestingLevelLimit)
        .LessThanOrEqual(NYson::NewNestingLevelLimit);

    registrar.Parameter("job_prober_rpc_timeout", &TThis::JobProberRpcTimeout)
        .Default(TDuration::Seconds(45));

    registrar.Parameter("default_cache_sticky_group_size", &TThis::DefaultCacheStickyGroupSize)
        .Alias("cache_sticky_group_size_override")
        .Default(1);
    registrar.Parameter("enable_dynamic_cache_sticky_group_size", &TThis::EnableDynamicCacheStickyGroupSize)
        .Default(false);

    registrar.Parameter("max_request_window_size", &TThis::MaxRequestWindowSize)
        .GreaterThan(0)
        .Default(65536);

    registrar.Parameter("upload_transaction_timeout", &TThis::UploadTransactionTimeout)
        .Default(TDuration::Seconds(15));
    registrar.Parameter("hive_sync_rpc_timeout", &TThis::HiveSyncRpcTimeout)
        .Default(TDuration::Seconds(30));

    registrar.Parameter("connection_name", &TThis::ConnectionName)
        .Alias("name")
        .Default("default");

    registrar.Parameter("permission_cache", &TThis::PermissionCache)
        .DefaultNew();

    registrar.Parameter("job_shell_descriptor_cache", &TThis::JobShellDescriptorCache)
        .Alias("job_node_descriptor_cache")
        .DefaultNew();

    registrar.Parameter("max_chunks_per_fetch", &TThis::MaxChunksPerFetch)
        .Default(100'000)
        .GreaterThan(0);

    registrar.Parameter("max_chunks_per_locate_request", &TThis::MaxChunksPerLocateRequest)
        .Default(10'000)
        .GreaterThan(0);

    registrar.Parameter("nested_input_transaction_timeout", &TThis::NestedInputTransactionTimeout)
        .Default(TDuration::Minutes(10));
    registrar.Parameter("nested_input_transaction_ping_period", &TThis::NestedInputTransactionPingPeriod)
        .Default(TDuration::Minutes(1));

    registrar.Parameter("cluster_liveness_check_timeout", &TThis::ClusterLivenessCheckTimeout)
        .Default(TDuration::Seconds(15));

    registrar.Parameter("chunk_fetch_retries", &TThis::ChunkFetchRetries)
        .DefaultNew();

    registrar.Parameter("enable_networking", &TThis::EnableNetworking)
        .Default(true);

    registrar.Parameter("sync_replica_cache", &TThis::SyncReplicaCache)
        .DefaultNew();

    registrar.Parameter("chaos_cell_channel", &TThis::ChaosCellChannel)
        .DefaultNew();

    registrar.Parameter("hydra_admin_channel", &TThis::HydraAdminChannel)
        .DefaultNew();

    registrar.Parameter("sequoia_path", &TThis::SequoiaPath)
        .Default("//sys/sequoia");
    registrar.Parameter("sequoia_transaction_timeout", &TThis::SequoiaTransactionTimeout)
        .Default(TDuration::Minutes(1));

    registrar.Parameter("use_followers_for_write_targets_allocation", &TThis::UseFollowersForWriteTargetsAllocation)
        .Default(false);

    registrar.Parameter("tvm_id", &TThis::TvmId)
        .Default();

    registrar.Preprocessor([] (TThis* config) {
        config->FunctionImplCache->Capacity = 100;

        config->JobShellDescriptorCache->ExpireAfterAccessTime = TDuration::Minutes(5);
        config->JobShellDescriptorCache->ExpireAfterSuccessfulUpdateTime = TDuration::Minutes(5);
        config->JobShellDescriptorCache->RefreshTime = TDuration::Minutes(1);

        config->SyncReplicaCache->ExpireAfterSuccessfulUpdateTime = TDuration::Minutes(5);
        config->SyncReplicaCache->RefreshTime = TDuration::Seconds(5);
    });
}

void TConnectionConfig::OverrideMasterAddresses(const std::vector<TString>& addresses)
{
    auto patchMasterConnectionConfig = [&] (const TMasterConnectionConfigPtr& config) {
        config->Addresses = addresses;
        config->Endpoints = nullptr;
        if (config->RetryTimeout && *config->RetryTimeout > config->RpcTimeout) {
            config->RpcTimeout = *config->RetryTimeout;
        }
        config->RetryTimeout = std::nullopt;
        config->RetryAttempts = 1;
        config->IgnorePeerState = true;
    };

    patchMasterConnectionConfig(PrimaryMaster);
    for (const auto& config : SecondaryMasters) {
        patchMasterConnectionConfig(config);
    }
    if (!MasterCache) {
        MasterCache = New<TMasterCacheConnectionConfig>();
        MasterCache->Load(ConvertToNode(PrimaryMaster));
    }
    patchMasterConnectionConfig(MasterCache);
    MasterCache->EnableMasterCacheDiscovery = false;

    MasterCellDirectorySynchronizer->RetryPeriod = std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////

TConnectionDynamicConfig::TConnectionDynamicConfig()
{
    RegisterParameter("sync_replica_cache", SyncReplicaCache)
        .DefaultNew();
    RegisterParameter("clock_manager", ClockManager)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

TRemoteTimestampProviderConfigPtr CreateRemoteTimestampProviderConfig(TMasterConnectionConfigPtr config)
{
    auto timestampProviderConfig = New<TRemoteTimestampProviderConfig>();

    // Use masters for timestamp generation.
    timestampProviderConfig->Addresses = config->Addresses;
    timestampProviderConfig->RpcTimeout = config->RpcTimeout;

    // TRetryingChannelConfig
    timestampProviderConfig->RetryBackoffTime = config->RetryBackoffTime;
    timestampProviderConfig->RetryAttempts = config->RetryAttempts;
    timestampProviderConfig->RetryTimeout = config->RetryTimeout;

    return timestampProviderConfig;
}

/////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative
