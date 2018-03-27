#pragma once
#include "public.h"

#include <yt/core/ytree/interned_attributes.h>

#define FOR_EACH_INTERNED_ATTRIBUTE(XX) \
    XX(AccessCounter, access_counter) \
    XX(AccessTime, access_time) \
    XX(Account, account) \
    XX(Acl, acl) \
    XX(ActionId, action_id) \
    XX(ActionIds, action_ids) \
    XX(Addresses, addresses) \
    XX(AlertCount, alert_count) \
    XX(Alerts, alerts) \
    XX(Atomicity, atomicity) \
    XX(AttributeKey, attribute_key) \
    XX(Available, available) \
    XX(AvailableSpace, available_space) \
    XX(AvailableSpacePerMedium, available_space_per_medium) \
    XX(Banned, banned) \
    XX(BannedNodeCount, banned_node_count) \
    XX(BranchedNodeIds, branched_node_ids) \
    XX(Broken, broken) \
    XX(Builtin, builtin) \
    XX(Cache, cache) \
    XX(CachedReplicas, cached_replicas) \
    XX(CellId, cell_id) \
    XX(CellIds, cell_ids) \
    XX(CellTag, cell_tag) \
    XX(ChildCount, child_count) \
    XX(ChildIds, child_ids) \
    XX(ChildKey, child_key) \
    XX(ChunkCount, chunk_count) \
    XX(ChunkIds, chunk_ids) \
    XX(ChunkListId, chunk_list_id) \
    XX(ChunkReplicaCount, chunk_replica_count) \
    XX(ChunkReplicatorEnabled, chunk_replicator_enabled) \
    XX(ChunkRowCount, chunk_row_count) \
    XX(ChunkType, chunk_type) \
    XX(ChunkWriter, chunk_writer) \
    XX(ClusterName, cluster_name) \
    XX(CommitOrdering, commit_ordering) \
    XX(CommittedResourceUsage, committed_resource_usage) \
    XX(CompressedDataSize, compressed_data_size) \
    XX(CompressionCodec, compression_codec) \
    XX(CompressionRatio, compression_ratio) \
    XX(CompressionStatistics, compression_statistics) \
    XX(Config, config) \
    XX(ConfigVersion, config_version) \
    XX(Confirmed, confirmed) \
    XX(CountByHealth, count_by_health) \
    XX(CreationTime, creation_time) \
    XX(CurrentCommitRevision, current_commit_revision) \
    XX(DataCenter, data_center) \
    XX(DataWeight, data_weight) \
    XX(Decommissioned, decommissioned) \
    XX(DecommissionedNodeCount, decommissioned_node_count) \
    XX(DependentTransactionIds, DependentTransactionIds) \
    XX(DesiredTabletCount, desired_tablet_count) \
    XX(DesiredTabletSize, desired_tablet_size) \
    XX(DisableSchedulerJobs, disable_scheduler_jobs) \
    XX(DisableTabletBalancer, disable_tablet_balancer) \
    XX(DisableTabletCells, disable_tablet_cells) \
    XX(DisableWriteSessions, disable_write_sessions) \
    XX(DiskSpace, disk_space) \
    XX(Dynamic, dynamic) \
    XX(Eden, eden) \
    XX(EffectiveAcl, effective_acl) \
    XX(EnableTabletBalancer, enable_tablet_balancer) \
    XX(EphemeralRefCounter, ephemeral_ref_counter) \
    XX(ErasureCodec, erasure_codec) \
    XX(ErasureStatistics, erasure_statistics) \
    XX(Error, error) \
    XX(Errors, errors) \
    XX(Executable, executable) \
    XX(ExpirationTime, expiration_time) \
    XX(ExportedObjectCount, exported_object_count) \
    XX(ExportedObjects, exported_objects) \
    XX(Exports, exports) \
    XX(External, external) \
    XX(ExternalCellTag, external_cell_tag) \
    XX(ExternalRequisitionIndexes, external_requisition_indexes) \
    XX(ExternalRequisitions, external_requisitions) \
    XX(FileName, file_name) \
    XX(FlushLagTime, flush_lag_time) \
    XX(FlushedRowCount, flushed_row_count) \
    XX(Foreign, foreign) \
    XX(Freeze, freeze) \
    XX(Full, full) \
    XX(FullNodeCount, full_node_count) \
    XX(Health, health) \
    XX(Id, id) \
    XX(Implicit, implicit) \
    XX(ImportRefCounter, import_ref_counter) \
    XX(ImportedObjectCount, imported_object_count) \
    XX(ImportedObjectIds, imported_object_ids) \
    XX(InMemoryMode, in_memory_mode) \
    XX(Index, index) \
    XX(InheritAcl, inherit_acl) \
    XX(IOWeights, io_weights) \
    XX(KeepFinished, keep_finished) \
    XX(Key, key) \
    XX(KeyColumns, key_columns) \
    XX(Kind, kind) \
    XX(LastCommitTimestamp, last_commit_timestamp) \
    XX(LastPingTime, last_ping_time) \
    XX(LastSeenReplicas, last_seen_replicas) \
    XX(LastSeenTime, last_seen_time) \
    XX(LastWriteTimestamp, last_write_timestamp) \
    XX(LeadingPeerId, leading_peer_id) \
    XX(LeaseTransactionId, lease_transaction_id) \
    XX(LifeStage, life_stage) \
    XX(LocalRequisition, local_requisition) \
    XX(LocalRequisitionIndex, local_requisition_index) \
    XX(LockCount, lock_count) \
    XX(LockIds, lock_ids) \
    XX(LockMode, lock_mode) \
    XX(LockedNodeIds, locked_node_ids) \
    XX(Locks, locks) \
    XX(MasterMetaSize, master_meta_size) \
    XX(MaxBlockSize, max_block_size) \
    XX(MaxKey, max_key) \
    XX(MaxTabletSize, max_tablet_size) \
    XX(MaxTimestamp, max_timestamp) \
    XX(MD5, md5) \
    XX(Media, media) \
    XX(MemberOf, member_of) \
    XX(MemberOfClosure, member_of_closure) \
    XX(Members, members) \
    XX(MetaSize, meta_size) \
    XX(MinKey, min_key) \
    XX(MinTabletSize, min_tablet_size) \
    XX(MinTimestamp, min_timestamp) \
    XX(Mixed, mixed) \
    XX(Mode, mode) \
    XX(ModificationTime, modification_time) \
    XX(MountRevision, mount_revision) \
    XX(Movable, movable) \
    XX(MulticellCount, multicell_count) \
    XX(MulticellResourceUsage, multicell_resource_usage) \
    XX(MulticellStates, multicell_states) \
    XX(MulticellStatistics, multicell_statistics) \
    XX(Name, name) \
    XX(NestedTransactionIds, nested_transaction_ids) \
    XX(NodeId, node_id) \
    XX(NodeTagFilter, node_tag_filter) \
    XX(Nodes, nodes) \
    XX(Offline, offline) \
    XX(OfflineNodeCount, offline_node_count) \
    XX(Online, online) \
    XX(OnlineNodeCount, online_node_count) \
    XX(Opaque, opaque) \
    XX(OptimizeFor, optimize_for) \
    XX(OptimizeForStatistics, optimize_for_statistics) \
    XX(Options, options) \
    XX(Owner, owner) \
    XX(OwningNodes, owning_nodes) \
    XX(ParentId, parent_id) \
    XX(ParentIds, parent_ids) \
    XX(Path, path) \
    XX(Peers, peers) \
    XX(PerformanceCounters, performance_counters) \
    XX(PivotKey, pivot_key) \
    XX(PivotKeys, pivot_keys) \
    XX(PrerequisiteTransactionId, prerequisite_transaction_id) \
    XX(PrerequisiteTransactionIds, PrerequisiteTransactionIds) \
    XX(PrimaryCellId, primary_cell_id) \
    XX(PrimaryCellTag, primary_cell_tag) \
    XX(PrimaryMedium, primary_medium) \
    XX(Priority, priority) \
    XX(QuorumRowCount, quorum_row_count) \
    XX(Rack, rack) \
    XX(Racks, racks) \
    XX(ReadQuorum, read_quorum) \
    XX(ReadRequestTime, read_request_time) \
    XX(RecursiveResourceUsage, recursive_resource_usage) \
    XX(RefCounter, ref_counter) \
    XX(RegisterTime, register_time) \
    XX(Registered, registered) \
    XX(RegisteredMasterCellTags, registered_master_cell_tags) \
    XX(ReplicaPath, replica_path) \
    XX(Replicas, replicas) \
    XX(ReplicationFactor, replication_factor) \
    XX(ReplicationLagTime, replication_lag_time) \
    XX(ReplicationStatus, replication_status) \
    XX(RequestCount, request_count) \
    XX(RequestQueueSizeLimit, request_queue_size_limit) \
    XX(RequestRateLimit, request_rate_limit) \
    XX(Requisition, requisition) \
    XX(ResourceLimits, resource_limits) \
    XX(ResourceLimitsOverrides, resource_limits_overrides) \
    XX(ResourceUsage, resource_usage) \
    XX(RetainedTimestamp, retained_timestamp) \
    XX(Revision, revision) \
    XX(RowCount, row_count) \
    XX(ScanFlags, scan_flags) \
    XX(Schema, schema) \
    XX(SchemaMode, schema_mode) \
    XX(Sealed, sealed) \
    XX(SecondaryCellTags, secondary_cell_tags) \
    XX(SkipFreezing, skip_freezing) \
    XX(Sorted, sorted) \
    XX(SortedBy, sorted_by) \
    XX(StagedNodeIds, staged_node_ids) \
    XX(StagedObjectIds, staged_object_ids) \
    XX(StagingAccount, staging_account) \
    XX(StagingTransactionId, staging_transaction_id) \
    XX(StartReplicationTimestamp, start_replication_timestamp) \
    XX(StartTime, start_time) \
    XX(State, state) \
    XX(Statistics, statistics) \
    XX(StoredReplicas, stored_replicas) \
    XX(StoresUpdatePrepared, stores_update_prepared) \
    XX(StoresUpdatePreparedTransactionId, stores_update_prepared_transaction_id) \
    XX(System, system) \
    XX(TableChunkFormat, table_chunk_format) \
    XX(TableChunkFormatStatistics, table_chunk_format_statistics) \
    XX(TableId, table_id) \
    XX(TablePath, table_path) \
    XX(TabletBalancerConfig, tablet_balancer_config) \
    XX(TabletCellBundle, tablet_cell_bundle) \
    XX(TabletCellCount, tablet_cell_count) \
    XX(TabletCellIds, tablet_cell_ids) \
    XX(TabletCount, tablet_count) \
    XX(TabletCountByState, tablet_count_by_state) \
    XX(TabletErrorCount, tablet_error_count) \
    XX(TabletErrors, tablet_errors) \
    XX(TabletIds, tablet_ids) \
    XX(TabletSlots, tablet_slots) \
    XX(TabletState, tablet_state) \
    XX(TabletStatistics, tablet_statistics) \
    XX(Tablets, tablets) \
    XX(Tags, tags) \
    XX(TargetPath, target_path) \
    XX(Timeout, timeout) \
    XX(Title, title) \
    XX(TotalStatistics, total_statistics) \
    XX(TransactionId, transaction_id) \
    XX(Transient, transient) \
    XX(Tree, tree) \
    XX(TrimmedChildCount, trimmed_child_count) \
    XX(TrimmedRowCount, trimmed_row_count) \
    XX(Type, type) \
    XX(UncompressedDataSize, uncompressed_data_size) \
    XX(UnflushedTimestamp, unflushed_timestamp) \
    XX(UnmergedRowCount, unmerged_row_count) \
    XX(Unregistered, unregistered) \
    XX(UpdateMode, update_mode) \
    XX(UpstreamReplicaId, upstream_replica_id) \
    XX(UsableAccounts, usable_accounts) \
    XX(UsedSpace, used_space) \
    XX(UsedSpacePerMedium, used_space_per_medium) \
    XX(UserAttributeKeys, user_attribute_keys) \
    XX(UserTags, user_tags) \
    XX(Value, value) \
    XX(ValueCount, value_count) \
    XX(ViolatedResourceLimits, violated_resource_limits) \
    XX(Vital, vital) \
    XX(WeakRefCounter, weak_ref_counter) \
    XX(WithAlertsNodeCount, with_alerts_node_count) \
    XX(WriteQuorum, write_quorum) \
    XX(WriteRequestTime, write_request_time)

namespace NYT {
namespace NObjectServer {

///////////////////////////////////////////////////////////////////////////////

// Don't litter the namespace, yet at the same time make the "enum" items
// implicitly castable to NYTree::TInternedAttributeKey (aka int).
struct EInternedAttributeKey
{
    enum : NYTree::TInternedAttributeKey
    {
        InvalidKey = NYTree::InvalidInternedAttribute,

        Count = NYTree::CountInternedAttribute,

#define XX(camelCaseName, snakeCaseName) camelCaseName,
    FOR_EACH_INTERNED_ATTRIBUTE(XX)
#undef XX
    };
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT
