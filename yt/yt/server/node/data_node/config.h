#pragma once

#include "public.h"

#include <yt/yt/server/lib/hydra/config.h>

#include <yt/yt/server/lib/misc/config.h>

#include <yt/yt/server/lib/containers/config.h>

#include <yt/yt/ytlib/chunk_client/config.h>

#include <yt/yt/ytlib/journal_client/config.h>

#include <yt/yt/ytlib/table_client/config.h>

#include <yt/yt/core/concurrency/config.h>

#include <yt/yt/core/misc/config.h>
#include <yt/yt/core/misc/arithmetic_formula.h>

#include <yt/yt/library/re2/re2.h>

namespace NYT::NDataNode {

////////////////////////////////////////////////////////////////////////////////

class TBlockPeerTableConfig
    : public NYTree::TYsonSerializable
{
public:
    int MaxPeersPerBlock;
    TDuration SweepPeriod;

    TBlockPeerTableConfig()
    {
        RegisterParameter("max_peers_per_block", MaxPeersPerBlock)
            .GreaterThan(0)
            .Default(64);
        RegisterParameter("sweep_period", SweepPeriod)
            .Default(TDuration::Minutes(10));
    }
};

DEFINE_REFCOUNTED_TYPE(TBlockPeerTableConfig)

////////////////////////////////////////////////////////////////////////////////

class TP2PBlockDistributorConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Period between distributor iterations.
    TDuration IterationPeriod;

    //! Transmitted byte count per second enough for P2P to become active.
    i64 OutTrafficActivationThreshold;

    //! Out queue size (Out throttler queue size + default network bus pending byte count) enough for P2P to become active.
    i64 OutQueueSizeActivationThreshold;

    //! Block throughput in bytes per second enough for P2P to become active.
    i64 TotalRequestedBlockSizeActivationThreshold;

    //! Regex for names of network interfaces considered when calculating transmitted byte count.
    NRe2::TRe2Ptr NetOutInterfaces;

    //! Maximum total size of blocks transmitted to a single node during the iteration.
    i64 MaxPopulateRequestSize;

    //! Number of nodes to send blocks on a given iteration.
    int DestinationNodeCount;

    //! Upper bound on number of times block may be distributed while we track it as an active. We do not want
    //! the same block to be distributed again and again.
    int MaxDistributionCount;

    //! Minimum number of times block should be requested during `WindowLength` time period in order to be
    //! considered as a candidate for distribution.
    int MinRequestCount;

    //! Delay between consecutive distributions of a given block.
    TDuration ConsecutiveDistributionDelay;

    //! Length of the window in which we consider events of blocks being accessed.
    TDuration WindowLength;

    //! Configuration of the retrying channel used for `PopulateCache` requests.
    NRpc::TRetryingChannelConfigPtr NodeChannel;

    //! Node tag filter defining which nodes will be considered as candidates for distribution.
    TBooleanFormula NodeTagFilter;

    TP2PBlockDistributorConfig()
    {
        RegisterParameter("iteration_period", IterationPeriod)
            .Default(TDuration::Seconds(1));
        RegisterParameter("out_traffic_activation_threshold", OutTrafficActivationThreshold)
            .Default(768_MB);
        RegisterParameter("out_queue_size_activation_threshold", OutQueueSizeActivationThreshold)
            .Default(256_MB);
        RegisterParameter("total_requested_block_size_activation_threshold", TotalRequestedBlockSizeActivationThreshold)
            .Default(512_MB);
        RegisterParameter("net_out_interfaces", NetOutInterfaces)
            .Default(New<NRe2::TRe2>("eth\\d*"));
        RegisterParameter("max_populate_request_size", MaxPopulateRequestSize)
            .Default(64_MB);
        RegisterParameter("destination_node_count", DestinationNodeCount)
            .Default(3);
        RegisterParameter("max_distribution_count", MaxDistributionCount)
            .Default(12);
        RegisterParameter("min_request_count", MinRequestCount)
            .Default(3);
        RegisterParameter("consecutive_distribution_delay", ConsecutiveDistributionDelay)
            .Default(TDuration::Seconds(5));
        RegisterParameter("window_length", WindowLength)
            .Default(TDuration::Seconds(10));
        RegisterParameter("node_channel", NodeChannel)
            .DefaultNew();
        RegisterParameter("node_tag_filter", NodeTagFilter)
            .Default(MakeBooleanFormula("!CLOUD"));
    }
};

DEFINE_REFCOUNTED_TYPE(TP2PBlockDistributorConfig)

////////////////////////////////////////////////////////////////////////////////

class TStoreLocationConfigBase
    : public TDiskLocationConfig
{
public:
    //! Maximum space chunks are allowed to occupy.
    //! (If not initialized then indicates to occupy all available space on drive).
    std::optional<i64> Quota;

    // NB: actually registered as parameter by subclasses (because default value
    // is subclass-specific).
    TString MediumName;

    //! Disk family in this location (HDD, SDD, etc.)
    TString DiskFamily;

    //! Controls outcoming location bandwidth used by replication jobs.
    NConcurrency::TThroughputThrottlerConfigPtr ReplicationOutThrottler;

    //! Controls outcoming location bandwidth used by tablet compaction and partitioning.
    NConcurrency::TThroughputThrottlerConfigPtr TabletCompactionAndPartitioningOutThrottler;

    //! Controls outcoming location bandwidth used by tablet logging.
    NConcurrency::TThroughputThrottlerConfigPtr TabletLoggingOutThrottler;

    //! Controls outcoming location bandwidth used by tablet preload.
    NConcurrency::TThroughputThrottlerConfigPtr TabletPreloadOutThrottler;

    //! Controls outcoming location bandwidth used by tablet recovery.
    NConcurrency::TThroughputThrottlerConfigPtr TabletRecoveryOutThrottler;

    NIO::EIOEngineType IOEngineType;
    NYTree::INodePtr IOConfig;

    TDuration ThrottleDuration;

    //! Maximum number of bytes in the gap between two adjacent read locations
    //! in order to join them together during read coalescing.
    i64 CoalescedReadMaxGapSize;

    TStoreLocationConfigBase()
    {
        RegisterParameter("quota", Quota)
            .GreaterThanOrEqual(0)
            .Default(std::optional<i64>());
        RegisterParameter("replication_out_throttler", ReplicationOutThrottler)
            .DefaultNew();
        RegisterParameter("tablet_compaction_and_partitioning_out_throttler", TabletCompactionAndPartitioningOutThrottler)
            .DefaultNew();
        RegisterParameter("tablet_logging_out_throttler", TabletLoggingOutThrottler)
            .DefaultNew();
        RegisterParameter("tablet_preload_out_throttler", TabletPreloadOutThrottler)
            .DefaultNew();
        RegisterParameter("tablet_recovery_out_throttler", TabletRecoveryOutThrottler)
            .DefaultNew();
        RegisterParameter("io_engine_type", IOEngineType)
            .Default(NIO::EIOEngineType::ThreadPool);
        RegisterParameter("io_config", IOConfig)
            .Optional();
        RegisterParameter("throttle_counter_interval", ThrottleDuration)
            .Default(TDuration::Seconds(30));
        RegisterParameter("coalesced_read_max_gap_size", CoalescedReadMaxGapSize)
            .GreaterThanOrEqual(0)
            .Default(0);
        RegisterParameter("disk_family", DiskFamily)
            .Default("UNKNOWN");
    }
};

DEFINE_REFCOUNTED_TYPE(TStoreLocationConfigBase)

////////////////////////////////////////////////////////////////////////////////

class TStoreLocationConfig
    : public TStoreLocationConfigBase
{
public:
    //! A currently full location is considered to be non-full again when available space grows
    //! above this limit.
    i64 LowWatermark;

    //! A location is considered to be full when available space becomes less than #HighWatermark.
    i64 HighWatermark;

    //! All writes to the location are aborted when available space becomes less than #DisableWritesWatermark.
    i64 DisableWritesWatermark;

    //! Maximum amount of time files of a deleted chunk could rest in trash directory before
    //! being permanently removed.
    TDuration MaxTrashTtl;

    //! When free space drops below this watermark, the system starts deleting files in trash directory,
    //! starting from the eldest ones.
    i64 TrashCleanupWatermark;

    //! Period between trash cleanups.
    TDuration TrashCheckPeriod;

    //! Controls incoming location bandwidth used by repair jobs.
    NConcurrency::TThroughputThrottlerConfigPtr RepairInThrottler;

    //! Controls incoming location bandwidth used by replication jobs.
    NConcurrency::TThroughputThrottlerConfigPtr ReplicationInThrottler;

    //! Controls incoming location bandwidth used by tablet compaction and partitioning.
    NConcurrency::TThroughputThrottlerConfigPtr TabletCompactionAndPartitioningInThrottler;

    //! Controls incoming location bandwidth used by tablet journals.
    NConcurrency::TThroughputThrottlerConfigPtr TabletLoggingInThrottler;

    //! Controls incoming location bandwidth used by tablet snapshots.
    NConcurrency::TThroughputThrottlerConfigPtr TabletSnapshotInThrottler;

    //! Controls incoming location bandwidth used by tablet store flush.
    NConcurrency::TThroughputThrottlerConfigPtr TabletStoreFlushInThrottler;

    //! Per-location multiplexed changelog configuration.
    NYTree::INodePtr MultiplexedChangelog;

    //! Per-location  configuration of per-chunk changelog that backs the multiplexed changelog.
    NYTree::INodePtr HighLatencySplitChangelog;

    //! Per-location configuration of per-chunk changelog that is being written directly (w/o multiplexing).
    NYTree::INodePtr LowLatencySplitChangelog;


    TStoreLocationConfig()
    {
        RegisterParameter("low_watermark", LowWatermark)
            .GreaterThanOrEqual(0)
            .Default(5_GB);
        RegisterParameter("high_watermark", HighWatermark)
            .GreaterThanOrEqual(0)
            .Default(2_GB);
        RegisterParameter("disable_writes_watermark", DisableWritesWatermark)
            .GreaterThanOrEqual(0)
            .Default(1_GB);
        RegisterParameter("max_trash_ttl", MaxTrashTtl)
            .Default(TDuration::Hours(1))
            .GreaterThanOrEqual(TDuration::Zero());
        RegisterParameter("trash_cleanup_watermark", TrashCleanupWatermark)
            .GreaterThanOrEqual(0)
            .Default(4_GB);
        RegisterParameter("trash_check_period", TrashCheckPeriod)
            .GreaterThanOrEqual(TDuration::Zero())
            .Default(TDuration::Seconds(10));
        RegisterParameter("repair_in_throttler", RepairInThrottler)
            .DefaultNew();
        RegisterParameter("replication_in_throttler", ReplicationInThrottler)
            .DefaultNew();
        RegisterParameter("tablet_comaction_and_partitoning_in_throttler", TabletCompactionAndPartitioningInThrottler)
            .DefaultNew();
        RegisterParameter("tablet_logging_in_throttler", TabletLoggingInThrottler)
            .DefaultNew();
        RegisterParameter("tablet_snapshot_in_throttler", TabletSnapshotInThrottler)
            .DefaultNew();
        RegisterParameter("tablet_store_flush_in_throttler", TabletStoreFlushInThrottler)
            .DefaultNew();

        RegisterParameter("multiplexed_changelog", MultiplexedChangelog)
            .Default();
        RegisterParameter("high_latency_split_changelog", HighLatencySplitChangelog)
            .Default();
        RegisterParameter("low_latency_split_changelog", LowLatencySplitChangelog)
            .Default();

        // NB: base class's field.
        RegisterParameter("medium_name", MediumName)
            .Default(NChunkClient::DefaultStoreMediumName);

        RegisterPostprocessor([&] () {
            if (HighWatermark > LowWatermark) {
                THROW_ERROR_EXCEPTION("\"high_full_watermark\" must be less than or equal to \"low_watermark\"");
            }
            if (DisableWritesWatermark > HighWatermark) {
                THROW_ERROR_EXCEPTION("\"write_disable_watermark\" must be less than or equal to \"high_watermark\"");
            }
            if (DisableWritesWatermark > TrashCleanupWatermark) {
                THROW_ERROR_EXCEPTION("\"disable_writes_watermark\" must be less than or equal to \"trash_cleanup_watermark\"");
            }
        });
    }
};

DEFINE_REFCOUNTED_TYPE(TStoreLocationConfig)

////////////////////////////////////////////////////////////////////////////////

class TCacheLocationConfig
    : public TStoreLocationConfigBase
{
public:
    //! Controls incoming location bandwidth used by cache.
    NConcurrency::TThroughputThrottlerConfigPtr InThrottler;

    TCacheLocationConfig()
    {
        RegisterParameter("in_throttler", InThrottler)
            .DefaultNew();

        // NB: base class's field.
        RegisterParameter("medium_name", MediumName)
            .Default(NChunkClient::DefaultCacheMediumName);
    }
};

DEFINE_REFCOUNTED_TYPE(TCacheLocationConfig)

////////////////////////////////////////////////////////////////////////////////

class TMultiplexedChangelogConfig
    : public NHydra::TFileChangelogConfig
    , public NHydra::IFileChangelogDispatcherConfig
{
public:
    //! Multiplexed changelog record count limit.
    /*!
     *  When this limit is reached, the current multiplexed changelog is rotated.
     */
    int MaxRecordCount;

    //! Multiplexed changelog data size limit, in bytes.
    /*!
     *  See #MaxRecordCount.
     */
    i64 MaxDataSize;

    //! Interval between automatic changelog rotation (to avoid keeping too many non-clean records
    //! and speed up starup).
    TDuration AutoRotationPeriod;

    //! Maximum bytes of multiplexed changelog to read during
    //! a single iteration of replay.
    i64 ReplayBufferSize;

    //! Maximum number of clean multiplexed changelogs to keep.
    int MaxCleanChangelogsToKeep;

    //! Time to wait before marking a multiplexed changelog as clean.
    TDuration CleanDelay;

    TMultiplexedChangelogConfig()
    {
        RegisterParameter("max_record_count", MaxRecordCount)
            .Default(1000000)
            .GreaterThan(0);
        RegisterParameter("max_data_size", MaxDataSize)
            .Default(256_MB)
            .GreaterThan(0);
        RegisterParameter("auto_rotation_period", AutoRotationPeriod)
            .Default(TDuration::Minutes(15));
        RegisterParameter("replay_buffer_size", ReplayBufferSize)
            .GreaterThan(0)
            .Default(256_MB);
        RegisterParameter("max_clean_changelogs_to_keep", MaxCleanChangelogsToKeep)
            .GreaterThanOrEqual(0)
            .Default(3);
        RegisterParameter("clean_delay", CleanDelay)
            .Default(TDuration::Minutes(1));
    }
};

DEFINE_REFCOUNTED_TYPE(TMultiplexedChangelogConfig)

////////////////////////////////////////////////////////////////////////////////

class TArtifactCacheReaderConfig
    : public virtual NChunkClient::TBlockFetcherConfig
    , public virtual NTableClient::TTableReaderConfig
    , public virtual NApi::TFileReaderConfig
{ };

DEFINE_REFCOUNTED_TYPE(TArtifactCacheReaderConfig)

////////////////////////////////////////////////////////////////////////////////

class TLayerLocationConfig
    : public TDiskLocationConfig
{
public:
    //! The location is considered to be full when available space becomes less than #LowWatermark.
    i64 LowWatermark;

    //! Maximum space layers are allowed to occupy.
    //! (If not initialized then indicates to occupy all available space on drive).
    std::optional<i64> Quota;

    bool LocationIsAbsolute;

    TLayerLocationConfig()
    {
        RegisterParameter("low_watermark", LowWatermark)
            .Default(1_GB)
            .GreaterThanOrEqual(0);

        RegisterParameter("quota", Quota)
            .Default();

        RegisterParameter("location_is_absolute", LocationIsAbsolute)
            .Default(true);
    }
};

DEFINE_REFCOUNTED_TYPE(TLayerLocationConfig)

////////////////////////////////////////////////////////////////////////////////

class TTmpfsLayerCacheConfig
    : public NYTree::TYsonSerializable
{
public:
    i64 Capacity;
    std::optional<TString> LayersDirectoryPath;
    TDuration LayersUpdatePeriod;

    TTmpfsLayerCacheConfig()
    {
        RegisterParameter("capacity", Capacity)
            .Default(10 * 1_GB)
            .GreaterThan(0);

        RegisterParameter("layers_directory_path", LayersDirectoryPath)
            .Default(std::nullopt);

        RegisterParameter("layers_update_period", LayersUpdatePeriod)
            .Default(TDuration::Minutes(3))
            .GreaterThan(TDuration::Zero());
    }
};

DEFINE_REFCOUNTED_TYPE(TTmpfsLayerCacheConfig)

////////////////////////////////////////////////////////////////////////////////

class TTableSchemaCacheConfig
    : public TSlruCacheConfig
{
public:
    //! Timeout for table schema request.
    TDuration TableSchemaCacheRequestTimeout;

    TTableSchemaCacheConfig()
    {
        RegisterParameter("table_schema_cache_request_timeout", TableSchemaCacheRequestTimeout)
            .Default(TDuration::Seconds(1));

        RegisterPreprocessor([&] {
            Capacity = 100_MB;
        });
    }
};

DEFINE_REFCOUNTED_TYPE(TTableSchemaCacheConfig)

////////////////////////////////////////////////////////////////////////////////

class TTableSchemaCacheDynamicConfig
    : public TSlruCacheDynamicConfig
{
public:
    std::optional<TDuration> TableSchemaCacheRequestTimeout;

    TTableSchemaCacheDynamicConfig()
    {
        RegisterParameter("table_schema_cache_request_timeout", TableSchemaCacheRequestTimeout)
            .Optional();
    }
};

DEFINE_REFCOUNTED_TYPE(TTableSchemaCacheDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

class TVolumeManagerConfig
    : public NYTree::TYsonSerializable
{
public:
    NContainers::TPortoExecutorConfigPtr PortoExecutor;
    std::vector<TLayerLocationConfigPtr> LayerLocations;
    double CacheCapacityFraction;
    int LayerImportConcurrency;

    TTmpfsLayerCacheConfigPtr TmpfsLayerCache;

    TVolumeManagerConfig()
    {
        RegisterParameter("porto_executor", PortoExecutor)
            .DefaultNew();

        RegisterParameter("layer_locations", LayerLocations);

        RegisterParameter("cache_capacity_fraction", CacheCapacityFraction)
            .Default(0.8)
            .GreaterThan(0)
            .LessThanOrEqual(1);

        RegisterParameter("layer_import_concurrency", LayerImportConcurrency)
            .Default(2)
            .GreaterThan(0)
            .LessThanOrEqual(10);

        RegisterParameter("tmpfs_layer_cache", TmpfsLayerCache)
            .DefaultNew();
    }
};

DEFINE_REFCOUNTED_TYPE(TVolumeManagerConfig)

////////////////////////////////////////////////////////////////////////////////

class TRepairReaderConfig
    : public virtual NChunkClient::TReplicationReaderConfig
    , public virtual NJournalClient::TChunkReaderConfig
{ };

DEFINE_REFCOUNTED_TYPE(TRepairReaderConfig)

////////////////////////////////////////////////////////////////////////////////

class TMediumUpdaterDynamicConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Whether media updater is enabled.
    bool Enabled;
    //! Period of media config fetching from Cypress.
    TDuration Period;

    TMediumUpdaterDynamicConfig()
    {
        RegisterParameter("enabled", Enabled)
            .Default(false);
        RegisterParameter("period", Period)
            .Default(TDuration::Minutes(5));
    }
};

DEFINE_REFCOUNTED_TYPE(TMediumUpdaterDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

// COMPAT(gritukan): Drop all the optionals in this class after configs migration.
class TMasterConnectorConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Period between consequent incremental data node heartbeats.
    std::optional<TDuration> IncrementalHeartbeatPeriod;

    //! Splay for data node heartbeats.
    TDuration IncrementalHeartbeatPeriodSplay;

    //! Period between consequent job heartbeats to a given cell.
    std::optional<TDuration> JobHeartbeatPeriod;

    //! Splay for job heartbeats.
    TDuration JobHeartbeatPeriodSplay;

    //! Timeout for incremental data node heartbeat RPC request.
    std::optional<TDuration> IncrementalHeartbeatTimeout;

    //! Timeout for full data node heartbeat RPC request.
    std::optional<TDuration> FullHeartbeatTimeout;

    //! Timeout for job heartbeat RPC request.
    std::optional<TDuration> JobHeartbeatTimeout;

    TMasterConnectorConfig()
    {
        RegisterParameter("incremental_heartbeat_period", IncrementalHeartbeatPeriod)
            .Default();
        RegisterParameter("incremental_heartbeat_period_splay", IncrementalHeartbeatPeriodSplay)
            .Default(TDuration::Seconds(1));
        RegisterParameter("job_heartbeat_period", JobHeartbeatPeriod)
            .Default();
        RegisterParameter("job_heartbeat_period_splay", JobHeartbeatPeriodSplay)
            .Default(TDuration::Seconds(1));
        RegisterParameter("incremental_heartbeat_timeout", IncrementalHeartbeatTimeout)
            .Default();
        RegisterParameter("full_heartbeat_timeout", FullHeartbeatTimeout)
            .Default();
        RegisterParameter("job_heartbeat_timeout", JobHeartbeatTimeout)
            .Default();
    }
};

DEFINE_REFCOUNTED_TYPE(TMasterConnectorConfig)

////////////////////////////////////////////////////////////////////////////////

class TMasterConnectorDynamicConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Period between consequent incremental data node heartbeats.
    std::optional<TDuration> IncrementalHeartbeatPeriod;

    //! Splay for data node heartbeats.
    std::optional<TDuration> IncrementalHeartbeatPeriodSplay;

    //! Period between consequent job heartbeats to a given cell.
    std::optional<TDuration> JobHeartbeatPeriod;

    //! Splay for job heartbeats.
    std::optional<TDuration> JobHeartbeatPeriodSplay;

    //! Maximum number of chunk events per incremental heartbeat.
    i64 MaxChunkEventsPerIncrementalHeartbeat;

    TMasterConnectorDynamicConfig()
    {
        RegisterParameter("incremental_heartbeat_period", IncrementalHeartbeatPeriod)
            .Default();
        RegisterParameter("incremental_heartbeat_period_splay", IncrementalHeartbeatPeriodSplay)
            .Default();
        RegisterParameter("job_heartbeat_period", JobHeartbeatPeriod)
            .Default();
        RegisterParameter("job_heartbeat_period_splay", JobHeartbeatPeriodSplay)
            .Default();
        RegisterParameter("max_chunk_events_per_incremental_heartbeat", MaxChunkEventsPerIncrementalHeartbeat)
            .Default(1000000);
    }
};

DEFINE_REFCOUNTED_TYPE(TMasterConnectorDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

class TDataNodeConfig
    : public NYTree::TYsonSerializable
{
public:
    //! Timeout for lease transactions.
    TDuration LeaseTransactionTimeout;

    //! Period between consequent lease transaction pings.
    TDuration LeaseTransactionPingPeriod;

    //! Period between consequent incremental heartbeats.
    TDuration IncrementalHeartbeatPeriod;

    //! Splay for incremental heartbeats.
    TDuration IncrementalHeartbeatPeriodSplay;

    //! Controls incremental heartbeats from node to master.
    NConcurrency::TThroughputThrottlerConfigPtr IncrementalHeartbeatThrottler;

    //! Period between consequent registration attempts.
    TDuration RegisterRetryPeriod;

    //! Splay for consequent registration attempts.
    TDuration RegisterRetrySplay;

    //! Timeout for RegisterNode requests.
    TDuration RegisterTimeout;

    //! Timeout for NodeTrackerService:IncrementalHeartbeat requests.
    TDuration IncrementalHeartbeatTimeout;

    //! Timeout for NodeTrackerService:FullHeartbeat requests.
    TDuration FullHeartbeatTimeout;

    //! Timeout for JobTrackerService:Heartbeat requests.
    TDuration JobHeartbeatTimeout;

    //! Cache for chunk metas.
    TSlruCacheConfigPtr ChunkMetaCache;

    //! Cache for blocks extensions.
    TSlruCacheConfigPtr BlocksExtCache;

    //! Cache for partition block metas.
    TSlruCacheConfigPtr BlockMetaCache;

    //! Cache for all types of blocks.
    NChunkClient::TBlockCacheConfigPtr BlockCache;

    //! Opened blob chunks cache.
    TSlruCacheConfigPtr BlobReaderCache;

    //! Opened changelogs cache.
    TSlruCacheConfigPtr ChangelogReaderCache;

    //! Table schema and row key comparer cache.
    TTableSchemaCacheConfigPtr TableSchemaCache;

    //! Multiplexed changelog configuration.
    TMultiplexedChangelogConfigPtr MultiplexedChangelog;

    //! Configuration of per-chunk changelog that backs the multiplexed changelog.
    NHydra::TFileChangelogConfigPtr HighLatencySplitChangelog;

    //! Configuration of per-chunk changelog that is being written directly (w/o multiplexing).
    NHydra::TFileChangelogConfigPtr LowLatencySplitChangelog;

    //! Upload session timeout.
    /*!
     * Some activity must be happening in a session regularly (i.e. new
     * blocks uploaded or sent to other data nodes). Otherwise
     * the session expires.
     */
    TDuration SessionTimeout;

    //! Timeout for "PutBlocks" requests to other data nodes.
    TDuration NodeRpcTimeout;

    //! Period between peer updates (see TBlockPeerUpdater).
    TDuration PeerUpdatePeriod;

    //! Peer update expiration time (see TBlockPeerUpdater).
    TDuration PeerUpdateExpirationTime;

    //! Read requests are throttled when the number of bytes queued at Bus layer exceeds this limit.
    //! This is a global limit.
    //! Cf. TTcpDispatcherStatistics::PendingOutBytes
    i64 NetOutThrottlingLimit;

    TDuration NetOutThrottleDuration;

    //! Write requests are throttled when the number of bytes queued for write exceeds this limit.
    //! This is a per-location limit.
    i64 DiskWriteThrottlingLimit;

    //! Read requests are throttled when the number of bytes scheduled for read exceeds this limit.
    //! This is a per-location limit.
    i64 DiskReadThrottlingLimit;

    //! Regular storage locations.
    std::vector<TStoreLocationConfigPtr> StoreLocations;

    //! Cached chunks location.
    std::vector<TCacheLocationConfigPtr> CacheLocations;

    //! Manages layers and root volumes for Porto job environment.
    TVolumeManagerConfigPtr VolumeManager;

    //! Writer configuration used to replicate chunks.
    NChunkClient::TReplicationWriterConfigPtr ReplicationWriter;

    //! Reader configuration used to repair chunks (both blob and journal).
    TRepairReaderConfigPtr RepairReader;

    //! Writer configuration used to repair chunks.
    NChunkClient::TReplicationWriterConfigPtr RepairWriter;

    //! Reader configuration used to seal chunks.
    NJournalClient::TChunkReaderConfigPtr SealReader;

    //! Reader configuration used to merge chunks.
    NChunkClient::TReplicationReaderConfigPtr MergeReader;

    //! Writer configuration used to merge chunks.
    NChunkClient::TMultiChunkWriterConfigPtr MergeWriter;

    //! Configuration for various Data Node throttlers.
    TEnumIndexedVector<EDataNodeThrottlerKind, NConcurrency::TThroughputThrottlerConfigPtr> Throttlers;

    //! Keeps chunk peering information.
    TBlockPeerTableConfigPtr BlockPeerTable;

    //! Distributes blocks when node is under heavy load.
    TP2PBlockDistributorConfigPtr P2PBlockDistributor;

    //! Runs periodic checks against disks.
    TDiskHealthCheckerConfigPtr DiskHealthChecker;

    //! Maximum number of concurrent balancing write sessions.
    int MaxWriteSessions;

    //! Maximum number of blocks to fetch via a single range request.
    int MaxBlocksPerRead;

    //! Maximum number of bytes to fetch via a single range request.
    i64 MaxBytesPerRead;

    //! Desired number of bytes per disk write in a blob chunks.
    i64 BytesPerWrite;

    //! Enables block checksums validation.
    bool ValidateBlockChecksums;

    //! Use DIRECT_IO flag when writing chunks data to disk.
    EDirectIOPolicy UseDirectIO;

    //! The time after which any registered placement info expires.
    TDuration PlacementExpirationTime;

    //! Controls if cluster and cell directories are to be synchronized on connect.
    //! Useful for tests.
    bool SyncDirectoriesOnConnect;

    //! The number of threads in StorageHeavy thread pool (used for extracting chunk meta, handling
    //! chunk slices, columnar statistic etc).
    int StorageHeavyThreadCount;

    //! The number of threads in StorageLight thread pool (used for reading chunk blocks).
    int StorageLightThreadCount;

    //! Number of threads in DataNodeLookup thread pool (used for row lookups).
    int StorageLookupThreadCount;

    //! Number of replication errors sent in heartbeat.
    int MaxReplicationErrorsInHeartbeat;

    //! Number of tablet errors sent in heartbeat.
    int MaxTabletErrorsInHeartbeat;

    //! Fraction of GetBlockSet/GetBlockRange RPC timeout, after which reading routine tries
    //! to return all blocks read up to moment (in case at least one block is read; otherwise
    //! it still tries to read at least one block).
    double BlockReadTimeoutFraction;

    //! Delay between node initializatin and start of background artifact validation.
    TDuration BackgroundArtifactValidationDelay;

    //! Master connector config.
    TMasterConnectorConfigPtr MasterConnector;

    TDataNodeConfig()
    {
        RegisterParameter("lease_transaction_timeout", LeaseTransactionTimeout)
            .Default(TDuration::Seconds(120));
        RegisterParameter("lease_transaction_ping_period", LeaseTransactionPingPeriod)
            .Default(TDuration::Seconds(15));
        RegisterParameter("incremental_heartbeat_period", IncrementalHeartbeatPeriod)
            .Default(TDuration::Seconds(5));
        RegisterParameter("incremental_heartbeat_period_splay", IncrementalHeartbeatPeriodSplay)
            .Default(TDuration::Seconds(5));
        RegisterParameter("register_retry_period", RegisterRetryPeriod)
            .Default(TDuration::Seconds(3));
        RegisterParameter("register_retry_splay", RegisterRetrySplay)
            .Default(TDuration::Seconds(3));
        RegisterParameter("register_timeout", RegisterTimeout)
            .Default(TDuration::Seconds(60));
        RegisterParameter("incremental_heartbeat_timeout", IncrementalHeartbeatTimeout)
            .Default(TDuration::Seconds(60));
        RegisterParameter("incremental_heartbeat_throttler", IncrementalHeartbeatThrottler)
            .DefaultNew(/* limit */1, /* period */TDuration::Minutes(10));

        RegisterParameter("full_heartbeat_timeout", FullHeartbeatTimeout)
            .Default(TDuration::Seconds(60));
        RegisterParameter("job_heartbeat_timeout", JobHeartbeatTimeout)
            .Default(TDuration::Seconds(60));

        RegisterParameter("chunk_meta_cache", ChunkMetaCache)
            .DefaultNew();
        RegisterParameter("blocks_ext_cache", BlocksExtCache)
            .DefaultNew();
        RegisterParameter("block_meta_cache", BlockMetaCache)
            .DefaultNew();
        RegisterParameter("block_cache", BlockCache)
            .DefaultNew();
        RegisterParameter("blob_reader_cache", BlobReaderCache)
            .DefaultNew();
        RegisterParameter("changelog_reader_cache", ChangelogReaderCache)
            .DefaultNew();
        RegisterParameter("table_schema_cache", TableSchemaCache)
            .DefaultNew();

        RegisterParameter("multiplexed_changelog", MultiplexedChangelog)
            .DefaultNew();
        RegisterParameter("high_latency_split_changelog", HighLatencySplitChangelog)
            .DefaultNew();
        RegisterParameter("low_latency_split_changelog", LowLatencySplitChangelog)
            .DefaultNew();

        RegisterParameter("session_timeout", SessionTimeout)
            .Default(TDuration::Seconds(120));
        RegisterParameter("node_rpc_timeout", NodeRpcTimeout)
            .Default(TDuration::Seconds(120));
        RegisterParameter("peer_update_period", PeerUpdatePeriod)
            .Default(TDuration::Seconds(30));
        RegisterParameter("peer_update_expiration_time", PeerUpdateExpirationTime)
            .Default(TDuration::Seconds(40));

        RegisterParameter("net_out_throttling_limit", NetOutThrottlingLimit)
            .GreaterThan(0)
            .Default(512_MB);
        RegisterParameter("net_out_throttle_duration", NetOutThrottleDuration)
            .Default(TDuration::Seconds(30));

        RegisterParameter("disk_write_throttling_limit", DiskWriteThrottlingLimit)
            .GreaterThan(0)
            .Default(1_GB);
        RegisterParameter("disk_read_throttling_limit", DiskReadThrottlingLimit)
            .GreaterThan(0)
            .Default(512_MB);

        RegisterParameter("store_locations", StoreLocations)
            .Default({});

        RegisterParameter("cache_locations", CacheLocations)
            .NonEmpty();

        RegisterParameter("volume_manager", VolumeManager)
            .DefaultNew();

        RegisterParameter("replication_writer", ReplicationWriter)
            .DefaultNew();
        RegisterParameter("repair_reader", RepairReader)
            .DefaultNew();
        RegisterParameter("repair_writer", RepairWriter)
            .DefaultNew();

        RegisterParameter("seal_reader", SealReader)
            .DefaultNew();

        RegisterParameter("merge_reader", MergeReader)
            .DefaultNew();
        RegisterParameter("merge_writer", MergeWriter)
            .DefaultNew();

        RegisterParameter("throttlers", Throttlers)
            .Optional();

        // COMPAT(babenko): use /data_node/throttlers instead.
        RegisterParameter("total_in_throttler", Throttlers[EDataNodeThrottlerKind::TotalIn])
            .Optional();
        RegisterParameter("total_out_throttler", Throttlers[EDataNodeThrottlerKind::TotalOut])
            .Optional();
        RegisterParameter("replication_in_throttler", Throttlers[EDataNodeThrottlerKind::ReplicationIn])
            .Optional();
        RegisterParameter("replication_out_throttler", Throttlers[EDataNodeThrottlerKind::ReplicationOut])
            .Optional();
        RegisterParameter("repair_in_throttler", Throttlers[EDataNodeThrottlerKind::RepairIn])
            .Optional();
        RegisterParameter("repair_out_throttler", Throttlers[EDataNodeThrottlerKind::RepairOut])
            .Optional();
        RegisterParameter("artifact_cache_in_throttler", Throttlers[EDataNodeThrottlerKind::ArtifactCacheIn])
            .Optional();
        RegisterParameter("artifact_cache_out_throttler", Throttlers[EDataNodeThrottlerKind::ArtifactCacheOut])
            .Optional();
        RegisterParameter("skynet_out_throttler", Throttlers[EDataNodeThrottlerKind::SkynetOut])
            .Optional();
        RegisterParameter("tablet_comaction_and_partitoning_in_throttler", Throttlers[EDataNodeThrottlerKind::TabletCompactionAndPartitioningIn])
            .Optional();
        RegisterParameter("tablet_comaction_and_partitoning_out_throttler", Throttlers[EDataNodeThrottlerKind::TabletCompactionAndPartitioningOut])
            .Optional();
        RegisterParameter("tablet_logging_in_throttler", Throttlers[EDataNodeThrottlerKind::TabletLoggingIn])
            .Optional();
        RegisterParameter("tablet_preload_out_throttler", Throttlers[EDataNodeThrottlerKind::TabletPreloadOut])
            .Optional();
        RegisterParameter("tablet_snapshot_in_throttler", Throttlers[EDataNodeThrottlerKind::TabletSnapshotIn])
            .Optional();
        RegisterParameter("tablet_store_flush_in_throttler", Throttlers[EDataNodeThrottlerKind::TabletStoreFlushIn])
            .Optional();
        RegisterParameter("tablet_recovery_out_throttler", Throttlers[EDataNodeThrottlerKind::TabletRecoveryOut])
            .Optional();
        RegisterParameter("tablet_replication_out_throttler", Throttlers[EDataNodeThrottlerKind::TabletReplicationOut])
            .Optional();
        RegisterParameter("read_rps_out_throttler", Throttlers[EDataNodeThrottlerKind::ReadRpsOut])
            .Optional();

        RegisterParameter("block_peer_table", BlockPeerTable)
            .DefaultNew();
        RegisterParameter("p2p_block_distributor", P2PBlockDistributor)
            .Alias("peer_block_distributor")
            .DefaultNew();

        RegisterParameter("disk_health_checker", DiskHealthChecker)
            .DefaultNew();

        RegisterParameter("max_write_sessions", MaxWriteSessions)
            .Default(1000)
            .GreaterThanOrEqual(1);

        RegisterParameter("max_blocks_per_read", MaxBlocksPerRead)
            .GreaterThan(0)
            .Default(100000);
        RegisterParameter("max_bytes_per_read", MaxBytesPerRead)
            .GreaterThan(0)
            .Default(64_MB);
        RegisterParameter("bytes_per_write", BytesPerWrite)
            .GreaterThan(0)
            .Default(16_MB);

        RegisterParameter("validate_block_checksums", ValidateBlockChecksums)
            .Default(true);

        RegisterParameter("use_direct_io", UseDirectIO)
            .Default(EDirectIOPolicy::Never);

        RegisterParameter("placement_expiration_time", PlacementExpirationTime)
            .Default(TDuration::Hours(1));

        RegisterParameter("sync_directories_on_connect", SyncDirectoriesOnConnect)
            .Default(false);

        RegisterParameter("storage_heavy_thread_count", StorageHeavyThreadCount)
            .GreaterThan(0)
            .Default(2);
        RegisterParameter("storage_light_thread_count", StorageLightThreadCount)
            .GreaterThan(0)
            .Default(2);
        RegisterParameter("storage_lookup_thread_count", StorageLookupThreadCount)
            .GreaterThan(0)
            .Default(2);

        RegisterParameter("max_replication_errors_in_heartbeat", MaxReplicationErrorsInHeartbeat)
            .GreaterThan(0)
            .Default(3);
        RegisterParameter("max_tablet_errors_in_heartbeat", MaxTabletErrorsInHeartbeat)
            .GreaterThan(0)
            .Default(10);

        RegisterParameter("block_read_timeout_fraction", BlockReadTimeoutFraction)
            .Default(0.75);

        RegisterParameter("background_artifact_validation_delay", BackgroundArtifactValidationDelay)
            .Default(TDuration::Minutes(5));

        RegisterParameter("master_connector", MasterConnector)
            .DefaultNew();

        RegisterPreprocessor([&] {
            ChunkMetaCache->Capacity = 1_GB;
            BlocksExtCache->Capacity = 1_GB;
            BlockMetaCache->Capacity = 1_GB;
            BlockCache->CompressedData->Capacity = 1_GB;
            BlockCache->UncompressedData->Capacity = 1_GB;

            BlobReaderCache->Capacity = 256;

            ChangelogReaderCache->Capacity = 256;

            // Expect many splits -- adjust configuration.
            HighLatencySplitChangelog->FlushPeriod = TDuration::Seconds(15);

            // Disable target allocation from master.
            ReplicationWriter->UploadReplicationFactor = 1;
            RepairWriter->UploadReplicationFactor = 1;

            // Use proper workload descriptors.
            // TODO(babenko): avoid passing workload descriptor in config
            RepairWriter->WorkloadDescriptor = TWorkloadDescriptor(EWorkloadCategory::SystemRepair);
            ReplicationWriter->WorkloadDescriptor = TWorkloadDescriptor(EWorkloadCategory::SystemReplication);

            // Don't populate caches in chunk jobs.
            RepairReader->PopulateCache = false;
            RepairReader->RetryTimeout = TDuration::Minutes(15);
            SealReader->PopulateCache = false;

            // Instantiate default throttler configs.
            for (auto kind : TEnumTraits<EDataNodeThrottlerKind>::GetDomainValues()) {
                Throttlers[kind] = New<NConcurrency::TThroughputThrottlerConfig>();
            }
        });

        RegisterPostprocessor([&] {
            // COMPAT(gritukan)
            if (!MasterConnector->IncrementalHeartbeatPeriod) {
                MasterConnector->IncrementalHeartbeatPeriod = IncrementalHeartbeatPeriod;
            }
            if (!MasterConnector->JobHeartbeatPeriod) {
                // This is not a mistake!
                MasterConnector->JobHeartbeatPeriod = IncrementalHeartbeatPeriod;
            }
            if (!MasterConnector->FullHeartbeatTimeout) {
                MasterConnector->FullHeartbeatTimeout = FullHeartbeatTimeout;
            }
            if (!MasterConnector->IncrementalHeartbeatTimeout) {
                MasterConnector->IncrementalHeartbeatTimeout = IncrementalHeartbeatTimeout;
            }
            if (!MasterConnector->JobHeartbeatTimeout) {
                MasterConnector->JobHeartbeatTimeout = JobHeartbeatTimeout;
            }
        });
    }

    i64 GetCacheCapacity() const
    {
        i64 capacity = 0;
        for (const auto& config : CacheLocations) {
            if (config->Quota) {
                capacity += *config->Quota;
            } else {
                return std::numeric_limits<i64>::max();
            }
        }
        return capacity;
    }
};

DEFINE_REFCOUNTED_TYPE(TDataNodeConfig)

////////////////////////////////////////////////////////////////////////////////

class TDataNodeDynamicConfig
    : public NYTree::TYsonSerializable
{
public:
    std::optional<int> StorageHeavyThreadCount;
    std::optional<int> StorageLightThreadCount;
    std::optional<int> StorageLookupThreadCount;

    TEnumIndexedVector<EDataNodeThrottlerKind, NConcurrency::TThroughputThrottlerConfigPtr> Throttlers;

    TSlruCacheDynamicConfigPtr ChunkMetaCache;
    TSlruCacheDynamicConfigPtr BlocksExtCache;
    TSlruCacheDynamicConfigPtr BlockMetaCache;
    NChunkClient::TBlockCacheDynamicConfigPtr BlockCache;
    TSlruCacheDynamicConfigPtr BlobReaderCache;
    TSlruCacheDynamicConfigPtr ChangelogReaderCache;
    TTableSchemaCacheDynamicConfigPtr TableSchemaCache;

    TMasterConnectorDynamicConfigPtr MasterConnector;
    TMediumUpdaterDynamicConfigPtr MediumUpdater;

    //! Prepared chunk readers are kept open during this period of time after the last use.
    TDuration ChunkReaderRetentionTimeout;

    //! Reader configuration used to download chunks into cache.
    TArtifactCacheReaderConfigPtr ArtifactCacheReader;

    TDataNodeDynamicConfig()
    {
        RegisterParameter("storage_heavy_thread_count", StorageHeavyThreadCount)
            .GreaterThan(0)
            .Optional();
        RegisterParameter("storage_light_thread_count", StorageLightThreadCount)
            .GreaterThan(0)
            .Optional();
        RegisterParameter("storage_lookup_thread_count", StorageLookupThreadCount)
            .GreaterThan(0)
            .Optional();

        RegisterParameter("throttlers", Throttlers)
            .Optional();

        RegisterParameter("chunk_meta_cache", ChunkMetaCache)
            .DefaultNew();
        RegisterParameter("blocks_ext_cache", BlocksExtCache)
            .DefaultNew();
        RegisterParameter("block_meta_cache", BlockMetaCache)
            .DefaultNew();
        RegisterParameter("block_cache", BlockCache)
            .DefaultNew();
        RegisterParameter("blob_reader_cache", BlobReaderCache)
            .DefaultNew();
        RegisterParameter("changelog_reader_cache", ChangelogReaderCache)
            .DefaultNew();
        RegisterParameter("table_schema_cache", TableSchemaCache)
            .DefaultNew();

        RegisterParameter("master_connector", MasterConnector)
            .DefaultNew();
        RegisterParameter("medium_updater", MediumUpdater)
            .DefaultNew();

        RegisterParameter("chunk_reader_retention_timeout", ChunkReaderRetentionTimeout)
            .Default(TDuration::Minutes(1));

        RegisterParameter("artifact_cache_reader", ArtifactCacheReader)
            .DefaultNew();
    }
};

DEFINE_REFCOUNTED_TYPE(TDataNodeDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
