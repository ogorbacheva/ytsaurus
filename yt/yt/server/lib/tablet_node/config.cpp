#include "config.h"

#include <yt/yt/server/lib/hive/config.h>

#include <yt/yt/server/lib/dynamic_config/config.h>

#include <yt/yt/server/lib/election/config.h>

#include <yt/yt/server/lib/transaction_supervisor/config.h>

#include <yt/yt/ytlib/query_client/config.h>

#include <yt/yt/core/rpc/config.h>

#include <yt/yt/core/concurrency/config.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

void TTabletHydraManagerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("response_keeper", &TThis::ResponseKeeper)
        .DefaultNew();
    registrar.Parameter("use_new_hydra", &TThis::UseNewHydra)
        .Default(false);

    registrar.Preprocessor([] (TThis* config) {
        config->PreallocateChangelogs = true;
    });
}

////////////////////////////////////////////////////////////////////////////////

void TRelativeReplicationThrottlerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable", &TThis::Enable)
        .Default(false);
    registrar.Parameter("ratio", &TThis::Ratio)
        .GreaterThan(0.0)
        .Default(2.0);
    registrar.Parameter("activation_threshold", &TThis::ActivationThreshold)
        .Default(TDuration::Seconds(60));
    registrar.Parameter("window_size", &TThis::WindowSize)
        .Default(TDuration::Seconds(30));
    registrar.Parameter("max_timestamps_to_keep", &TThis::MaxTimestampsToKeep)
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

    registrar.Parameter("replication_progress_update_tick_period", &TThis::ReplicationProgressUpdateTickPeriod)
        .Default(TDuration::Seconds(10));

    registrar.Parameter("enable_profiling", &TThis::EnableProfiling)
        .Default(false);

    registrar.Parameter("enable_structured_logger", &TThis::EnableStructuredLogger)
        .Default(true);

    registrar.Parameter("enable_compaction_and_partitioning", &TThis::EnableCompactionAndPartitioning)
        .Default(true);

    registrar.Parameter("enable_store_rotation", &TThis::EnableStoreRotation)
        .Default(true);

    registrar.Parameter("enable_store_flush", &TThis::EnableStoreFlush)
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

    registrar.Parameter("simulated_tablet_snapshot_delay", &TThis::SimulatedTabletSnapshotDelay)
        .Default()
        .DontSerializeDefault();

    registrar.Parameter("simulated_store_preload_delay", &TThis::SimulatedStorePreloadDelay)
        .Default()
        .DontSerializeDefault();

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

void TTransactionManagerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("max_transaction_timeout", &TThis::MaxTransactionTimeout)
        .GreaterThan(TDuration())
        .Default(TDuration::Seconds(60));
    registrar.Parameter("barrier_check_period", &TThis::BarrierCheckPeriod)
        .Default(TDuration::MilliSeconds(100));
    registrar.Parameter("max_aborted_transaction_pool_size", &TThis::MaxAbortedTransactionPoolSize)
        .Default(1000);
    registrar.Parameter("reject_incorrect_clock_cluster_tag", &TThis::RejectIncorrectClockClusterTag)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

void TTabletStoreReaderConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("prefer_local_replicas", &TThis::PreferLocalReplicas)
        .Default(true);
    registrar.Parameter("hedging_manager", &TThis::HedgingManager)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

void TTabletHunkReaderConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("hedging_manager", &TThis::HedgingManager)
        .DefaultNew();
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

void TTabletManagerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("pool_chunk_size", &TThis::PoolChunkSize)
        .GreaterThan(64_KB)
        .Default(1_MB);

    registrar.Parameter("max_blocked_row_wait_time", &TThis::MaxBlockedRowWaitTime)
        .Default(TDuration::Seconds(5));

    registrar.Parameter("preload_backoff_time", &TThis::PreloadBackoffTime)
        .Default(TDuration::Minutes(1));
    registrar.Parameter("compaction_backoff_time", &TThis::CompactionBackoffTime)
        .Default(TDuration::Minutes(1));
    registrar.Parameter("flush_backoff_time", &TThis::FlushBackoffTime)
        .Default(TDuration::Minutes(1));

    registrar.Parameter("changelog_codec", &TThis::ChangelogCodec)
        .Default(NCompression::ECodec::Lz4);

    registrar.Parameter("client_timestamp_threshold", &TThis::ClientTimestampThreshold)
        .Default(TDuration::Minutes(1));

    registrar.Parameter("replicator_thread_pool_size", &TThis::ReplicatorThreadPoolSize)
        .GreaterThan(0)
        .Default(1);
    registrar.Parameter("replicator_soft_backoff_time", &TThis::ReplicatorSoftBackoffTime)
        .Default(TDuration::Seconds(3));
    registrar.Parameter("replicator_hard_backoff_time", &TThis::ReplicatorHardBackoffTime)
        .Default(TDuration::Seconds(60));

    registrar.Parameter("tablet_cell_decommission_check_period", &TThis::TabletCellDecommissionCheckPeriod)
        .Default(TDuration::Seconds(10));
    registrar.Parameter("tablet_cell_suspension_check_period", &TThis::TabletCellSuspensionCheckPeriod)
        .Default(TDuration::Seconds(10));

    registrar.Parameter("sleep_before_post_to_master", &TThis::SleepBeforePostToMaster)
        .Default();

    registrar.Parameter("shuffle_locked_rows", &TThis::ShuffleLockedRows)
        .Default(false);
}

////////////////////////////////////////////////////////////////////////////////

void TTabletManagerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("replicator_thread_pool_size", &TThis::ReplicatorThreadPoolSize)
        .GreaterThan(0)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

void TTabletCellWriteManagerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("write_failure_probability", &TThis::WriteFailureProbability)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

void TStoreFlusherConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("thread_pool_size", &TThis::ThreadPoolSize)
        .GreaterThan(0)
        .Default(1);
    registrar.Parameter("max_concurrent_flushes", &TThis::MaxConcurrentFlushes)
        .GreaterThan(0)
        .Default(16);
    registrar.Parameter("min_forced_flush_data_size", &TThis::MinForcedFlushDataSize)
        .GreaterThan(0)
        .Default(1_MB);
}

////////////////////////////////////////////////////////////////////////////////

void TStoreFlusherDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable", &TThis::Enable)
        .Default(true);
    registrar.Parameter("forced_rotation_memory_ratio", &TThis::ForcedRotationMemoryRatio)
        .InRange(0.0, 1.0)
        .Optional();
    registrar.Parameter("thread_pool_size", &TThis::ThreadPoolSize)
        .GreaterThan(0)
        .Optional();
    registrar.Parameter("max_concurrent_flushes", &TThis::MaxConcurrentFlushes)
        .GreaterThan(0)
        .Optional();
    registrar.Parameter("min_forced_flush_data_size", &TThis::MinForcedFlushDataSize)
        .GreaterThan(0)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

void TStoreCompactorConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("thread_pool_size", &TThis::ThreadPoolSize)
        .GreaterThan(0)
        .Default(1);
    registrar.Parameter("max_concurrent_compactions", &TThis::MaxConcurrentCompactions)
        .GreaterThan(0)
        .Default(1);
    registrar.Parameter("max_concurrent_partitionings", &TThis::MaxConcurrentPartitionings)
        .GreaterThan(0)
        .Default(1);
}

////////////////////////////////////////////////////////////////////////////////

void TStoreCompactorDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable", &TThis::Enable)
        .Default(true);
    registrar.Parameter("thread_pool_size", &TThis::ThreadPoolSize)
        .GreaterThan(0)
        .Optional();
    registrar.Parameter("max_concurrent_compactions", &TThis::MaxConcurrentCompactions)
        .GreaterThan(0)
        .Optional();
    registrar.Parameter("max_concurrent_partitionings", &TThis::MaxConcurrentPartitionings)
        .GreaterThan(0)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

void TStoreTrimmerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable", &TThis::Enable)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

void THunkChunkSweeperDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable", &TThis::Enable)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

void TInMemoryManagerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("max_concurrent_preloads", &TThis::MaxConcurrentPreloads)
        .GreaterThan(0)
        .Default(1);
    registrar.Parameter("intercepted_data_retention_time", &TThis::InterceptedDataRetentionTime)
        .Default(TDuration::Seconds(30));
    registrar.Parameter("ping_period", &TThis::PingPeriod)
        .Default(TDuration::Seconds(10));
    registrar.Parameter("control_rpc_timeout", &TThis::ControlRpcTimeout)
        .Default(TDuration::Seconds(10));
    registrar.Parameter("heavy_rpc_timeout", &TThis::HeavyRpcTimeout)
        .Default(TDuration::Seconds(20));
    registrar.Parameter("batch_size", &TThis::BatchSize)
        .Default(16_MB);
    registrar.Parameter("workload_descriptor", &TThis::WorkloadDescriptor)
        .Default(TWorkloadDescriptor(EWorkloadCategory::UserBatch));
    registrar.Parameter("preload_throttler", &TThis::PreloadThrottler)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

void TInMemoryManagerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("max_concurrent_preloads", &TThis::MaxConcurrentPreloads)
        .GreaterThan(0)
        .Optional();
}

////////////////////////////////////////////////////////////////////////////////

void TPartitionBalancerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("chunk_location_throttler", &TThis::ChunkLocationThrottler)
        .DefaultNew();
    registrar.Parameter("chunk_scraper", &TThis::ChunkScraper)
        .DefaultNew();
    registrar.Parameter("samples_fetcher", &TThis::SamplesFetcher)
        .DefaultNew();
    registrar.Parameter("min_partitioning_sample_count", &TThis::MinPartitioningSampleCount)
        .Default(10)
        .GreaterThanOrEqual(3);
    registrar.Parameter("max_partitioning_sample_count", &TThis::MaxPartitioningSampleCount)
        .Default(1000)
        .GreaterThanOrEqual(10);
    registrar.Parameter("max_concurrent_samplings", &TThis::MaxConcurrentSamplings)
        .GreaterThan(0)
        .Default(8);
    registrar.Parameter("resampling_period", &TThis::ResamplingPeriod)
        .Default(TDuration::Minutes(1));
    registrar.Parameter("split_retry_delay", &TThis::SplitRetryDelay)
        .Default(TDuration::Seconds(30));
}

////////////////////////////////////////////////////////////////////////////////

void TPartitionBalancerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("enable", &TThis::Enable)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

void TSecurityManagerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("resource_limits_cache", &TThis::ResourceLimitsCache)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

void TSecurityManagerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("resource_limits_cache", &TThis::ResourceLimitsCache)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

void TMasterConnectorConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("heartbeat_period", &TThis::HeartbeatPeriod)
        .Default(TDuration::Seconds(30));
    registrar.Parameter("heartbeat_period_splay", &TThis::HeartbeatPeriodSplay)
        .Default(TDuration::Seconds(1));
    registrar.Parameter("heartbeat_timeout", &TThis::HeartbeatTimeout)
        .Default(TDuration::Seconds(60));
}

////////////////////////////////////////////////////////////////////////////////

void TMasterConnectorDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("heartbeat_period", &TThis::HeartbeatPeriod)
        .Default();
    registrar.Parameter("heartbeat_period_splay", &TThis::HeartbeatPeriodSplay)
        .Default();
    registrar.Parameter("heartbeat_timeout", &TThis::HeartbeatTimeout)
        .Default(TDuration::Seconds(60));
}

////////////////////////////////////////////////////////////////////////////////

void TResourceLimitsConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("slots", &TThis::Slots)
        .GreaterThanOrEqual(0)
        .Default(4);
    registrar.Parameter("tablet_static_memory", &TThis::TabletStaticMemory)
        .GreaterThanOrEqual(0)
        .Default(std::numeric_limits<i64>::max());
    registrar.Parameter("tablet_dynamic_memory", &TThis::TabletDynamicMemory)
        .GreaterThanOrEqual(0)
        .Default(std::numeric_limits<i64>::max());
}

////////////////////////////////////////////////////////////////////////////////

void TBackupManagerDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("checkpoint_feasibility_check_batch_period", &TThis::CheckpointFeasibilityCheckBatchPeriod)
        .Default(TDuration::MilliSeconds(100));
    registrar.Parameter("checkpoint_feasibility_check_backoff", &TThis::CheckpointFeasibilityCheckBackoff)
        .Default(TDuration::Seconds(1));
}

////////////////////////////////////////////////////////////////////////////////

void TTabletNodeDynamicConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("slots", &TThis::Slots)
        .Optional();

    registrar.Parameter("tablet_manager", &TThis::TabletManager)
        .DefaultNew();

    registrar.Parameter("tablet_cell_write_manager", &TThis::TabletCellWriteManager)
        .DefaultNew();

    registrar.Parameter("throttlers", &TThis::Throttlers)
        .Optional();

    registrar.Parameter("store_compactor", &TThis::StoreCompactor)
        .DefaultNew();
    registrar.Parameter("store_flusher", &TThis::StoreFlusher)
        .DefaultNew();
    registrar.Parameter("store_trimmer", &TThis::StoreTrimmer)
        .DefaultNew();
    registrar.Parameter("hunk_chunk_sweeper", &TThis::HunkChunkSweeper)
        .DefaultNew();
    registrar.Parameter("partition_balancer", &TThis::PartitionBalancer)
        .DefaultNew();
    registrar.Parameter("in_memory_manager", &TThis::InMemoryManager)
        .DefaultNew();

    registrar.Parameter("versioned_chunk_meta_cache", &TThis::VersionedChunkMetaCache)
        .DefaultNew();

    registrar.Parameter("column_evaluator_cache", &TThis::ColumnEvaluatorCache)
        .DefaultNew();

    registrar.Parameter("enable_structured_logger", &TThis::EnableStructuredLogger)
        .Default(true);
    registrar.Parameter("full_structured_tablet_heartbeat_period", &TThis::FullStructuredTabletHeartbeatPeriod)
        .Default(TDuration::Minutes(5));
    registrar.Parameter("incremental_structured_tablet_heartbeat_period", &TThis::IncrementalStructuredTabletHeartbeatPeriod)
        .Default(TDuration::Seconds(5));

    registrar.Parameter("master_connector", &TThis::MasterConnector)
        .DefaultNew();

    registrar.Parameter("security_manager", &TThis::SecurityManager)
        .DefaultNew();

    registrar.Parameter("backup_manager", &TThis::BackupManager)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

void THintManagerConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("replicator_hint_config_fetcher", &TThis::ReplicatorHintConfigFetcher)
        .DefaultNew();
}

////////////////////////////////////////////////////////////////////////////////

void TTabletNodeConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("forced_rotation_memory_ratio", &TThis::ForcedRotationMemoryRatio)
        .InRange(0.0, 1.0)
        .Default(0.8)
        .Alias("forced_rotations_memory_ratio");

    registrar.Parameter("resource_limits", &TThis::ResourceLimits)
        .DefaultNew();

    registrar.Parameter("snapshots", &TThis::Snapshots)
        .DefaultNew();
    registrar.Parameter("changelogs", &TThis::Changelogs)
        .DefaultNew();
    registrar.Parameter("hydra_manager", &TThis::HydraManager)
        .DefaultNew();
    registrar.Parameter("election_manager", &TThis::ElectionManager)
        .DefaultNew();
    registrar.Parameter("hive_manager", &TThis::HiveManager)
        .DefaultNew();
    registrar.Parameter("transaction_manager", &TThis::TransactionManager)
        .DefaultNew();
    registrar.Parameter("transaction_supervisor", &TThis::TransactionSupervisor)
        .DefaultNew();
    registrar.Parameter("tablet_manager", &TThis::TabletManager)
        .DefaultNew();
    registrar.Parameter("store_flusher", &TThis::StoreFlusher)
        .DefaultNew();
    registrar.Parameter("store_compactor", &TThis::StoreCompactor)
        .DefaultNew();
    registrar.Parameter("in_memory_manager", &TThis::InMemoryManager)
        .DefaultNew();
    registrar.Parameter("partition_balancer", &TThis::PartitionBalancer)
        .DefaultNew();
    registrar.Parameter("security_manager", &TThis::SecurityManager)
        .DefaultNew();
    registrar.Parameter("hint_manager", &TThis::HintManager)
        .DefaultNew();

    registrar.Parameter("versioned_chunk_meta_cache", &TThis::VersionedChunkMetaCache)
        .DefaultNew();

    registrar.Parameter("throttlers", &TThis::Throttlers)
        .Optional();

    registrar.Parameter("slot_scan_period", &TThis::SlotScanPeriod)
        .Default(TDuration::Seconds(1));

    registrar.Parameter("tablet_snapshot_eviction_timeout", &TThis::TabletSnapshotEvictionTimeout)
        .Default(TDuration::Seconds(5));

    registrar.Parameter("column_evaluator_cache", &TThis::ColumnEvaluatorCache)
        .DefaultNew();

    registrar.Parameter("master_connector", &TThis::MasterConnector)
        .DefaultNew();

    registrar.Preprocessor([] (TThis* config) {
        config->VersionedChunkMetaCache->Capacity = 10_GB;
        config->HydraManager->MaxCommitBatchDelay = TDuration::MilliSeconds(5);
    });

    registrar.Postprocessor([] (TThis* config) {
        // Instantiate default throttler configs.
        for (auto kind : TEnumTraits<ETabletNodeThrottlerKind>::GetDomainValues()) {
            if (config->Throttlers[kind]) {
                continue;
            }

            switch (kind) {
                case ETabletNodeThrottlerKind::StaticStorePreloadIn:
                case ETabletNodeThrottlerKind::DynamicStoreReadOut:
                    config->Throttlers[kind] = New<NConcurrency::TRelativeThroughputThrottlerConfig>(100_MB);
                    break;

                default:
                    config->Throttlers[kind] = New<NConcurrency::TRelativeThroughputThrottlerConfig>();
            }
        }

        if (config->InMemoryManager->PreloadThrottler) {
            config->Throttlers[ETabletNodeThrottlerKind::StaticStorePreloadIn] = config->InMemoryManager->PreloadThrottler;
        }

        // COMPAT(akozhikhov): set to false when masters are updated too.
        config->HintManager->ReplicatorHintConfigFetcher->IgnoreConfigAbsence = true;
    });
}

////////////////////////////////////////////////////////////////////////////////

void TReplicatorHintConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("banned_replica_clusters", &TThis::BannedReplicaClusters)
        .Default();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
