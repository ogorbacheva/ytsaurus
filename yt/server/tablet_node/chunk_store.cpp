#include "stdafx.h"
#include "chunk_store.h"
#include "tablet.h"
#include "config.h"
#include "automaton.h"
#include "transaction.h"

#include <core/concurrency/scheduler.h>
#include <core/concurrency/delayed_executor.h>
#include <core/concurrency/thread_affinity.h>

#include <core/ytree/fluent.h>

#include <core/misc/protobuf_helpers.h>

#include <ytlib/object_client/helpers.h>

#include <ytlib/new_table_client/versioned_reader.h>
#include <ytlib/new_table_client/versioned_chunk_reader.h>
#include <ytlib/new_table_client/versioned_lookuper.h>
#include <ytlib/new_table_client/versioned_chunk_lookuper.h>
#include <ytlib/new_table_client/cached_versioned_chunk_meta.h>
#include <ytlib/new_table_client/chunk_meta_extensions.h>

#include <ytlib/api/client.h>

#include <ytlib/chunk_client/chunk_reader.h>
#include <ytlib/chunk_client/replication_reader.h>
#include <ytlib/chunk_client/read_limit.h>
#include <ytlib/chunk_client/block_cache.h>
#include <ytlib/chunk_client/chunk_meta_extensions.h>

#include <ytlib/transaction_client/helpers.h>

#include <server/data_node/local_chunk_reader.h>
#include <server/data_node/block_store.h>
#include <server/data_node/chunk_registry.h>
#include <server/data_node/chunk.h>

#include <server/query_agent/config.h>

#include <server/cell_node/bootstrap.h>
#include <server/cell_node/config.h>

namespace NYT {
namespace NTabletNode {

using namespace NConcurrency;
using namespace NYTree;
using namespace NYson;
using namespace NRpc;
using namespace NObjectClient;
using namespace NVersionedTableClient;
using namespace NVersionedTableClient::NProto;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NNodeTrackerClient;
using namespace NTransactionClient;
using namespace NDataNode;
using namespace NCellNode;
using namespace NQueryAgent;

using NChunkClient::TReadLimit;

////////////////////////////////////////////////////////////////////////////////

static const auto ChunkExpirationTimeout = TDuration::Seconds(15);
static const auto ChunkReaderExpirationTimeout = TDuration::Seconds(15);

////////////////////////////////////////////////////////////////////////////////

class TChunkStore::TLocalChunkReaderWrapper
    : public IChunkReader
{
public:
    TLocalChunkReaderWrapper(
        IChunkReaderPtr underlyingReader,
        TChunkStorePtr owner)
        : UnderlyingReader_(std::move(underlyingReader))
        , Owner_(std::move(owner))
    { }

    virtual TFuture<std::vector<TSharedRef>> ReadBlocks(const std::vector<int>& blockIndexes) override
    {
        return UnderlyingReader_->ReadBlocks(blockIndexes).Apply(
            BIND([=, this_ = MakeStrong(this)] (const TErrorOr<std::vector<TSharedRef>>& result) -> std::vector<TSharedRef> {
                return CheckResult(result);
            }));
    }

    virtual TFuture<std::vector<TSharedRef>> ReadBlocks(int firstBlockIndex, int blockCount) override
    {
        return UnderlyingReader_->ReadBlocks(firstBlockIndex, blockCount).Apply(
            BIND([=, this_ = MakeStrong(this)] (const TErrorOr<std::vector<TSharedRef>>& result) -> std::vector<TSharedRef> {
                return CheckResult(result);
            }));
    }

    virtual TFuture<TChunkMeta> GetMeta(
        const TNullable<int>& partitionTag,
        const TNullable<std::vector<int>>& extensionTags) override
    {
        return UnderlyingReader_->GetMeta(partitionTag, extensionTags).Apply(
            BIND([=, this_ = MakeStrong(this)] (const TErrorOr<TChunkMeta>& result) -> TChunkMeta {
                return CheckResult(result);
            }));
    }

    virtual TChunkId GetChunkId() const override
    {
        return UnderlyingReader_->GetChunkId();
    }

private:
    const IChunkReaderPtr UnderlyingReader_;
    const TChunkStorePtr Owner_;


    template <class T>
    T CheckResult(const TErrorOr<T>& result)
    {
        if (!result.IsOK()) {
            Owner_->OnLocalReaderFailed();
            THROW_ERROR(result);
        }
        return result.Value();
    }

};

////////////////////////////////////////////////////////////////////////////////

class TChunkStore::TVersionedReaderWrapper
    : public IVersionedReader
{
public:
    TVersionedReaderWrapper(
        IVersionedReaderPtr underlyingReader,
        TTabletPerformanceCountersPtr performanceCounters)
        : UnderlyingReader_(std::move(underlyingReader))
        , PerformanceCounters_(std::move(performanceCounters))
    { }

    virtual TFuture<void> Open() override
    {
        return UnderlyingReader_->Open();
    }

    virtual bool Read(std::vector<TVersionedRow>* rows) override
    {
        auto result = UnderlyingReader_->Read(rows);
        if (result) {
            PerformanceCounters_->StaticChunkRowReadCount += rows->size();
        }
        return result;
    }

    virtual TFuture <void> GetReadyEvent() override
    {
        return UnderlyingReader_->GetReadyEvent();
    }

private:
    const IVersionedReaderPtr UnderlyingReader_;
    const TTabletPerformanceCountersPtr PerformanceCounters_;

};

////////////////////////////////////////////////////////////////////////////////

class TChunkStore::TVersionedLookuperWrapper
    : public IVersionedLookuper
{
public:
    TVersionedLookuperWrapper(
        IVersionedLookuperPtr underlyingReader,
        TTabletPerformanceCountersPtr performanceCounters)
        : UnderlyingLookuper_(std::move(underlyingReader))
        , PerformanceCounters_(std::move(performanceCounters))
    { }

    virtual TFutureHolder<TVersionedRow> Lookup(TKey key) override
    {
        ++PerformanceCounters_->StaticChunkRowLookupCount;
        return UnderlyingLookuper_->Lookup(key);
    }

private:
    const IVersionedLookuperPtr UnderlyingLookuper_;
    const TTabletPerformanceCountersPtr PerformanceCounters_;

};

////////////////////////////////////////////////////////////////////////////////

class TChunkStore::TBlockCache
    : public IBlockCache
{
public:
    explicit TBlockCache(const TChunkId& chunkId)
        : ChunkId_(chunkId)
    { }

    virtual void Put(
        const TBlockId& id,
        const TSharedRef& data,
        const TNullable<NNodeTrackerClient::TNodeDescriptor>& /*source*/) override
    {
        YASSERT(id.ChunkId == ChunkId_);

        TWriterGuard guard(SpinLock_);
        if (id.BlockIndex >= Blocks_.size()) {
            Blocks_.resize(id.BlockIndex + 1);
        }
        Blocks_[id.BlockIndex] = data;
    }

    virtual TSharedRef Find(const TBlockId& id) override
    {
        YASSERT(id.ChunkId == ChunkId_);

        TReaderGuard guard(SpinLock_);
        return id.BlockIndex < Blocks_.size() ? Blocks_[id.BlockIndex] : TSharedRef();
    }

private:
    const TChunkId ChunkId_;

    TReaderWriterSpinLock SpinLock_;
    std::vector<TSharedRef> Blocks_;

};

////////////////////////////////////////////////////////////////////////////////

TChunkStore::TChunkStore(
    const TStoreId& id,
    TTablet* tablet,
    const TChunkMeta* chunkMeta,
    TBootstrap* boostrap)
    : TStoreBase(
        id,
        tablet)
    , Bootstrap_(boostrap)
{
    YCHECK(
        TypeFromId(StoreId_) == EObjectType::Chunk ||
        TypeFromId(StoreId_) == EObjectType::ErasureChunk);

    StoreState_ = EStoreState::Persistent;

    if (chunkMeta) {
        ChunkMeta_ = *chunkMeta;
        PrecacheProperties();
    }

    SetInMemory(Tablet_->GetConfig()->InMemory);

    LOG_DEBUG("Static chunk store created (TabletId: %v)",
        TabletId_);
}

TChunkStore::~TChunkStore()
{
    LOG_DEBUG("Static chunk store destroyed");
}

const TChunkMeta& TChunkStore::GetChunkMeta() const
{
    return ChunkMeta_;
}

void TChunkStore::SetBackingStore(IStorePtr store)
{
    TWriterGuard guard(BackingStoreLock_);
    BackingStore_ = store;
}

bool TChunkStore::HasBackingStore() const
{
    TReaderGuard guard(BackingStoreLock_);
    return BackingStore_ != nullptr;
}

void TChunkStore::SetInMemory(bool value)
{
    VERIFY_THREAD_AFFINITY_ANY();

    TWriterGuard guard(UncompressedPreloadedBlockCacheLock_);
    if (value) {
        if (!UncompressedPreloadedBlockCache_) {
            UncompressedPreloadedBlockCache_ = New<TBlockCache>(StoreId_);
        }
        switch (PreloadState_) {
            case EStorePreloadState::Disabled:
            case EStorePreloadState::Failed:
                PreloadState_ = EStorePreloadState::None;
                break;
            case EStorePreloadState::Scheduled:
            case EStorePreloadState::Running:
            case EStorePreloadState::Complete:
                break;
            default:
                YUNREACHABLE();
        }
    } else {
        UncompressedPreloadedBlockCache_.Reset();
        PreloadState_ = EStorePreloadState::Disabled;
    }
}

IBlockCachePtr TChunkStore::GetUncompressedPreloadedBlockCache()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(UncompressedPreloadedBlockCacheLock_);
    return UncompressedPreloadedBlockCache_;
}


NChunkClient::IChunkReaderPtr TChunkStore::GetChunkReader()
{
    VERIFY_THREAD_AFFINITY_ANY();

    auto chunk = PrepareChunk();
    return PrepareChunkReader(chunk);
}

EStorePreloadState TChunkStore::GetPreloadState() const
{
    return PreloadState_;
}

void TChunkStore::SetPreloadState(EStorePreloadState value)
{
    PreloadState_ = value;
}

EStoreType TChunkStore::GetType() const
{
    return EStoreType::Chunk;
}

i64 TChunkStore::GetUncompressedDataSize() const
{
    return DataSize_;
}

i64 TChunkStore::GetRowCount() const
{
    return RowCount_;
}

TOwningKey TChunkStore::GetMinKey() const
{
    return MinKey_;
}

TOwningKey TChunkStore::GetMaxKey() const
{
    return MaxKey_;
}

TTimestamp TChunkStore::GetMinTimestamp() const
{
    return MinTimestamp_;
}

TTimestamp TChunkStore::GetMaxTimestamp() const
{
    return MaxTimestamp_;
}

IVersionedReaderPtr TChunkStore::CreateReader(
    TOwningKey lowerKey,
    TOwningKey upperKey,
    TTimestamp timestamp,
    const TColumnFilter& columnFilter)
{
    VERIFY_THREAD_AFFINITY_ANY();

    if (upperKey <= MinKey_ || lowerKey > MaxKey_) {
        return nullptr;
    }

    auto backingStore = GetBackingStore();
    if (backingStore) {
        return backingStore->CreateReader(
            std::move(lowerKey),
            std::move(upperKey),
            timestamp,
            columnFilter);
    }

    auto uncompressedBlockCache = GetUncompressedBlockCache();
    auto chunk = PrepareChunk();
    auto chunkReader = PrepareChunkReader(chunk);
    auto cachedVersionedChunkMeta = PrepareCachedVersionedChunkMeta(chunkReader);

    TReadLimit lowerLimit;
    lowerLimit.SetKey(std::move(lowerKey));

    TReadLimit upperLimit;
    upperLimit.SetKey(std::move(upperKey));

    auto versionedReader = CreateVersionedChunkReader(
        Bootstrap_->GetConfig()->TabletNode->ChunkReader,
        std::move(chunkReader),
        std::move(uncompressedBlockCache),
        std::move(cachedVersionedChunkMeta),
        lowerLimit,
        upperLimit,
        columnFilter,
        timestamp);

    return New<TVersionedReaderWrapper>(std::move(versionedReader), PerformanceCounters_);
}

IVersionedLookuperPtr TChunkStore::CreateLookuper(
    TTimestamp timestamp,
    const TColumnFilter& columnFilter)
{
    VERIFY_THREAD_AFFINITY_ANY();

    auto backingStore = GetBackingStore();
    if (backingStore) {
        return backingStore->CreateLookuper(timestamp, columnFilter);
    }

    auto uncompressedBlockCache = GetUncompressedBlockCache();
    auto chunk = PrepareChunk();
    auto chunkReader = PrepareChunkReader(chunk);
    auto cachedVersionedChunkMeta = PrepareCachedVersionedChunkMeta(chunkReader);

    auto versionedLookuper = CreateVersionedChunkLookuper(
        Bootstrap_->GetConfig()->TabletNode->ChunkReader,
        std::move(chunkReader),
        std::move(uncompressedBlockCache),
        std::move(cachedVersionedChunkMeta),
        columnFilter,
        timestamp);

    return New<TVersionedLookuperWrapper>(std::move(versionedLookuper), PerformanceCounters_);
}

void TChunkStore::CheckRowLocks(
    TKey key,
    TTransaction* transaction,
    ui32 lockMask)
{
    auto backingStore = GetBackingStore();
    if (backingStore) {
        return backingStore->CheckRowLocks(key, transaction, lockMask);
    }

    THROW_ERROR_EXCEPTION(
        "Checking for transaction conflicts against chunk stores is not supported; "
        "consider reducing transaction duration or increasing store retention time")
        << TErrorAttribute("transaction_id", transaction->GetId())
        << TErrorAttribute("transaction_start_time", transaction->GetStartTime())
        << TErrorAttribute("transaction_register_time", transaction->GetRegisterTime())
        << TErrorAttribute("tablet_id", TabletId_)
        << TErrorAttribute("store_id", StoreId_)
        << TErrorAttribute("key", key);
}

void TChunkStore::Save(TSaveContext& context) const
{
    TStoreBase::Save(context);

    using NYT::Save;

    Save(context, GetPersistentStoreState());
    Save(context, ChunkMeta_);
}

void TChunkStore::Load(TLoadContext& context)
{
    TStoreBase::Load(context);

    using NYT::Load;

    Load(context, StoreState_);
    Load(context, ChunkMeta_);

    PrecacheProperties();
}

void TChunkStore::BuildOrchidYson(IYsonConsumer* consumer)
{
    TStoreBase::BuildOrchidYson(consumer);

    auto backingStore = GetBackingStore();
    auto miscExt = GetProtoExtension<TMiscExt>(ChunkMeta_.extensions());
    BuildYsonMapFluently(consumer)
        .Item("preload_state").Value(PreloadState_)
        .Item("compressed_data_size").Value(miscExt.compressed_data_size())
        .Item("uncompressed_data_size").Value(miscExt.uncompressed_data_size())
        .Item("key_count").Value(miscExt.row_count())
        .DoIf(backingStore, [&] (TFluentMap fluent) {
            fluent.Item("backing_store_id").Value(backingStore->GetId());
        });
}

IChunkPtr TChunkStore::PrepareChunk()
{
    VERIFY_THREAD_AFFINITY_ANY();

    {
        TReaderGuard guard(ChunkLock_);
        if (ChunkInitialized_) {
            return Chunk_;
        }
    }

    auto chunkRegistry = Bootstrap_->GetChunkRegistry();
    auto asyncChunk = BIND(&TChunkStore::DoFindChunk, MakeStrong(this))
        .AsyncVia(Bootstrap_->GetControlInvoker())
        .Run();
    auto chunkOrError = WaitFor(asyncChunk);
    if (!chunkOrError.IsOK()) {
        return nullptr;
    }
    const auto& chunk = chunkOrError.Value();

    {
        TWriterGuard guard(ChunkLock_);
        ChunkInitialized_ = true;
        Chunk_ = chunk;
    }

    TDelayedExecutor::Submit(
        BIND([=, this_ = MakeStrong(this)] () {
            TWriterGuard guard(ChunkLock_);
            ChunkInitialized_ = false;
            Chunk_.Reset();
        }),
        ChunkExpirationTimeout);

    return chunk;
}

IChunkPtr TChunkStore::DoFindChunk()
{
    auto chunkRegistry = Bootstrap_->GetChunkRegistry();
    auto chunk = chunkRegistry->FindChunk(StoreId_);
    if (!chunk) {
        return nullptr;
    }
    if (chunk->IsRemoveScheduled()) {
        return nullptr;
    }
    return chunk;
}

IChunkReaderPtr TChunkStore::PrepareChunkReader(IChunkPtr chunk)
{
    VERIFY_THREAD_AFFINITY_ANY();

    {
        TReaderGuard guard(ChunkReaderLock_);
        if (ChunkReader_) {
            return ChunkReader_;
        }
    }

    IChunkReaderPtr chunkReader;
    if (chunk) {
        auto localChunkReader = CreateLocalChunkReader(
            Bootstrap_,
            Bootstrap_->GetConfig()->TabletNode->ChunkReader,
            chunk);
        chunkReader = New<TLocalChunkReaderWrapper>(
            localChunkReader,
            this);
    } else {
        // TODO(babenko): provide seed replicas
        chunkReader = CreateReplicationReader(
            Bootstrap_->GetConfig()->TabletNode->ChunkReader,
            Bootstrap_->GetBlockStore()->GetCompressedBlockCache(),
            Bootstrap_->GetMasterClient()->GetMasterChannel(NApi::EMasterChannelKind::LeaderOrFollower),
            New<TNodeDirectory>(),
            Bootstrap_->GetLocalDescriptor(),
            StoreId_);
    }

    {
        TWriterGuard guard(ChunkReaderLock_);
        ChunkReader_ = chunkReader;
    }

    TDelayedExecutor::Submit(
        BIND([=, this_ = MakeStrong(this)] () {
            TWriterGuard guard(ChunkReaderLock_);
            ChunkReader_.Reset();
        }),
        ChunkReaderExpirationTimeout);

    return chunkReader;
}

TCachedVersionedChunkMetaPtr TChunkStore::PrepareCachedVersionedChunkMeta(IChunkReaderPtr chunkReader)
{
    VERIFY_THREAD_AFFINITY_ANY();

    {
        TReaderGuard guard(CachedVersionedChunkMetaLock_);
        if (CachedVersionedChunkMeta_) {
            return CachedVersionedChunkMeta_;
        }
    }

    auto cachedMetaOrError = WaitFor(TCachedVersionedChunkMeta::Load(
        chunkReader,
        Schema_,
        KeyColumns_));
    THROW_ERROR_EXCEPTION_IF_FAILED(cachedMetaOrError);
    auto cachedMeta = cachedMetaOrError.Value();

    {
        TWriterGuard guard(CachedVersionedChunkMetaLock_);
        CachedVersionedChunkMeta_ = cachedMeta;
    }

    return cachedMeta;
}

IStorePtr TChunkStore::GetBackingStore()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(UncompressedPreloadedBlockCacheLock_);
    return BackingStore_;
}

IBlockCachePtr TChunkStore::GetUncompressedBlockCache()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(UncompressedPreloadedBlockCacheLock_);
    return UncompressedPreloadedBlockCache_
        ? UncompressedPreloadedBlockCache_
        : Bootstrap_->GetUncompressedBlockCache();
}

void TChunkStore::PrecacheProperties()
{
    // Precache frequently used values.
    auto miscExt = GetProtoExtension<TMiscExt>(ChunkMeta_.extensions());
    DataSize_ = miscExt.uncompressed_data_size();
    RowCount_ = miscExt.row_count();
    MinTimestamp_ = miscExt.min_timestamp();
    MaxTimestamp_ = miscExt.max_timestamp();

    auto boundaryKeysExt = GetProtoExtension<TBoundaryKeysExt>(ChunkMeta_.extensions());
    MinKey_ = FromProto<TOwningKey>(boundaryKeysExt.min());
    MaxKey_ = FromProto<TOwningKey>(boundaryKeysExt.max());
}

void TChunkStore::OnLocalReaderFailed()
{
    VERIFY_THREAD_AFFINITY_ANY();

    {
        TWriterGuard guard(ChunkLock_);
        ChunkInitialized_ = false;
        Chunk_.Reset();
    }
    {
        TWriterGuard guard(ChunkReaderLock_);
        ChunkReader_.Reset();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT

