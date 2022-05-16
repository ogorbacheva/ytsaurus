#include "config.h"

#include <yt/yt/server/lib/hive/config.h>

#include <yt/yt/server/lib/dynamic_config/config.h>

#include <yt/yt/server/lib/election/config.h>

#include <yt/yt/ytlib/query_client/config.h>

#include <yt/yt/core/rpc/config.h>

#include <yt/yt/core/concurrency/config.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

TTabletHydraManagerConfig::TTabletHydraManagerConfig()
{
    RegisterParameter("response_keeper", ResponseKeeper)
        .DefaultNew();
    RegisterParameter("use_new_hydra", UseNewHydra)
        .Default(false);

    RegisterPreprocessor([&] {
        PreallocateChangelogs = true;
    });
}

////////////////////////////////////////////////////////////////////////////////

TRelativeReplicationThrottlerConfig::TRelativeReplicationThrottlerConfig()
{
    RegisterParameter("enable", Enable)
        .Default(false);
    RegisterParameter("ratio", Ratio)
        .GreaterThan(0.0)
        .Default(2.0);
    RegisterParameter("activation_threshold", ActivationThreshold)
        .Default(TDuration::Seconds(60));
    RegisterParameter("window_size", WindowSize)
        .Default(TDuration::Seconds(30));
    RegisterParameter("max_timestamps_to_keep", MaxTimestampsToKeep)
        .GreaterThan(0)
        .Default(100);
}

////////////////////////////////////////////////////////////////////////////////

void TBuiltinTableMountConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("tablet_cell_bundle", &TThis::TabletCellBundle)
        .Optional();
    registrar.Parameter("in_memory_mode", &TThis::InMemoryMode)
        .Default(NTabletClient::EInMemoryMode::None);
    registrar.Parameter("forced_compaction_revision", &TThis::ForcedCompactionRevision)
        .Default();
    registrar.Parameter("forced_store_compaction_revision", &TThis::ForcedStoreCompactionRevision)
        .Default();
    registrar.Parameter("forced_hunk_compaction_revision", &TThis::ForcedHunkCompactionRevision)
        .Default();
    registrar.Parameter("profiling_mode", &TThis::ProfilingMode)
        .Default(EDynamicTableProfilingMode::Path);
    registrar.Parameter("profiling_tag", &TThis::ProfilingTag)
        .Optional();
    registrar.Parameter("enable_dynamic_store_read", &TThis::EnableDynamicStoreRead)
        .Default(false);
    registrar.Parameter("enable_consistent_chunk_replica_placement", &TThis::EnableConsistentChunkReplicaPlacement)
        .Default(false);
    registrar.Parameter("enable_detailed_profiling", &TThis::EnableDetailedProfiling)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

void TCustomTableMountConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("max_dynamic_store_row_count", &TThis::MaxDynamicStoreRowCount)
        .GreaterThan(0)
        .Default(1000000);
    registrar.Parameter("max_dynamic_store_value_count", &TThis::MaxDynamicStoreValueCount)
        .GreaterThan(0)
        .Default(1000000000);
    registrar.Parameter("max_dynamic_store_timestamp_count", &TThis::MaxDynamicStoreTimestampCount)
        .GreaterThan(0)
        .Default(10000000)
        // NB: This limit is really important; please consult babenko@
        // before changing it.
        .LessThanOrEqual(SoftRevisionsPerDynamicStoreLimit);
    registrar.Parameter("max_dynamic_store_pool_size", &TThis::MaxDynamicStorePoolSize)
        .GreaterThan(0)
        .Default(1_GB);
    registrar.Parameter("max_dynamic_store_row_data_weight", &TThis::MaxDynamicStoreRowDataWeight)
        .GreaterThan(0)
        .Default(NTableClient::MaxClientVersionedRowDataWeight)
        // NB: This limit is important: it ensures that store is flushable.
        // Please consult savrus@ before changing.
        .LessThanOrEqual(NTableClient::MaxServerVersionedRowDataWeight / 2);

    registrar.Parameter("dynamic_store_overflow_threshold", &TThis::DynamicStoreOverflowThreshold)
        .GreaterThan(0.0)
        .Default(0.7)
        .LessThanOrEqual(1.0);

    registrar.Parameter("max_partition_data_size", &TThis::MaxPartitionDataSize)
        .Default(320_MB)
        .GreaterThan(0);
    registrar.Parameter("desired_partition_data_size", &TThis::DesiredPartitionDataSize)
        .Default(256_MB)
        .GreaterThan(0);
    registrar.Parameter("min_partition_data_size", &TThis::MinPartitionDataSize)
        .Default(96_MB)
        .GreaterThan(0);

    registrar.Parameter("max_partition_count", &TThis::MaxPartitionCount)
        .Default(10240)
        .GreaterThan(0);

    registrar.Parameter("min_partitioning_data_size", &TThis::MinPartitioningDataSize)
        .Default(64_MB)
        .GreaterThan(0);
    registrar.Parameter("min_partitioning_store_count", &TThis::MinPartitioningStoreCount)
        .Default(1)
        .GreaterThan(0);
    registrar.Parameter("max_partitioning_data_size", &TThis::MaxPartitioningDataSize)
        .Default(1_GB)
        .GreaterThan(0);
    registrar.Parameter("max_partitioning_store_count", &TThis::MaxPartitioningStoreCount)
        .Default(5)
        .GreaterThan(0);

    registrar.Parameter("min_compaction_store_count", &TThis::MinCompactionStoreCount)
        .Default(3)
        .GreaterThan(1);
    registrar.Parameter("max_compaction_store_count", &TThis::MaxCompactionStoreCount)
        .Default(5)
        .GreaterThan(0);
    registrar.Parameter("compaction_data_size_base", &TThis::CompactionDataSizeBase)
        .Default(16_MB)
        .GreaterThan(0);
    registrar.Parameter("compaction_data_size_ratio", &TThis::CompactionDataSizeRatio)
        .Default(2.0)
        .GreaterThan(1.0);

    registrar.Parameter("flush_throttler", &TThis::FlushThrottler)
        .DefaultNew();
    registrar.Parameter("compaction_throttler", &TThis::CompactionThrottler)
        .DefaultNew();
    registrar.Parameter("partitioning_throttler", &TThis::PartitioningThrottler)
        .DefaultNew();

    registrar.Parameter("throttlers", &TThis::Throttlers)
        .Default();

    registrar.Parameter("samples_per_partition", &TThis::SamplesPerPartition)
        .Default(100)
        .GreaterThanOrEqual(0);

    registrar.Parameter("backing_store_retention_time", &TThis::BackingStoreRetentionTime)
        .Default(TDuration::Seconds(60));

    registrar.Parameter("max_read_fan_in", &TThis::MaxReadFanIn)
        .GreaterThan(0)
        .Default(30);

    registrar.Parameter("max_overlapping_store_count", &TThis::MaxOverlappingStoreCount)
        .GreaterThan(0)
        .Default(DefaultMaxOverlappingStoreCount);
    registrar.Parameter("critical_overlapping_store_count", &TThis::CriticalOverlappingStoreCount)
        .GreaterThan(0)
        .Optional();
    registrar.Parameter("overlapping_store_immediate_split_threshold", &TThis::OverlappingStoreImmediateSplitThreshold)
        .GreaterThan(0)
        .Default(20);

    registrar.Parameter("max_stores_per_tablet", &TThis::MaxStoresPerTablet)
        .Default(10000)
        .GreaterThan(0);
    registrar.Parameter("max_eden_stores_per_tablet", &TThis::MaxEdenStoresPerTablet)
        .Default(100)
        .GreaterThan(0);

    registrar.Parameter("forced_chunk_view_compaction_revision", &TThis::ForcedChunkViewCompactionRevision)
        .Default();

    registrar.Parameter("dynamic_store_auto_flush_period", &TThis::DynamicStoreAutoFlushPeriod)
        .Default(TDuration::Minutes(15));
    registrar.Parameter("dynamic_store_flush_period_splay", &TThis::DynamicStoreFlushPeriodSplay)
        .Default(TDuration::Minutes(1));
    registrar.Parameter("auto_compaction_period", &TThis::AutoCompactionPeriod)
        .Default();
    registrar.Parameter("auto_compaction_period_splay_ratio", &TThis::AutoCompactionPeriodSplayRatio)
        .Default(0.3);
    registrar.Parameter("periodic_compaction_mode", &TThis::PeriodicCompactionMode)
        .Default(EPeriodicCompactionMode::Store);

    registrar.Parameter("enable_lookup_hash_table", &TThis::EnableLookupHashTable)
        .Default(false);

    registrar.Parameter("lookup_cache_rows_per_tablet", &TThis::LookupCacheRowsPerTablet)
        .Default(0);
    registrar.Parameter("lookup_cache_rows_ratio", &TThis::LookupCacheRowsRatio)
        .Default(0)
        .GreaterThanOrEqual(0)
        .LessThanOrEqual(1);
    registrar.Parameter("enable_lookup_cache_by_default", &TThis::EnableLookupCacheByDefault)
        .Default(false);

    registrar.Parameter("row_count_to_keep", &TThis::RowCountToKeep)
        .Default(0);

    registrar.Parameter("replication_tick_period", &TThis::ReplicationTickPeriod)
        .Default(TDuration::MilliSeconds(100));
    registrar.Parameter("min_replication_log_ttl", &TThis::MinReplicationLogTtl)
        .Default(TDuration::Minutes(5));
    registrar.Parameter("max_timestamps_per_replication_commit", &TThis::MaxTimestampsPerReplicationCommit)
        .Default(10000);
    registrar.Parameter("max_rows_per_replication_commit", &TThis::MaxRowsPerReplicationCommit)
        .Default(90000);
    registrar.Parameter("max_data_weight_per_replication_commit", &TThis::MaxDataWeightPerReplicationCommit)
        .Default(128_MB);
    registrar.Parameter("replication_throttler", &TThis::ReplicationThrottler)
        .DefaultNew();
    registrar.Parameter("relative_replication_throttler", &TThis::RelativeReplicationThrottler)
        .DefaultNew();
    registrar.Parameter("enable_replication_logging", &TThis::EnableReplicationLogging)
        .Default(false);

    registrar.Parameter("enable_profiling", &TThis::EnableProfiling)
        .Default(false);

    registrar.Parameter("enable_structured_logger", &TThis::EnableStructuredLogger)
        .Default(true);

    registrar.Parameter("enable_compaction_and_partitioning", &TThis::EnableCompactionAndPartitioning)
        .Default(true);

    registrar.Parameter("enable_store_rotation", &TThis::EnableStoreRotation)
        .Default(true);

    registrar.Parameter("merge_rows_on_flush", &TThis::MergeRowsOnFlush)
        .Default(false);

    registrar.Parameter("merge_deletions_on_flush", &TThis::MergeDeletionsOnFlush)
        .Default(false);

    registrar.Parameter("enable_lsm_verbose_logging", &TThis::EnableLsmVerboseLogging)
        .Default(false);

    registrar.Parameter("max_unversioned_block_size", &TThis::MaxUnversionedBlockSize)
        .GreaterThan(0)
        .Optional();

    registrar.Parameter("preserve_tablet_index", &TThis::PreserveTabletIndex)
        .Default(false);

    registrar.Parameter("enable_partition_split_while_eden_partitioning", &TThis::EnablePartitionSplitWhileEdenPartitioning)
        .Default(false);

    registrar.Parameter("enable_discarding_expired_partitions", &TThis::EnableDiscardingExpiredPartitions)
        .Default(true);

    registrar.Parameter("enable_data_node_lookup", &TThis::EnableDataNodeLookup)
        .Default(false);

    registrar.Parameter("enable_peer_probing_in_data_node_lookup", &TThis::EnablePeerProbingInDataNodeLookup)
        .Default(false);

    registrar.Parameter("max_parallel_partition_lookups", &TThis::MaxParallelPartitionLookups)
        .Optional()
        .GreaterThan(0)
        .LessThanOrEqual(MaxParallelPartitionLookupsLimit);

    registrar.Parameter("enable_rejects_in_data_node_lookup_if_throttling", &TThis::EnableRejectsInDataNodeLookupIfThrottling)
        .Default(false);

    registrar.Parameter("lookup_rpc_multiplexing_parallelism", &TThis::LookupRpcMultiplexingParallelism)
        .Default(1)
        .InRange(1, 16);

    registrar.Parameter("enable_new_scan_reader_for_lookup", &TThis::EnableNewScanReaderForLookup)
        .Default(false);
    registrar.Parameter("enable_new_scan_reader_for_select", &TThis::EnableNewScanReaderForSelect)
        .Default(false);

    registrar.Parameter("enable_hunk_columnar_profiling", &TThis::EnableHunkColumnarProfiling)
        .Default(false);

    registrar.Parameter("min_hunk_compaction_total_hunk_length", &TThis::MinHunkCompactionTotalHunkLength)
        .GreaterThanOrEqual(0)
        .Default(1_MB);
    registrar.Parameter("max_hunk_compaction_garbage_ratio", &TThis::MaxHunkCompactionGarbageRatio)
        .InRange(0.0, 1.0)
        .Default(0.5);

    registrar.Parameter("max_hunk_compaction_size", &TThis::MaxHunkCompactionSize)
        .GreaterThan(0)
        .Default(8_MB);
    registrar.Parameter("hunk_compaction_size_base", &TThis::HunkCompactionSizeBase)
        .GreaterThan(0)
        .Default(16_MB);
    registrar.Parameter("hunk_compaction_size_ratio", &TThis::HunkCompactionSizeRatio)
        .GreaterThan(1.0)
        .Default(100.0);
    registrar.Parameter("min_hunk_compaction_chunk_count", &TThis::MinHunkCompactionChunkCount)
        .GreaterThan(1)
        .Default(2);
    registrar.Parameter("max_hunk_compaction_chunk_count", &TThis::MaxHunkCompactionChunkCount)
        .GreaterThan(1)
        .Default(5);

    registrar.Parameter("precache_chunk_replicas_on_mount", &TThis::PrecacheChunkReplicasOnMount)
        .Default(false);
    registrar.Parameter("register_chunk_replicas_on_stores_update", &TThis::RegisterChunkReplicasOnStoresUpdate)
        .Default(false);

    registrar.Parameter("enable_replication_progress_advance_to_barrier", &TThis::EnableReplicationProgressAdvanceToBarrier)
        .Default(true);

    registrar.Postprocessor([&] (TCustomTableMountConfig* config) {
        if (config->MaxDynamicStoreRowCount > config->MaxDynamicStoreValueCount) {
            THROW_ERROR_EXCEPTION("\"max_dynamic_store_row_count\" must be less than or equal to \"max_dynamic_store_value_count\"");
        }
        if (config->MinPartitionDataSize >= config->DesiredPartitionDataSize) {
            THROW_ERROR_EXCEPTION("\"min_partition_data_size\" must be less than \"desired_partition_data_size\"");
        }
        if (config->DesiredPartitionDataSize >= config->MaxPartitionDataSize) {
            THROW_ERROR_EXCEPTION("\"desired_partition_data_size\" must be less than \"max_partition_data_size\"");
        }
        if (config->MaxPartitioningStoreCount < config->MinPartitioningStoreCount) {
            THROW_ERROR_EXCEPTION("\"max_partitioning_store_count\" must be greater than or equal to \"min_partitioning_store_count\"");
        }
        if (config->MaxPartitioningDataSize < config->MinPartitioningDataSize) {
            THROW_ERROR_EXCEPTION("\"max_partitioning_data_size\" must be greater than or equal to \"min_partitioning_data_size\"");
        }
        if (config->MaxCompactionStoreCount < config->MinCompactionStoreCount) {
            THROW_ERROR_EXCEPTION("\"max_compaction_store_count\" must be greater than or equal to \"min_compaction_chunk_count\"");
        }
        if (config->MaxHunkCompactionChunkCount < config->MinHunkCompactionChunkCount) {
            THROW_ERROR_EXCEPTION("\"max_hunk_compaction_chunk_count\" must be greater than or equal to \"min_hunk_compaction_chunk_count\"");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

void TTableMountConfig::Register(TRegistrar registrar)
{
    registrar.Postprocessor([&] (TTableMountConfig* config) {
        if (config->EnableLookupHashTable && config->InMemoryMode != NTabletClient::EInMemoryMode::Uncompressed) {
            THROW_ERROR_EXCEPTION("\"enable_lookup_hash_table\" can only be true if \"in_memory_mode\" is \"uncompressed\"");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

TTransactionManagerConfig::TTransactionManagerConfig()
{
    RegisterParameter("max_transaction_timeout", MaxTransactionTimeout)
        .GreaterThan(TDuration())
        .Default(TDuration::Seconds(60));
    RegisterParameter("barrier_check_period", BarrierCheckPeriod)
        .Default(TDuration::MilliSeconds(100));
    RegisterParameter("max_aborted_transaction_pool_size", MaxAbortedTransactionPoolSize)
        .Default(1000);
    RegisterParameter("reject_incorrect_clock_cluster_tag", RejectIncorrectClockClusterTag)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

void TTabletStoreReaderConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("prefer_local_replicas", &TThis::PreferLocalReplicas)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

void TTabletHunkReaderConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("use_new_chunk_fragment_reader", &TThis::UseNewChunkFragmentReader)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

void TTabletHunkWriterConfig::Register(TRegistrar registrar)
{
    registrar.Preprocessor([&] (TTabletHunkWriterConfig* config) {
        config->EnableStripedErasure = true;
    });

    registrar.Postprocessor([&] (TTabletHunkWriterConfig* config) {
        if (!config->EnableStripedErasure) {
            THROW_ERROR_EXCEPTION("Hunk chunk writer must use striped erasure writer");
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

TTabletManagerConfig::TTabletManagerConfig()
{
    RegisterParameter("pool_chunk_size", PoolChunkSize)
        .GreaterThan(64_KB)
        .Default(1_MB);

    RegisterParameter("max_blocked_row_wait_time", MaxBlockedRowWaitTime)
        .Default(TDuration::Seconds(5));

    RegisterParameter("preload_backoff_time", PreloadBackoffTime)
        .Default(TDuration::Minutes(1));
    RegisterParameter("compaction_backoff_time", CompactionBackoffTime)
        .Default(TDuration::Minutes(1));
    RegisterParameter("flush_backoff_time", FlushBackoffTime)
        .Default(TDuration::Minutes(1));

    RegisterParameter("changelog_codec", ChangelogCodec)
        .Default(NCompression::ECodec::Lz4);

    RegisterParameter("client_timestamp_threshold", ClientTimestampThreshold)
        .Default(TDuration::Minutes(1));

    RegisterParameter("replicator_thread_pool_size", ReplicatorThreadPoolSize)
        .GreaterThan(0)
        .Default(1);
    RegisterParameter("replicator_soft_backoff_time", ReplicatorSoftBackoffTime)
        .Default(TDuration::Seconds(3));
    RegisterParameter("replicator_hard_backoff_time", ReplicatorHardBackoffTime)
        .Default(TDuration::Seconds(60));

    RegisterParameter("tablet_cell_decommission_check_period", TabletCellDecommissionCheckPeriod)
        .Default(TDuration::Seconds(10));

    RegisterParameter("sleep_before_post_to_master", SleepBeforePostToMaster)
        .Default();

    RegisterParameter("shuffle_locked_rows", ShuffleLockedRows)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

TTabletManagerDynamicConfig::TTabletManagerDynamicConfig()
{
    RegisterParameter("replicator_thread_pool_size", ReplicatorThreadPoolSize)
        .GreaterThan(0)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

TStoreFlusherConfig::TStoreFlusherConfig()
{
    RegisterParameter("thread_pool_size", ThreadPoolSize)
        .GreaterThan(0)
        .Default(1);
    RegisterParameter("max_concurrent_flushes", MaxConcurrentFlushes)
        .GreaterThan(0)
        .Default(16);
    RegisterParameter("min_forced_flush_data_size", MinForcedFlushDataSize)
        .GreaterThan(0)
        .Default(1_MB);
}

////////////////////////////////////////////////////////////////////////////////

TStoreFlusherDynamicConfig::TStoreFlusherDynamicConfig()
{
    RegisterParameter("enable", Enable)
        .Default(true);
    RegisterParameter("forced_rotation_memory_ratio", ForcedRotationMemoryRatio)
        .InRange(0.0, 1.0)
        .Optional();
    RegisterParameter("thread_pool_size", ThreadPoolSize)
        .GreaterThan(0)
        .Optional();
    RegisterParameter("max_concurrent_flushes", MaxConcurrentFlushes)
        .GreaterThan(0)
        .Optional();
    RegisterParameter("min_forced_flush_data_size", MinForcedFlushDataSize)
        .GreaterThan(0)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

TStoreCompactorConfig::TStoreCompactorConfig()
{
    RegisterParameter("thread_pool_size", ThreadPoolSize)
        .GreaterThan(0)
        .Default(1);
    RegisterParameter("max_concurrent_compactions", MaxConcurrentCompactions)
        .GreaterThan(0)
        .Default(1);
    RegisterParameter("max_concurrent_partitionings", MaxConcurrentPartitionings)
        .GreaterThan(0)
        .Default(1);
}

////////////////////////////////////////////////////////////////////////////////

TStoreCompactorDynamicConfig::TStoreCompactorDynamicConfig()
{
    RegisterParameter("enable", Enable)
        .Default(true);
    RegisterParameter("thread_pool_size", ThreadPoolSize)
        .GreaterThan(0)
        .Optional();
    RegisterParameter("max_concurrent_compactions", MaxConcurrentCompactions)
        .GreaterThan(0)
        .Optional();
    RegisterParameter("max_concurrent_partitionings", MaxConcurrentPartitionings)
        .GreaterThan(0)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

TStoreTrimmerDynamicConfig::TStoreTrimmerDynamicConfig()
{
    RegisterParameter("enable", Enable)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

THunkChunkSweeperDynamicConfig::THunkChunkSweeperDynamicConfig()
{
    RegisterParameter("enable", Enable)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

TInMemoryManagerConfig::TInMemoryManagerConfig()
{
    RegisterParameter("max_concurrent_preloads", MaxConcurrentPreloads)
        .GreaterThan(0)
        .Default(1);
    RegisterParameter("intercepted_data_retention_time", InterceptedDataRetentionTime)
        .Default(TDuration::Seconds(30));
    RegisterParameter("ping_period", PingPeriod)
        .Default(TDuration::Seconds(10));
    RegisterParameter("control_rpc_timeout", ControlRpcTimeout)
        .Default(TDuration::Seconds(10));
    RegisterParameter("heavy_rpc_timeout", HeavyRpcTimeout)
        .Default(TDuration::Seconds(20));
    RegisterParameter("batch_size", BatchSize)
        .Default(16_MB);
    RegisterParameter("workload_descriptor", WorkloadDescriptor)
        .Default(TWorkloadDescriptor(EWorkloadCategory::UserBatch));
    RegisterParameter("preload_throttler", PreloadThrottler)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

TPartitionBalancerConfig::TPartitionBalancerConfig()
{
    RegisterParameter("chunk_location_throttler", ChunkLocationThrottler)
        .DefaultNew();
    RegisterParameter("chunk_scraper", ChunkScraper)
        .DefaultNew();
    RegisterParameter("samples_fetcher", SamplesFetcher)
        .DefaultNew();
    RegisterParameter("min_partitioning_sample_count", MinPartitioningSampleCount)
        .Default(10)
        .GreaterThanOrEqual(3);
    RegisterParameter("max_partitioning_sample_count", MaxPartitioningSampleCount)
        .Default(1000)
        .GreaterThanOrEqual(10);
    RegisterParameter("max_concurrent_samplings", MaxConcurrentSamplings)
        .GreaterThan(0)
        .Default(8);
    RegisterParameter("resampling_period", ResamplingPeriod)
        .Default(TDuration::Minutes(1));
    RegisterParameter("split_retry_delay", SplitRetryDelay)
        .Default(TDuration::Seconds(30));
}

////////////////////////////////////////////////////////////////////////////////

TPartitionBalancerDynamicConfig::TPartitionBalancerDynamicConfig()
{
    RegisterParameter("enable", Enable)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

TSecurityManagerConfig::TSecurityManagerConfig()
{
    RegisterParameter("resource_limits_cache", ResourceLimitsCache)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

TSecurityManagerDynamicConfig::TSecurityManagerDynamicConfig()
{
    RegisterParameter("resource_limits_cache", ResourceLimitsCache)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

TMasterConnectorConfig::TMasterConnectorConfig()
{
    RegisterParameter("heartbeat_period", HeartbeatPeriod)
        .Default(TDuration::Seconds(30));
    RegisterParameter("heartbeat_period_splay", HeartbeatPeriodSplay)
        .Default(TDuration::Seconds(1));
    RegisterParameter("heartbeat_timeout", HeartbeatTimeout)
        .Default(TDuration::Seconds(60));
}

////////////////////////////////////////////////////////////////////////////////

TMasterConnectorDynamicConfig::TMasterConnectorDynamicConfig()
{
    RegisterParameter("heartbeat_period", HeartbeatPeriod)
        .Default();
    RegisterParameter("heartbeat_period_splay", HeartbeatPeriodSplay)
        .Default();
    RegisterParameter("heartbeat_timeout", HeartbeatTimeout)
        .Default(TDuration::Seconds(60));
}

////////////////////////////////////////////////////////////////////////////////

TResourceLimitsConfig::TResourceLimitsConfig()
{
    RegisterParameter("slots", Slots)
        .GreaterThanOrEqual(0)
        .Default(4);
    RegisterParameter("tablet_static_memory", TabletStaticMemory)
        .GreaterThanOrEqual(0)
        .Default(std::numeric_limits<i64>::max());
    RegisterParameter("tablet_dynamic_memory", TabletDynamicMemory)
        .GreaterThanOrEqual(0)
        .Default(std::numeric_limits<i64>::max());
}

////////////////////////////////////////////////////////////////////////////////

TBackupManagerDynamicConfig::TBackupManagerDynamicConfig()
{
    RegisterParameter("checkpoint_feasibility_check_batch_period", CheckpointFeasibilityCheckBatchPeriod)
        .Default(TDuration::MilliSeconds(100));
    RegisterParameter("checkpoint_feasibility_check_backoff", CheckpointFeasibilityCheckBackoff)
        .Default(TDuration::Seconds(1));
}

////////////////////////////////////////////////////////////////////////////////

TTabletNodeDynamicConfig::TTabletNodeDynamicConfig()
{
    RegisterParameter("slots", Slots)
        .Optional();

    RegisterParameter("tablet_manager", TabletManager)
        .DefaultNew();

    RegisterParameter("throttlers", Throttlers)
        .Optional();

    RegisterParameter("store_compactor", StoreCompactor)
        .DefaultNew();
    RegisterParameter("store_flusher", StoreFlusher)
        .DefaultNew();
    RegisterParameter("store_trimmer", StoreTrimmer)
        .DefaultNew();
    RegisterParameter("hunk_chunk_sweeper", HunkChunkSweeper)
        .DefaultNew();
    RegisterParameter("partition_balancer", PartitionBalancer)
        .DefaultNew();

    RegisterParameter("versioned_chunk_meta_cache", VersionedChunkMetaCache)
        .DefaultNew();

    RegisterParameter("column_evaluator_cache", ColumnEvaluatorCache)
        .DefaultNew();

    RegisterParameter("enable_structured_logger", EnableStructuredLogger)
        .Default(true);
    RegisterParameter("full_structured_tablet_heartbeat_period", FullStructuredTabletHeartbeatPeriod)
        .Default(TDuration::Minutes(5));
    RegisterParameter("incremental_structured_tablet_heartbeat_period", IncrementalStructuredTabletHeartbeatPeriod)
        .Default(TDuration::Seconds(5));

    RegisterParameter("master_connector", MasterConnector)
        .DefaultNew();

    RegisterParameter("security_manager", SecurityManager)
        .DefaultNew();

    RegisterParameter("backup_manager", BackupManager)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

THintManagerConfig::THintManagerConfig()
{
    RegisterParameter("replicator_hint_config_fetcher", ReplicatorHintConfigFetcher)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

TTabletNodeConfig::TTabletNodeConfig()
{
    RegisterParameter("forced_rotation_memory_ratio", ForcedRotationMemoryRatio)
        .InRange(0.0, 1.0)
        .Default(0.8)
        .Alias("forced_rotations_memory_ratio");

    RegisterParameter("resource_limits", ResourceLimits)
        .DefaultNew();

    RegisterParameter("snapshots", Snapshots)
        .DefaultNew();
    RegisterParameter("changelogs", Changelogs)
        .DefaultNew();
    RegisterParameter("hydra_manager", HydraManager)
        .DefaultNew();
    RegisterParameter("election_manager", ElectionManager)
        .DefaultNew();
    RegisterParameter("hive_manager", HiveManager)
        .DefaultNew();
    RegisterParameter("transaction_manager", TransactionManager)
        .DefaultNew();
    RegisterParameter("transaction_supervisor", TransactionSupervisor)
        .DefaultNew();
    RegisterParameter("tablet_manager", TabletManager)
        .DefaultNew();
    RegisterParameter("store_flusher", StoreFlusher)
        .DefaultNew();
    RegisterParameter("store_compactor", StoreCompactor)
        .DefaultNew();
    RegisterParameter("in_memory_manager", InMemoryManager)
        .DefaultNew();
    RegisterParameter("partition_balancer", PartitionBalancer)
        .DefaultNew();
    RegisterParameter("security_manager", SecurityManager)
        .DefaultNew();
    RegisterParameter("hint_manager", HintManager)
        .DefaultNew();

    RegisterParameter("versioned_chunk_meta_cache", VersionedChunkMetaCache)
        .DefaultNew();

    RegisterParameter("throttlers", Throttlers)
        .Optional();

    // COMPAT(babenko): use /tablet_node/throttlers instead.
    RegisterParameter("store_flush_out_throttler", Throttlers[ETabletNodeThrottlerKind::StoreFlushOut])
        .Optional();
    RegisterParameter("store_compaction_and_partitioning_in_throttler", Throttlers[ETabletNodeThrottlerKind::StoreCompactionAndPartitioningIn])
        .Optional();
    RegisterParameter("store_compaction_and_partitioning_out_throttler", Throttlers[ETabletNodeThrottlerKind::StoreCompactionAndPartitioningOut])
        .Optional();
    RegisterParameter("replication_in_throttler", Throttlers[ETabletNodeThrottlerKind::ReplicationIn])
        .Optional();
    RegisterParameter("replication_out_throttler", Throttlers[ETabletNodeThrottlerKind::ReplicationOut])
        .Optional();
    RegisterParameter("dynamic_store_read_out_throttler", Throttlers[ETabletNodeThrottlerKind::DynamicStoreReadOut])
        .Optional();

    RegisterParameter("slot_scan_period", SlotScanPeriod)
        .Default(TDuration::Seconds(1));

    RegisterParameter("tablet_snapshot_eviction_timeout", TabletSnapshotEvictionTimeout)
        .Default(TDuration::Seconds(5));

    RegisterParameter("column_evaluator_cache", ColumnEvaluatorCache)
        .DefaultNew();

    RegisterParameter("master_connector", MasterConnector)
        .DefaultNew();

    RegisterPreprocessor([&] {
        VersionedChunkMetaCache->Capacity = 10_GB;
        HydraManager->MaxCommitBatchDelay = TDuration::MilliSeconds(5);
    });

    RegisterPostprocessor([&] {
        // Instantiate default throttler configs.
        for (auto kind : TEnumTraits<ETabletNodeThrottlerKind>::GetDomainValues()) {
            if (Throttlers[kind]) {
                continue;
            }

            switch (kind) {
                case ETabletNodeThrottlerKind::StaticStorePreloadIn:
                case ETabletNodeThrottlerKind::DynamicStoreReadOut:
                    Throttlers[kind] = New<NConcurrency::TRelativeThroughputThrottlerConfig>(100_MB);
                    break;

                default:
                    Throttlers[kind] = New<NConcurrency::TRelativeThroughputThrottlerConfig>();
            }
        }

        if (InMemoryManager->PreloadThrottler) {
            Throttlers[ETabletNodeThrottlerKind::StaticStorePreloadIn] = InMemoryManager->PreloadThrottler;
        }

        // COMPAT(akozhikhov): set to false when masters are updated too.
        HintManager->ReplicatorHintConfigFetcher->IgnoreConfigAbsence = true;
    });
}

////////////////////////////////////////////////////////////////////////////////

TReplicatorHintConfig::TReplicatorHintConfig()
{
    RegisterParameter("banned_replica_clusters", BannedReplicaClusters)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
