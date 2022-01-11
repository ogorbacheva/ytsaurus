#pragma once

#include "public.h"
#include "dynamic_store_bits.h"
#include "store.h"

#include <yt/yt/server/node/cluster_node/public.h>

#include <yt/yt/server/node/data_node/public.h>

#include <yt/yt/client/table_client/schema.h>

#include <yt/yt_proto/yt/client/chunk_client/proto/chunk_meta.pb.h>

#include <yt/yt/client/node_tracker_client/node_directory.h>

#include <yt/yt/ytlib/api/native/public.h>

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

#include <yt/yt/core/actions/signal.h>

#include <yt/yt/core/logging/log.h>

#include <library/cpp/yt/threading/rw_spin_lock.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TStoreBase
    : public virtual IStore
{
public:
    TStoreBase(
        TTabletManagerConfigPtr config,
        TStoreId id,
        TTablet* tablet);
    ~TStoreBase();

    // IStore implementation.
    TStoreId GetId() const override;
    TTablet* GetTablet() const override;

    bool IsEmpty() const override;

    EStoreState GetStoreState() const override;
    void SetStoreState(EStoreState state) override;

    void SetMemoryTracker(NClusterNode::TNodeMemoryTrackerPtr memoryTracker);
    i64 GetDynamicMemoryUsage() const override;

    void Initialize() override;

    void Save(TSaveContext& context) const override;
    void Load(TLoadContext& context) override;

    void BuildOrchidYson(NYTree::TFluentMap fluent) override;

protected:
    const TTabletManagerConfigPtr Config_;
    const TStoreId StoreId_;
    TTablet* const Tablet_;

    const TTabletPerformanceCountersPtr PerformanceCounters_;
    const TRuntimeTabletDataPtr RuntimeData_;
    const TTabletId TabletId_;
    const NYPath::TYPath TablePath_;
    const NTableClient::TTableSchemaPtr Schema_;
    const int KeyColumnCount_;
    const int SchemaColumnCount_;
    const int ColumnLockCount_;
    const std::vector<TString> LockIndexToName_;
    const std::vector<int> ColumnIndexToLockIndex_;

    EStoreState StoreState_ = EStoreState::Undefined;

    const NLogging::TLogger Logger;

    NClusterNode::TNodeMemoryTrackerPtr MemoryTracker_;
    TMemoryUsageTrackerGuard DynamicMemoryTrackerGuard_;


    TLegacyOwningKey RowToKey(TUnversionedRow row) const;
    TLegacyOwningKey RowToKey(TSortedDynamicRow row) const;

    virtual NNodeTrackerClient::EMemoryCategory GetMemoryCategory() const = 0;

    void SetDynamicMemoryUsage(i64 value);

private:
    i64 DynamicMemoryUsage_ = 0;

    static ETabletDynamicMemoryType DynamicMemoryTypeFromState(EStoreState state);
    void UpdateTabletDynamicMemoryUsage(i64 multiplier);
};

////////////////////////////////////////////////////////////////////////////////

class TDynamicStoreBase
    : public TStoreBase
    , public IDynamicStore
{
public:
    TDynamicStoreBase(
        TTabletManagerConfigPtr config,
        TStoreId id,
        TTablet* tablet);

    i64 Lock();
    i64 Unlock();

    // IStore implementation.
    TTimestamp GetMinTimestamp() const override;
    TTimestamp GetMaxTimestamp() const override;

    //! Sets the store state, as expected.
    //! Additionally, when the store transitions from |ActiveDynamic| to |PassiveDynamic|,
    //! invokes #OnSetPassive.
    void SetStoreState(EStoreState state) override;

    i64 GetCompressedDataSize() const override;
    i64 GetUncompressedDataSize() const override;

    // IDynamicStore implementation.
    EStoreFlushState GetFlushState() const override;
    void SetFlushState(EStoreFlushState state) override;

    i64 GetValueCount() const override;
    i64 GetLockCount() const override;

    i64 GetPoolSize() const override;
    i64 GetPoolCapacity() const override;

    TInstant GetLastFlushAttemptTimestamp() const override;
    void UpdateFlushAttemptTimestamp() override;

    void BuildOrchidYson(NYTree::TFluentMap fluent) override;

    bool IsDynamic() const override;
    IDynamicStorePtr AsDynamic() override;

    void SetBackupCheckpointTimestamp(TTimestamp timestamp) override;

protected:
    //! Some sanity checks may need the tablet's atomicity mode but the tablet may die.
    //! So we capture a copy of this mode upon store's construction.
    const NTransactionClient::EAtomicity Atomicity_;
    const NTableClient::TRowBufferPtr RowBuffer_;

    TTimestamp MinTimestamp_ = NTransactionClient::MaxTimestamp;
    TTimestamp MaxTimestamp_ = NTransactionClient::MinTimestamp;

    EStoreFlushState FlushState_ = EStoreFlushState::None;
    TInstant LastFlushAttemptTimestamp_;

    i64 StoreLockCount_ = 0;
    i64 StoreValueCount_ = 0;

    void UpdateTimestampRange(TTimestamp commitTimestamp);

    virtual void OnSetPassive() = 0;

    NNodeTrackerClient::EMemoryCategory GetMemoryCategory() const override;
};

////////////////////////////////////////////////////////////////////////////////

class TChunkStoreBase
    : public TStoreBase
    , public IChunkStore
{
public:
    TChunkStoreBase(
        IBootstrap* bootstrap,
        TTabletManagerConfigPtr config,
        TStoreId id,
        NChunkClient::TChunkId chunkId,
        TTimestamp overrideTimestamp,
        TTablet* tablet,
        const NTabletNode::NProto::TAddStoreDescriptor* addStoreDescriptor,
        NChunkClient::IBlockCachePtr blockCache,
        IVersionedChunkMetaManagerPtr chunkMetaManager,
        NDataNode::IChunkRegistryPtr chunkRegistry,
        NDataNode::IChunkBlockManagerPtr chunkBlockManager,
        NApi::NNative::IClientPtr client,
        const NNodeTrackerClient::TNodeDescriptor& localDescriptor);

    void Initialize() override;

    // IStore implementation.
    TTimestamp GetMinTimestamp() const override;
    TTimestamp GetMaxTimestamp() const override;

    i64 GetCompressedDataSize() const override;
    i64 GetUncompressedDataSize() const override;
    i64 GetRowCount() const override;

    void Save(TSaveContext& context) const override;
    void Load(TLoadContext& context) override;

    TCallback<void(TSaveContext&)> AsyncSave() override;
    void AsyncLoad(TLoadContext& context) override;

    void BuildOrchidYson(NYTree::TFluentMap fluent) override;

    // IChunkStore implementation.
    TInstant GetCreationTime() const override;

    void SetBackingStore(IDynamicStorePtr store) override;
    bool HasBackingStore() const override;
    IDynamicStorePtr GetBackingStore() override;

    EStorePreloadState GetPreloadState() const override;
    void SetPreloadState(EStorePreloadState state) override;

    bool IsPreloadAllowed() const override;
    void UpdatePreloadAttempt(bool isBackoff) override;

    TFuture<void> GetPreloadFuture() const override;
    void SetPreloadFuture(TFuture<void> future) override;

    EStoreCompactionState GetCompactionState() const override;
    void SetCompactionState(EStoreCompactionState state) override;

    void UpdateCompactionAttempt() override;
    TInstant GetLastCompactionTimestamp() const override;

    bool IsChunk() const override;
    IChunkStorePtr AsChunk() override;

    TReaders GetReaders(std::optional<EWorkloadCategory> workloadCategory) override;
    TTabletStoreReaderConfigPtr GetReaderConfig() override;
    void InvalidateCachedReaders(const TTableSettings& settings) override;

    NTabletClient::EInMemoryMode GetInMemoryMode() const override;
    void SetInMemoryMode(NTabletClient::EInMemoryMode mode) override;

    void Preload(TInMemoryChunkDataPtr chunkData) override;

    NChunkClient::TChunkId GetChunkId() const override;
    TTimestamp GetOverrideTimestamp() const override;

    NChunkClient::TChunkReplicaList GetReplicas(
        NNodeTrackerClient::TNodeId localNodeId) const override;

    const NChunkClient::NProto::TChunkMeta& GetChunkMeta() const override;

    const std::vector<THunkChunkRef>& HunkChunkRefs() const override;

protected:
    IBootstrap* const Bootstrap_;
    const NChunkClient::IBlockCachePtr BlockCache_;
    const IVersionedChunkMetaManagerPtr ChunkMetaManager_;
    const NDataNode::IChunkRegistryPtr ChunkRegistry_;
    const NDataNode::IChunkBlockManagerPtr ChunkBlockManager_;
    const NApi::NNative::IClientPtr Client_;
    const NNodeTrackerClient::TNodeDescriptor LocalDescriptor_;

    std::vector<THunkChunkRef> HunkChunkRefs_;

    NTabletClient::EInMemoryMode InMemoryMode_ = NTabletClient::EInMemoryMode::None;
    EStorePreloadState PreloadState_ = EStorePreloadState::None;
    TInstant AllowedPreloadTimestamp_;
    TFuture<void> PreloadFuture_;
    TPreloadedBlockCachePtr PreloadedBlockCache_;
    NTableClient::TChunkStatePtr ChunkState_;

    EStoreCompactionState CompactionState_ = EStoreCompactionState::None;
    TInstant LastCompactionTimestamp_;

    YT_DECLARE_SPIN_LOCK(NThreading::TReaderWriterSpinLock, VersionedChunkMetaLock_);
    TWeakPtr<TVersionedChunkMetaCacheEntry> CachedWeakVersionedChunkMeta_;

    // Cached for fast retrieval from ChunkMeta_.
    NChunkClient::NProto::TMiscExt MiscExt_;
    NChunkClient::TRefCountedChunkMetaPtr ChunkMeta_;

    NChunkClient::TChunkId ChunkId_;

    TTimestamp OverrideTimestamp_;

    void OnLocalReaderFailed();

    NChunkClient::IBlockCachePtr GetBlockCache();

    NNodeTrackerClient::EMemoryCategory GetMemoryCategory() const override;

    NTableClient::TChunkStatePtr FindPreloadedChunkState();

    NTableClient::TCachedVersionedChunkMetaPtr GetCachedVersionedChunkMeta(
        const NChunkClient::IChunkReaderPtr& chunkReader,
        const NChunkClient::TClientChunkReadOptions& chunkReadOptions,
        bool prepareColumnarMeta = false);

    virtual NTableClient::TKeyComparer GetKeyComparer() = 0;

private:
    YT_DECLARE_SPIN_LOCK(NThreading::TReaderWriterSpinLock, ReaderLock_);
    NProfiling::TCpuInstant ChunkReaderEvictionDeadline_ = 0;
    TReaders CachedReaders_;
    THashMap<std::optional<EWorkloadCategory>, TReaders> CachedRemoteReaderAdapters_;
    bool CachedReadersLocal_ = false;
    TWeakPtr<NDataNode::IChunk> CachedWeakChunk_;
    TTabletStoreReaderConfigPtr ReaderConfig_;

    IDynamicStorePtr BackingStore_;

    NChunkClient::IBlockCachePtr DoGetBlockCache();

    bool IsLocalChunkValid(const NDataNode::IChunkPtr& chunk) const;

    void DoInvalidateCachedReaders();

    friend TPreloadedBlockCache;
};

////////////////////////////////////////////////////////////////////////////////

class TSortedStoreBase
    : public ISortedStore
{
public:
    TPartition* GetPartition() const override;
    void SetPartition(TPartition* partition) override;

    bool IsSorted() const override;
    ISortedStorePtr AsSorted() override;

protected:
    TPartition* Partition_ = nullptr;
};

////////////////////////////////////////////////////////////////////////////////

class TOrderedStoreBase
    : public IOrderedStore
{
public:
    bool IsOrdered() const override;
    IOrderedStorePtr AsOrdered() override;

    i64 GetStartingRowIndex() const override;
    void SetStartingRowIndex(i64 value) override;

    void Save(TSaveContext& context) const override;
    void Load(TLoadContext& context) override;

protected:
    i64 StartingRowIndex_ = 0;
};

////////////////////////////////////////////////////////////////////////////////

TLegacyOwningKey RowToKey(const TTableSchema& schema, TSortedDynamicRow row);

TTimestamp CalculateRetainedTimestamp(TTimestamp currentTimestamp, TDuration minDataTtl);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
