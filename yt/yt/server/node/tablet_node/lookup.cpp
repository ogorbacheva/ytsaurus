#include "lookup.h"
#include "private.h"
#include "hedging_manager_registry.h"
#include "store.h"
#include "tablet.h"
#include "tablet_profiling.h"
#include "tablet_reader.h"
#include "tablet_slot.h"
#include "tablet_snapshot_store.h"

#include <yt/yt/server/node/query_agent/helpers.h>

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/server/lib/misc/profiling_helpers.h>

#include <yt/yt/client/chunk_client/data_statistics.h>

#include <yt/yt/ytlib/chunk_client/public.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_options.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_statistics.h>

#include <yt/yt/ytlib/table_client/config.h>
#include <yt/yt/ytlib/table_client/hunks.h>
#include <yt/yt/ytlib/table_client/row_merger.h>

#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/versioned_reader.h>

#include <yt/yt/client/table_client/wire_protocol.h>

#include <yt/yt_proto/yt/client/table_chunk_format/proto/wire_protocol.pb.h>

#include <yt/yt/client/transaction_client/helpers.h>

#include <yt/yt/core/actions/future.h>

#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/profiling/profile_manager.h>
#include <yt/yt/core/profiling/profiler.h>
#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/misc/optional.h>
#include <yt/yt/core/misc/protobuf_helpers.h>
#include <yt/yt/core/misc/tls_cache.h>

#include <library/cpp/yt/small_containers/compact_vector.h>

////////////////////////////////////////////////////////////////////////////////

namespace NYT::NTabletNode {

using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NConcurrency;
using namespace NProfiling;
using namespace NTableClient;
using namespace NTabletClient;

using NYT::FromProto;

using NTransactionClient::TReadTimestampRange;

////////////////////////////////////////////////////////////////////////////////

static constexpr i64 RowBufferCapacity = 1000;

struct TLookupSessionBufferTag
{ };

struct TLookupRowsBufferTag
{ };

////////////////////////////////////////////////////////////////////////////////

class TLookupSession;
using TLookupSessionPtr = TIntrusivePtr<TLookupSession>;

struct TTabletLookupRequest;

template <class TPipeline>
class TTabletLookupSession;

struct TPartitionSession;

class TStoreSession;

////////////////////////////////////////////////////////////////////////////////

class TUnversionedAdapter
{
protected:
    using TMutableRow = TMutableUnversionedRow;

    const std::unique_ptr<IWireProtocolWriter> Writer_;

    TSchemafulRowMerger Merger_;

    TUnversionedAdapter(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TColumnFilter& columnFilter,
        const TRetentionConfigPtr& /*retentionConfig*/,
        const TReadTimestampRange& timestampRange)
        : Writer_(CreateWireProtocolWriter())
        , Merger_(
            New<TRowBuffer>(TLookupSessionBufferTag()),
            tabletSnapshot->PhysicalSchema->GetColumnCount(),
            tabletSnapshot->PhysicalSchema->GetKeyColumnCount(),
            columnFilter,
            tabletSnapshot->ColumnEvaluator,
            timestampRange.RetentionTimestamp)
    { }

    void WriteRow(TUnversionedRow row)
    {
        Writer_->WriteSchemafulRow(row);
    }
};

class TVersionedAdapter
{
protected:
    using TMutableRow = TMutableVersionedRow;

    const std::unique_ptr<IWireProtocolWriter> Writer_;

    TVersionedRowMerger Merger_;

    TVersionedAdapter(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TColumnFilter& columnFilter,
        const TRetentionConfigPtr& retentionConfig,
        const TReadTimestampRange& timestampRange)
        : Writer_(CreateWireProtocolWriter())
        , Merger_(
            New<TRowBuffer>(TLookupSessionBufferTag()),
            tabletSnapshot->PhysicalSchema->GetColumnCount(),
            tabletSnapshot->PhysicalSchema->GetKeyColumnCount(),
            columnFilter,
            retentionConfig,
            timestampRange.Timestamp,
            MinTimestamp,
            tabletSnapshot->ColumnEvaluator,
            /*lookup*/ true,
            /*mergeRowsOnFlush*/ false)
    { }

    void WriteRow(TVersionedRow row)
    {
        Writer_->WriteVersionedRow(row);
    }
};

////////////////////////////////////////////////////////////////////////////////

template <class TRowAdapter>
class TSimplePipeline
    : protected TRowAdapter
{
protected:
    using typename TRowAdapter::TMutableRow;

    DEFINE_BYVAL_RO_PROPERTY(int, FoundRowCount, 0);
    DEFINE_BYVAL_RO_PROPERTY(i64, FoundDataWeight, 0);

protected:
    TSimplePipeline(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TColumnFilter& columnFilter,
        const TRetentionConfigPtr& retentionConfig,
        const TReadTimestampRange& timestampRange,
        const NChunkClient::TClientChunkReadOptions& /*chunkReadOptions*/,
        const std::optional<TString>& /*profilingUser*/,
        NLogging::TLogger /*logger*/)
        : TRowAdapter(
            tabletSnapshot,
            columnFilter,
            retentionConfig,
            timestampRange)
        , Timestamp_(timestampRange.Timestamp)
    { }

    TSharedRange<TUnversionedRow> Initialize(TSharedRange<TUnversionedRow> lookupKeys)
    {
        return lookupKeys;
    }

    bool IsLookupInChunkNeeded(int /*index*/) const
    {
        return true;
    }

    TTimestamp GetReadTimestamp() const
    {
        return Timestamp_;
    }

    void AddPartialRow(TVersionedRow partialRow, TTimestamp timestamp, bool /*activeStore*/)
    {
        Merger_.AddPartialRow(partialRow, timestamp);
    }

    TMutableRow GetMergedRow()
    {
        auto mergedRow = Merger_.BuildMergedRow();
        FoundRowCount_ += static_cast<bool>(mergedRow);
        FoundDataWeight_ += GetDataWeight(mergedRow);
        return mergedRow;
    }

    void FinishRow()
    {
        TRowAdapter::WriteRow(GetMergedRow());
    }

    TFuture<std::vector<TSharedRef>> PostprocessTabletLookup(TRefCountedPtr /*owner*/)
    {
        return MakeFuture(Writer_->Finish());
    }

private:
    using TRowAdapter::Merger_;
    using TRowAdapter::Writer_;

    const TTimestamp Timestamp_;
};

template <class TRowAdapter>
class TRowCachePipeline
    : protected TRowAdapter
{
protected:
    using TMutableRow = TMutableVersionedRow;

    DEFINE_BYVAL_RO_PROPERTY(int, FoundRowCount, 0);
    DEFINE_BYVAL_RO_PROPERTY(i64, FoundDataWeight, 0);

protected:
    TRowCachePipeline(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TColumnFilter& columnFilter,
        const TRetentionConfigPtr& retentionConfig,
        const TReadTimestampRange& timestampRange,
        const NChunkClient::TClientChunkReadOptions& /*chunkReadOptions*/,
        const std::optional<TString>& profilingUser,
        NLogging::TLogger logger)
        : TRowAdapter(
            tabletSnapshot,
            columnFilter,
            retentionConfig,
            timestampRange)
        , TabletId_(tabletSnapshot->TabletId)
        , TableProfiler_(tabletSnapshot->TableProfiler)
        , RowCache_(tabletSnapshot->RowCache)
        , ProfilingUser_(profilingUser)
        , Timestamp_(timestampRange.Timestamp)
        , RetainedTimestamp_(tabletSnapshot->RetainedTimestamp)
        , StoreFlushIndex_(tabletSnapshot->StoreFlushIndex)
        , Logger(std::move(logger))
        , CacheRowMerger_(
            RowBuffer_,
            tabletSnapshot->PhysicalSchema->GetColumnCount(),
            tabletSnapshot->PhysicalSchema->GetKeyColumnCount(),
            TColumnFilter::MakeUniversal(),
            tabletSnapshot->Settings.MountConfig,
            GetCompactionTimestamp(tabletSnapshot->Settings.MountConfig, RetainedTimestamp_, Logger),
            MaxTimestamp, // Do not consider major timestamp.
            tabletSnapshot->ColumnEvaluator,
            /*lookup*/ true, // Do not produce sentinel rows.
            /*mergeRowsOnFlush*/ true) // Always merge rows on flush.
    { }

    ~TRowCachePipeline()
    {
        auto* counters = TableProfiler_->GetLookupCounters(ProfilingUser_);

        counters->CacheHits.Increment(CacheHits_);
        counters->CacheOutdated.Increment(CacheOutdated_);
        counters->CacheMisses.Increment(CacheMisses_);
        counters->CacheInserts.Increment(CacheInserts_);
    }

    TSharedRange<TUnversionedRow> Initialize(const TSharedRange<TUnversionedRow>& lookupKeys)
    {
        std::vector<TUnversionedRow> chunkLookupKeys;

        YT_LOG_DEBUG("Lookup in row cache started");

        auto flushIndex = RowCache_->GetFlushIndex();

        CacheLookuper_ = RowCache_->GetCache()->GetLookuper();
        CacheInserter_ = RowCache_->GetCache()->GetInserter();
        for (auto key : lookupKeys) {
            auto foundItemRef = CacheLookuper_(key);
            auto foundItem = foundItemRef.Get();

            if (foundItem) {
                // If table is frozen both revisions are zero.
                if (foundItem->Revision.load(std::memory_order_acquire) >= flushIndex) {
                    ++CacheHits_;
                    YT_LOG_TRACE("Row found (Key: %v)", key);
                    RowsFromCache_.push_back(std::move(foundItemRef));
                    continue;
                } else {
                    ++CacheOutdated_;
                }
            } else {
                ++CacheMisses_;
                YT_LOG_TRACE("Row not found (Key: %v)", key);
            }

            chunkLookupKeys.push_back(key);
            RowsFromCache_.emplace_back();
        }

        YT_LOG_DEBUG("Lookup in row cache finished"
            "(CacheHits: %v, CacheOutdated: %v, CacheMisses: %v)",
            CacheHits_,
            CacheOutdated_,
            CacheMisses_);

        RowsFromActiveStore_.resize(RowsFromCache_.size());
        return MakeSharedRange(std::move(chunkLookupKeys), lookupKeys);
    }

    TTimestamp GetReadTimestamp() const
    {
        // When using lookup cache we must read all versions.
        // It is safe to change fixed timestamp to SyncLastCommitted and drop newer than timestamp versions
        // in row merger.
        auto readTimestamp = Timestamp_ != AsyncLastCommittedTimestamp
            ? SyncLastCommittedTimestamp
            : Timestamp_;

        return readTimestamp;
    }

    bool IsLookupInChunkNeeded(int keyIndex) const
    {
        return !RowsFromCache_[keyIndex];
    }

    void AddPartialRow(TVersionedRow partialRow, TTimestamp /*timestamp*/, bool activeStore)
    {
        if (IsLookupInChunkNeeded(CurrentRowIndex_)) {
            // The only purpose of it is memory consumption optimization.
            // It does not affect correctness.
            // Make sense if row is absent in cache.
            // We must include values from active dynamic store in result, but we want to
            // minimize memory consumption in row cache and do not add values in CacheRowMerger_.
            // So we preserve row from active store and add only key to row cache.
            if (activeStore) {
                // Add key without values.
                CacheRowMerger_.AddPartialRow(partialRow, MinTimestamp);
                RowsFromActiveStore_[CurrentRowIndex_] = RowBuffer_->CaptureRow(partialRow);
            } else {
                CacheRowMerger_.AddPartialRow(partialRow, MaxTimestamp);
            }
        } else {
            // CacheRowMerger_ performs compaction with MergeRowsOnFlush option and uses max MajorTimestamp.
            // It can be done if we have all versions of row.
            // Otherwise it can drop delete timestamps before earliestWriteTimestamp.
            // In this case some versions are read from cache.
            // So we need to use row merger without compaction.
            SimpleRowMerger_.AddPartialRow(partialRow);
        }
    }

    TMutableVersionedRow GetMergedRow()
    {
        // For non cached rows (IsLookupInChunkNeeded() == true) use CacheRowMerger_.
        // For cached rows use simple CacheMerger_ which merges rows into one without compaction.

        auto mergedRow = IsLookupInChunkNeeded(CurrentRowIndex_)
            ? CacheRowMerger_.BuildMergedRow()
            : SimpleRowMerger_.BuildMergedRow(RowBuffer_);
        auto rowFromActiveStore = RowsFromActiveStore_[CurrentRowIndex_];

        FoundRowCount_ += static_cast<bool>(mergedRow) | static_cast<bool>(rowFromActiveStore);
        FoundDataWeight_ += GetDataWeight(mergedRow) + GetDataWeight(rowFromActiveStore);
        ++CurrentRowIndex_;
        return mergedRow;
    }

    void FinishRow()
    {
        auto mergedRow = GetMergedRow();
        WriteRow(mergedRow);
    }

    void WriteRow(TVersionedRow lookupedRow)
    {
        Merger_.AddPartialRow(lookupedRow, Timestamp_ + 1);

        auto cachedItemRef = std::move(RowsFromCache_[WriteRowIndex_]);

        if (auto cachedItemHead = cachedItemRef.Get()) {
            auto cachedItem = GetLatestRow(cachedItemHead);

            if (Timestamp_ < cachedItem->RetainedTimestamp) {
                THROW_ERROR_EXCEPTION("Timestamp %llx is less than retained timestamp %llx of cached row in tablet %v",
                    Timestamp_,
                    cachedItem->RetainedTimestamp,
                    TabletId_);
            }

            YT_LOG_TRACE("Using row from cache (CacheRow: %v, Revision: %v, ReadTimestamp: %llx)",
                cachedItem->GetVersionedRow(),
                cachedItem->Revision.load(),
                Timestamp_);

            Merger_.AddPartialRow(cachedItem->GetVersionedRow(), Timestamp_ + 1);

            // Reinsert row here.
            // TODO(lukyan): Move into function UpdateRow(cachedItemRef, inserter, cachedItem)
            auto lookupTable = CacheInserter_.GetTable();
            if (lookupTable == cachedItemRef.Origin) {
                YT_LOG_TRACE("Updating row");
                cachedItemRef.Update(std::move(cachedItem), cachedItemHead.Get());
            } else {
                YT_LOG_TRACE("Reinserting row");
                lookupTable->Insert(std::move(cachedItem));
            }
        } else {
            Merger_.AddPartialRow(RowsFromActiveStore_[WriteRowIndex_], Timestamp_ + 1);

            auto cachedItem = CachedRowFromVersionedRow(
                RowCache_->GetAllocator(),
                lookupedRow,
                RetainedTimestamp_);

            if (cachedItem) {
                YT_VERIFY(cachedItem->GetVersionedRow().GetKeyCount() > 0);

                auto revision = StoreFlushIndex_;
                cachedItem->Revision.store(revision, std::memory_order_release);

                YT_LOG_TRACE("Populating cache (Row: %v, Revision: %v)",
                    cachedItem->GetVersionedRow(),
                    revision);
                CacheInserter_.GetTable()->Insert(cachedItem);

                auto flushIndex = RowCache_->GetFlushIndex();

                // Row revision is equal to flushRevision if the last passive dynamic store has started flushing.
                if (revision >= flushIndex) {
                    cachedItem->Revision.compare_exchange_strong(revision, std::numeric_limits<ui32>::max());
                }

                ++CacheInserts_;
            }
        }

        ++WriteRowIndex_;

        auto mergedRow = Merger_.BuildMergedRow();
        TRowAdapter::WriteRow(mergedRow);
    }

    TFuture<std::vector<TSharedRef>> PostprocessTabletLookup(TRefCountedPtr /*owner*/)
    {
        return MakeFuture(Writer_->Finish());
    }

private:
    class TSimpleRowMerger
    {
    public:
        void AddPartialRow(TVersionedRow row)
        {
            if (!row) {
                return;
            }

            if (!Started_) {
                Started_ = true;
                Keys_.resize(row.GetKeyCount());
                std::copy(row.BeginKeys(), row.EndKeys(), Keys_.data());
            } else {
                YT_VERIFY(std::ssize(Keys_) == row.GetKeyCount());
            }

            for (auto it = row.BeginValues(); it != row.EndValues(); ++it) {
                Values_.push_back(*it);
            }

            for (auto it = row.BeginDeleteTimestamps(); it != row.EndDeleteTimestamps(); ++it) {
                DeleteTimestamps_.push_back(*it);
            }

            for (auto it = row.BeginWriteTimestamps(); it != row.EndWriteTimestamps(); ++it) {
                WriteTimestamps_.push_back(*it);
            }
        }

        TMutableVersionedRow BuildMergedRow(const TRowBufferPtr& rowBuffer)
        {
            if (!Started_) {
                return {};
            }

            std::sort(DeleteTimestamps_.begin(), DeleteTimestamps_.end(), [] (auto lhs, auto rhs) {
                return lhs > rhs;
            });
            DeleteTimestamps_.erase(
                std::unique(DeleteTimestamps_.begin(), DeleteTimestamps_.end()),
                DeleteTimestamps_.end());

            std::sort(WriteTimestamps_.begin(), WriteTimestamps_.end(), [] (auto lhs, auto rhs) {
                return lhs > rhs;
            });
            WriteTimestamps_.erase(
                std::unique(WriteTimestamps_.begin(), WriteTimestamps_.end()),
                WriteTimestamps_.end());

            // Sort input values by |(id, timestamp)| and remove duplicates.
            std::sort(
                Values_.begin(),
                Values_.end(),
                [&] (const TVersionedValue& lhs, const TVersionedValue& rhs) {
                    return lhs.Id != rhs.Id ? lhs.Id < rhs.Id : lhs.Timestamp > rhs.Timestamp;
                });
            Values_.erase(
                std::unique(
                    Values_.begin(),
                    Values_.end(),
                    [] (const TVersionedValue& lhs, const TVersionedValue& rhs) {
                        return std::tie(lhs.Id, lhs.Timestamp) == std::tie(rhs.Id, rhs.Timestamp);
                    }),
                Values_.end());


            // Construct output row.
            auto row = rowBuffer->AllocateVersioned(
                Keys_.size(),
                Values_.size(),
                WriteTimestamps_.size(),
                DeleteTimestamps_.size());

            // Construct output keys.
            std::copy(Keys_.begin(), Keys_.end(), row.BeginKeys());

            // Construct output values.
            std::copy(Values_.begin(), Values_.end(), row.BeginValues());

            // Construct output timestamps.
            std::copy(WriteTimestamps_.begin(), WriteTimestamps_.end(), row.BeginWriteTimestamps());
            std::copy(DeleteTimestamps_.begin(), DeleteTimestamps_.end(), row.BeginDeleteTimestamps());

            Cleanup();

            return row;
        }

        void Cleanup()
        {
            Started_ = false;

            Keys_.clear();
            Values_.clear();
            WriteTimestamps_.clear();
            DeleteTimestamps_.clear();
        }

    private:
        bool Started_ = false;

        std::vector<TUnversionedValue> Keys_;
        std::vector<TVersionedValue> Values_;
        std::vector<TTimestamp> WriteTimestamps_;
        std::vector<TTimestamp> DeleteTimestamps_;
    };

    using TRowAdapter::Merger_;
    using TRowAdapter::Writer_;

    const TTabletId TabletId_;
    const TTableProfilerPtr TableProfiler_;
    const TRowCachePtr RowCache_;
    const std::optional<TString> ProfilingUser_;
    const TTimestamp Timestamp_;
    const TTimestamp RetainedTimestamp_;
    const ui32 StoreFlushIndex_;
    const NLogging::TLogger Logger;
    const TRowBufferPtr RowBuffer_ = New<TRowBuffer>(TLookupSessionBufferTag());

    TVersionedRowMerger CacheRowMerger_;
    TSimpleRowMerger SimpleRowMerger_;

    // Holds references to lookup tables.
    TConcurrentCache<TCachedRow>::TLookuper CacheLookuper_;
    TConcurrentCache<TCachedRow>::TInserter CacheInserter_;
    std::vector<TConcurrentCache<TCachedRow>::TCachedItemRef> RowsFromCache_;
    std::vector<TVersionedRow> RowsFromActiveStore_;

    // Assume that rows are finished and written in order.
    int CurrentRowIndex_ = 0;
    int WriteRowIndex_ = 0;

    int CacheHits_ = 0;
    int CacheMisses_ = 0;
    int CacheOutdated_ = 0;
    int CacheInserts_ = 0;

    static TTimestamp GetCompactionTimestamp(
        const TTableMountConfigPtr& mountConfig,
        TTimestamp retainedTimestamp,
        const NLogging::TLogger& Logger)
    {
        auto compactionTimestamp = NTransactionClient::InstantToTimestamp(
            NTransactionClient::TimestampToInstant(retainedTimestamp).first + mountConfig->MinDataTtl).first;

        YT_LOG_DEBUG("Creating row merger for row cache (CompactionTimestamp: %llx)",
            compactionTimestamp);

        return compactionTimestamp;
    }
};

template <class TBasePipeline>
class THunkDecodingPipeline
    : protected TBasePipeline
{
protected:
    THunkDecodingPipeline(
        const TTabletSnapshotPtr& tabletSnapshot,
        const TColumnFilter& columnFilter,
        const TRetentionConfigPtr& retentionConfig,
        const TReadTimestampRange& timestampRange,
        const NChunkClient::TClientChunkReadOptions& chunkReadOptions,
        const std::optional<TString>& profilingUser,
        const NLogging::TLogger Logger)
        : TBasePipeline(
            tabletSnapshot,
            columnFilter,
            retentionConfig,
            timestampRange,
            chunkReadOptions,
            profilingUser,
            Logger)
        , Schema_(tabletSnapshot->PhysicalSchema)
        , ColumnFilter_(std::move(columnFilter))
        , ChunkFragmentReader_(tabletSnapshot->ChunkFragmentReader)
        , ChunkReadOptions_(std::move(chunkReadOptions))
    {
        if (const auto& hedgingManagerRegistry = tabletSnapshot->HedgingManagerRegistry) {
            ChunkReadOptions_.HedgingManager = hedgingManagerRegistry->GetOrCreateHedgingManager(
                THedgingUnit{
                    .UserTag = profilingUser,
                    .HunkChunk = true,
                });
        }
    }

    void FinishRow()
    {
        auto mergedRow = TBasePipeline::GetMergedRow();
        RowBuffer_->CaptureValues(mergedRow);
        HunkEncodedRows_.push_back(mergedRow);
    }

    TFuture<std::vector<TSharedRef>> PostprocessTabletLookup(TRefCountedPtr owner)
    {
        auto sharedRows = MakeSharedRange(std::move(HunkEncodedRows_), std::move(RowBuffer_));

        // Being rigorous we should wrap the callback into AsyncVia but that does not matter in practice.
        return DecodeHunks(std::move(sharedRows))
            .Apply(BIND([=, owner = std::move(owner)] (const TSharedRange<TMutableRow>& rows) {
                for (auto row : rows) {
                    TBasePipeline::WriteRow(row);
                }

                return TBasePipeline::PostprocessTabletLookup(owner);
            }));
    }

private:
    using typename TBasePipeline::TMutableRow;

    const TRowBufferPtr RowBuffer_ = New<TRowBuffer>(TLookupSessionBufferTag());
    const TTableSchemaPtr Schema_;
    const TColumnFilter ColumnFilter_;

    NChunkClient::IChunkFragmentReaderPtr ChunkFragmentReader_;
    NChunkClient::TClientChunkReadOptions ChunkReadOptions_;

    std::vector<TMutableRow> HunkEncodedRows_;
    bool HunksDecoded_ = false;


    TFuture<TSharedRange<TMutableUnversionedRow>> DecodeHunks(
        TSharedRange<TMutableUnversionedRow> rows)
    {
        YT_VERIFY(!std::exchange(HunksDecoded_, true));

        return DecodeHunksInSchemafulUnversionedRows(
            Schema_,
            ColumnFilter_,
            std::move(ChunkFragmentReader_),
            std::move(ChunkReadOptions_),
            std::move(rows));
    }

    TFuture<TSharedRange<TMutableVersionedRow>> DecodeHunks(
        TSharedRange<TMutableVersionedRow> rows)
    {
        YT_VERIFY(!std::exchange(HunksDecoded_, true));

        return DecodeHunksInVersionedRows(
            std::move(ChunkFragmentReader_),
            std::move(ChunkReadOptions_),
            std::move(rows));
    }
};

////////////////////////////////////////////////////////////////////////////////

bool GetUseLookupCache(const TTabletSnapshotPtr& tabletSnapshot, std::optional<bool> useLookupCache)
{
    return
        tabletSnapshot->RowCache &&
        useLookupCache.value_or(tabletSnapshot->Settings.MountConfig->EnableLookupCacheByDefault);
}

NTableClient::TColumnFilter DecodeColumnFilter(
    std::unique_ptr<NTableClient::NProto::TColumnFilter> protoColumnFilter,
    int columnCount)
{
    auto columnFilter = protoColumnFilter
        ? TColumnFilter(FromProto<TColumnFilter::TIndexes>(protoColumnFilter->indexes()))
        : TColumnFilter();
    ValidateColumnFilter(columnFilter, columnCount);
    return columnFilter;
}

////////////////////////////////////////////////////////////////////////////////

class TStoreSession
{
public:
    explicit TStoreSession(IVersionedReaderPtr reader)
        : Reader_(std::move(reader))
    { }

    TStoreSession(const TStoreSession& otherSession) = delete;
    TStoreSession(TStoreSession&& otherSession) = default;

    TStoreSession& operator=(const TStoreSession& otherSession) = delete;
    TStoreSession& operator=(TStoreSession&& otherSession)
    {
        YT_VERIFY(!Reader_);
        YT_VERIFY(!otherSession.Reader_);
        return *this;
    }

    TFuture<void> Open() const
    {
        return Reader_->Open();
    }

    TVersionedRow FetchRow()
    {
        YT_ASSERT(IsReaderReady());
        return RowBatch_->MaterializeRows()[RowIndex_++];
    }

    bool PrepareBatch()
    {
        if (IsReaderReady()) {
            return true;
        }

        RowIndex_ = 0;
        RowBatch_ = Reader_->Read(TRowBatchReadOptions{
            .MaxRowsPerRead = RowBufferCapacity
        });

        YT_VERIFY(RowBatch_);

        return !RowBatch_->IsEmpty();
    }

    TFuture<void> GetReadyEvent() const
    {
        return Reader_->GetReadyEvent();
    }

    NChunkClient::NProto::TDataStatistics GetDataStatistics() const
    {
        return Reader_->GetDataStatistics();
    }

    TCodecStatistics GetDecompressionStatistics() const
    {
        return Reader_->GetDecompressionStatistics();
    }

private:
    const IVersionedReaderPtr Reader_;

    IVersionedRowBatchPtr RowBatch_;
    int RowIndex_ = -1;


    bool IsReaderReady() const
    {
        return RowBatch_ && RowIndex_ < RowBatch_->GetRowCount();
    }
};

static constexpr int TypicalStoreSessionCount = 16;
using TStoreSessionList = TCompactVector<TStoreSession, TypicalStoreSessionCount>;

////////////////////////////////////////////////////////////////////////////////

struct TPartitionSession
{
    int CurrentKeyIndex;
    int EndKeyIndex;

    const TPartitionSnapshotPtr PartitionSnapshot;
    const TSharedRange<TLegacyKey> ChunkLookupKeys;

    // TODO(akozhikhov): Proper block fetcher: Create all partition sessions at the begining of the lookup session.
    // Right know we cannot do that because chunk reader may call Open in ctor and start reading blocks.
    bool SessionStarted = false;

    TStoreSessionList StoreSessions;
};

////////////////////////////////////////////////////////////////////////////////

struct TTabletLookupRequest
{
    const TTabletId TabletId;
    const TCellId CellId;
    const NHydra::TRevision MountRevision;
    const TSharedRef RequestData;

    std::vector<TError> InnerErrors;

    TFuture<TSharedRef> RunTabletLookupSession(const TLookupSessionPtr& lookupSession);
};

class TLookupSession
    : public ILookupSession
{
public:
    TLookupSession(
        EInMemoryMode inMemoryMode,
        int tabletRequestCount,
        NCompression::ICodec* responseCodec,
        int maxRetryCount,
        int maxConcurrentSubqueries,
        TReadTimestampRange timestampRange,
        std::optional<bool> useLookupCache,
        NChunkClient::TClientChunkReadOptions chunkReadOptions,
        TRetentionConfigPtr retentionConfig,
        bool enablePartialResult,
        const ITabletSnapshotStorePtr& snapshotStore,
        std::optional<TString> profilingUser,
        IInvokerPtr invoker);

    ~TLookupSession();

    void AddTabletRequest(
        TTabletId tabletId,
        TCellId cellId,
        NHydra::TRevision mountRevision,
        TSharedRef requestData) override;

    TFuture<std::vector<TSharedRef>> Run() override;

private:
    friend struct TTabletLookupRequest;

    template <class TPipeline>
    friend class TTabletLookupSession;

    const EInMemoryMode InMemoryMode_;
    const TReadTimestampRange TimestampRange_;
    NCompression::ICodec* const ResponseCodec_;
    const int MaxRetryCount_;
    const int MaxConcurrentSubqueries_;
    const std::optional<bool> UseLookupCache_;
    const TRetentionConfigPtr RetentionConfig_;
    const bool EnablePartialResult_;
    const ITabletSnapshotStorePtr& SnapshotStore_;
    const std::optional<TString> ProfilingUser_;
    const IInvokerPtr Invoker_;

    const NLogging::TLogger Logger;

    TWallTimer WallTimer_;
    NChunkClient::TClientChunkReadOptions ChunkReadOptions_;
    std::optional<std::pair<TTabletSnapshotPtr, TServiceProfilerGuard>> ProfilerGuard_;

    std::vector<TTabletLookupRequest> TabletRequests_;

    TDeleteListFlusher FlushGuard_;

    std::optional<TDuration> CpuTime_;
    // This flag is used to increment wasted_* profiling counters in case of failed lookup.
    bool FinishedSuccessfully_ = false;

    // NB: These counters are updated within TTabletLookupSession dtor
    // and used for profiling within TLookupSession dtor.
    std::atomic<int> FoundRowCount_ = 0;
    std::atomic<i64> FoundDataWeight_ = 0;
    std::atomic<int> MissingKeyCount_ = 0;
    std::atomic<int> UnmergedRowCount_ = 0;
    std::atomic<i64> UnmergedDataWeight_ = 0;
    std::atomic<TDuration::TValue> DecompressionCpuTime_ = 0;
    std::atomic<int> RetryCount_ = 0;


    TFuture<TSharedRef> RunTabletRequest(
        int requestIndex);
    TFuture<TSharedRef> OnTabletLookupAttemptFinished(
        int requestIndex,
        const TErrorOr<TSharedRef>& resultOrError);
    TFuture<TSharedRef> OnTabletLookupAttemptFailed(
        int requestIndex,
        const TError& error);

    std::vector<TSharedRef> ProcessResults(std::vector<TErrorOr<TSharedRef>>&& resultOrErrors);
};

////////////////////////////////////////////////////////////////////////////////

template <class TPipeline>
class TTabletLookupSession
    : public TRefCounted
    , public TPipeline
{
public:
    TTabletLookupSession(
        TTabletSnapshotPtr tabletSnapshot,
        bool produceAllVersions,
        TColumnFilter columnFilter,
        TSharedRange<TUnversionedRow> lookupKeys,
        TLookupSessionPtr lookupSession);

    ~TTabletLookupSession();

    TFuture<TSharedRef> Run();

    const IInvokerPtr& GetInvoker() const
    {
        return LookupSession_->Invoker_;
    }

private:
    const TLookupSessionPtr LookupSession_;

    const TTabletSnapshotPtr TabletSnapshot_;
    const TTimestamp Timestamp_;
    const bool ProduceAllVersions_;
    const TColumnFilter ColumnFilter_;
    const TSharedRange<TUnversionedRow> LookupKeys_;
    const TSharedRange<TUnversionedRow> ChunkLookupKeys_;

    int ActiveStoreIndex_ = -1;

    const NLogging::TLogger Logger;

    TStoreSessionList DynamicEdenSessions_;
    TStoreSessionList ChunkEdenSessions_;

    int RecursionDepth_ = 0;
    int CurrentPartitionSessionIndex_ = 0;
    std::vector<TPartitionSession> PartitionSessions_;

    using TPipeline::GetFoundRowCount;
    using TPipeline::GetFoundDataWeight;

    int UnmergedRowCount_ = 0;
    i64 UnmergedDataWeight_ = 0;
    TDuration DecompressionCpuTime_;

    TWallTimer Timer_;
    TDuration InitializationDuration_;
    TDuration PartitionsLookupDuration_;


    TPartitionSession CreatePartitionSession(
        decltype(LookupKeys_)::iterator* currentIt,
        int* startChunkKeyIndex);

    TStoreSessionList CreateStoreSessions(
        const std::vector<ISortedStorePtr>& stores,
        const TSharedRange<TLegacyKey>& keys);

    std::vector<TFuture<void>> OpenStoreSessions(const TStoreSessionList& sessions);

    TFuture<TSharedRef> DoRun();
    TFuture<std::vector<TSharedRef>> LookupInPartitions();

    TFuture<void> LookupInCurrentPartition();
    TFuture<void> DoLookupInCurrentPartition();

    void OnStoreSessionsPrepared();
    void LookupFromStoreSessions(TStoreSessionList* sessions, int activeStoreIndex);

    TSharedRef FinishSession(std::vector<TSharedRef>&& rowset);

    void UpdateUnmergedStatistics(const TStoreSessionList& sessions);
};

////////////////////////////////////////////////////////////////////////////////

TLookupSession::TLookupSession(
    EInMemoryMode inMemoryMode,
    int tabletRequestCount,
    NCompression::ICodec* responseCodec,
    int maxRetryCount,
    int maxConcurrentSubqueries,
    TReadTimestampRange timestampRange,
    std::optional<bool> useLookupCache,
    NChunkClient::TClientChunkReadOptions chunkReadOptions,
    TRetentionConfigPtr retentionConfig,
    bool enablePartialResult,
    const ITabletSnapshotStorePtr& snapshotStore,
    std::optional<TString> profilingUser,
    IInvokerPtr invoker)
    : InMemoryMode_(inMemoryMode)
    , TimestampRange_(timestampRange)
    , ResponseCodec_(responseCodec)
    , MaxRetryCount_(maxRetryCount)
    , MaxConcurrentSubqueries_(maxConcurrentSubqueries)
    , UseLookupCache_(useLookupCache)
    , RetentionConfig_(std::move(retentionConfig))
    , EnablePartialResult_(enablePartialResult)
    , SnapshotStore_(snapshotStore)
    , ProfilingUser_(std::move(profilingUser))
    , Invoker_(std::move(invoker))
    , Logger(TabletNodeLogger.WithTag("ReadSessionId: %v", chunkReadOptions.ReadSessionId))
    , ChunkReadOptions_(std::move(chunkReadOptions))
{
    TabletRequests_.reserve(tabletRequestCount);
}

void TLookupSession::AddTabletRequest(
    TTabletId tabletId,
    TCellId cellId,
    NHydra::TRevision mountRevision,
    TSharedRef requestData)
{
    TabletRequests_.push_back(TTabletLookupRequest{
        .TabletId = tabletId,
        .CellId = cellId,
        .MountRevision = mountRevision,
        .RequestData = std::move(requestData),
    });

    if (!ProfilerGuard_) {
        // NB: Any tablet snapshot will suffice.
        if (auto tabletSnapshot = SnapshotStore_->FindTabletSnapshot(tabletId, mountRevision)) {
            const auto& mountConfig = tabletSnapshot->Settings.MountConfig;
            ChunkReadOptions_.MultiplexingParallelism = mountConfig->LookupRpcMultiplexingParallelism;
            ChunkReadOptions_.HunkChunkReaderStatistics = CreateHunkChunkReaderStatistics(
                mountConfig->EnableHunkColumnarProfiling,
                tabletSnapshot->PhysicalSchema);

            if (InMemoryMode_ == EInMemoryMode::None) {
                if (const auto& hedgingManagerRegistry = tabletSnapshot->HedgingManagerRegistry) {
                    ChunkReadOptions_.HedgingManager = hedgingManagerRegistry->GetOrCreateHedgingManager(
                        THedgingUnit{
                            .UserTag = ProfilingUser_,
                            .HunkChunk = false,
                        });
                }
            }

            auto counters = tabletSnapshot->TableProfiler->GetQueryServiceCounters(ProfilingUser_);
            ProfilerGuard_.emplace(std::make_pair(std::move(tabletSnapshot), TServiceProfilerGuard{}));
            ProfilerGuard_->second.Start(counters->Multiread);
        }
    }
}

TFuture<std::vector<TSharedRef>> TLookupSession::Run()
{
    VERIFY_INVOKER_AFFINITY(Invoker_);

    if (TabletRequests_.empty()) {
        return MakeFuture<std::vector<TSharedRef>>({});
    }

    if (InMemoryMode_ == EInMemoryMode::Uncompressed) {
        std::vector<TFuture<TSharedRef>> futures;
        futures.reserve(TabletRequests_.size());

        std::vector<TErrorOr<TSharedRef>> results;
        results.reserve(TabletRequests_.size());

        for (int requestIndex = 0; requestIndex < std::ssize(TabletRequests_); ++requestIndex) {
            futures.push_back(RunTabletRequest(requestIndex));
            if (futures.back().IsSet()) {
                results.push_back(futures.back().Get());
            }
        }

        // TODO(akozhikhov): Proper block fetcher: we may face unset futures here
        // presumably due to some issues with block fetching logic in old columnar readers.
        if (futures.size() != results.size()) {
            return AllSet(std::move(futures)).ApplyUnique(BIND(
                &TLookupSession::ProcessResults,
                MakeStrong(this)));
        }

        return MakeFuture(ProcessResults(std::move(results)));
    }

    std::vector<TCallback<TFuture<TSharedRef>()>> callbacks;
    callbacks.reserve(TabletRequests_.size());

    for (int requestIndex = 0; requestIndex < std::ssize(TabletRequests_); ++requestIndex) {
        callbacks.push_back(BIND(
            &TLookupSession::RunTabletRequest,
            MakeStrong(this),
            requestIndex)
            .AsyncVia(Invoker_));
    }

    return CancelableRunWithBoundedConcurrency(
        std::move(callbacks),
        MaxConcurrentSubqueries_)
        .ApplyUnique(BIND(
            &TLookupSession::ProcessResults,
            MakeStrong(this)));
}

TFuture<TSharedRef> TLookupSession::RunTabletRequest(int requestIndex)
{
    VERIFY_INVOKER_AFFINITY(Invoker_);

    TFuture<TSharedRef> future;
    try {
        future = TabletRequests_[requestIndex].RunTabletLookupSession(this);
    } catch (const std::exception& ex) {
        return OnTabletLookupAttemptFailed(requestIndex, TError(ex));
    }

    if (auto maybeResult = future.TryGet()) {
        return OnTabletLookupAttemptFinished(requestIndex, *maybeResult);
    }
    return future.Apply(BIND(
        &TLookupSession::OnTabletLookupAttemptFinished,
        MakeStrong(this),
        requestIndex));
}

TFuture<TSharedRef> TLookupSession::OnTabletLookupAttemptFinished(
    int requestIndex,
    const TErrorOr<TSharedRef>& resultOrError)
{
    if (resultOrError.IsOK()) {
        return MakeFuture(resultOrError.Value());
    } else {
        return BIND(
            &TLookupSession::OnTabletLookupAttemptFailed,
            MakeStrong(this),
            requestIndex,
            resultOrError)
            .AsyncVia(Invoker_)
            .Run();
    }
}

TFuture<TSharedRef> TLookupSession::OnTabletLookupAttemptFailed(
    int requestIndex,
    const TError& error)
{
    VERIFY_INVOKER_AFFINITY(Invoker_);

    YT_VERIFY(!error.IsOK());

    auto& request = TabletRequests_[requestIndex];

    if (NQueryAgent::IsRetriableQueryError(error)) {
        request.InnerErrors.push_back(error);
        if (std::ssize(request.InnerErrors) < MaxRetryCount_) {
            YT_LOG_INFO(error, "Tablet lookup request failed, retrying "
                "(Iteration: %v, MaxRetryCount: %v, TabletId: %v)",
                std::ssize(request.InnerErrors),
                MaxRetryCount_,
                request.TabletId);

            RetryCount_.fetch_add(1, std::memory_order_relaxed);

            return RunTabletRequest(requestIndex);
        } else {
            if (auto tabletSnapshot = SnapshotStore_->FindLatestTabletSnapshot(request.TabletId)) {
                ++tabletSnapshot->PerformanceCounters->LookupErrorCount;
            }

            return MakeFuture<TSharedRef>(TError("Request failed after %v retries",
                MaxRetryCount_)
                << request.InnerErrors);
        }
    } else {
        YT_LOG_DEBUG(error, "Tablet lookup request failed (TabletId: %v)",
            request.TabletId);

        if (auto tabletSnapshot = SnapshotStore_->FindLatestTabletSnapshot(request.TabletId)) {
            ++tabletSnapshot->PerformanceCounters->LookupErrorCount;
        }

        return MakeFuture<TSharedRef>(error);
    }
}

std::vector<TSharedRef> TLookupSession::ProcessResults(
    std::vector<TErrorOr<TSharedRef>>&& resultOrErrors)
{
    VERIFY_THREAD_AFFINITY_ANY();

    // NB: No trace context is available in dtor so we have to fetch cpu time here.
    if (const auto* traceContext = NTracing::GetCurrentTraceContext()) {
        NTracing::FlushCurrentTraceContextTime();
        CpuTime_ = traceContext->GetElapsedTime();
    }

    std::vector<TSharedRef> results;
    results.reserve(resultOrErrors.size());

    int skippedTabletResultCount = 0;
    for (auto& resultOrError : resultOrErrors) {
        if (!resultOrError.IsOK()) {
            if (EnablePartialResult_) {
                ++skippedTabletResultCount;
                results.emplace_back();
                continue;
            } else {
                YT_LOG_DEBUG(resultOrError, "Lookup session failed");
                resultOrError.ThrowOnError();
            }
        }

        results.push_back(std::move(resultOrError.Value()));
    }

    FinishedSuccessfully_ = true;

    YT_LOG_DEBUG("Lookup session finished successfully "
        "(CpuTime: %v, WallTime: %v, SkippedTabletResultCount: %v)",
        CpuTime_,
        WallTimer_.GetElapsedTime(),
        skippedTabletResultCount);

    return results;
}

TLookupSession::~TLookupSession()
{
    if (!ProfilerGuard_) {
        return;
    }

    const auto& tabletSnapshot = ProfilerGuard_->first;

    auto* counters = tabletSnapshot->TableProfiler->GetLookupCounters(ProfilingUser_);

    counters->RowCount.Increment(FoundRowCount_.load(std::memory_order_relaxed));
    counters->MissingKeyCount.Increment(MissingKeyCount_.load(std::memory_order_relaxed));
    counters->DataWeight.Increment(FoundDataWeight_.load(std::memory_order_relaxed));
    counters->UnmergedRowCount.Increment(UnmergedRowCount_.load(std::memory_order_relaxed));
    counters->UnmergedDataWeight.Increment(UnmergedDataWeight_.load(std::memory_order_relaxed));
    if (!FinishedSuccessfully_) {
        counters->WastedUnmergedDataWeight.Increment(UnmergedDataWeight_.load(std::memory_order_relaxed));
    }

    counters->DecompressionCpuTime.Add(
        TDuration::MicroSeconds(DecompressionCpuTime_.load(std::memory_order_relaxed)));
    if (CpuTime_) {
        counters->CpuTime.Add(*CpuTime_);
    }

    counters->RetryCount.Increment(RetryCount_.load(std::memory_order_relaxed));

    counters->ChunkReaderStatisticsCounters.Increment(
        ChunkReadOptions_.ChunkReaderStatistics,
        !FinishedSuccessfully_);
    counters->HunkChunkReaderCounters.Increment(
        ChunkReadOptions_.HunkChunkReaderStatistics,
        !FinishedSuccessfully_);

    if (FinishedSuccessfully_ && tabletSnapshot->Settings.MountConfig->EnableDetailedProfiling) {
        counters->LookupDuration.Record(WallTimer_.GetElapsedTime());
    }
}

////////////////////////////////////////////////////////////////////////////////

template <class TRowAdapter>
TFuture<TSharedRef> DoRunTabletLookupSession(
    bool useLookupCache,
    TTabletSnapshotPtr tabletSnapshot,
    bool produceAllVersions,
    TColumnFilter columnFilter,
    TSharedRange<TUnversionedRow> lookupKeys,
    TLookupSessionPtr lookupSession)
{
    if (useLookupCache) {
        if (tabletSnapshot->PhysicalSchema->HasHunkColumns()) {
            return New<TTabletLookupSession<THunkDecodingPipeline<TRowCachePipeline<TRowAdapter>>>>(
                std::move(tabletSnapshot),
                /*produceAllVersions*/ true,
                std::move(columnFilter),
                std::move(lookupKeys),
                std::move(lookupSession))->Run();
        } else {
            return New<TTabletLookupSession<TRowCachePipeline<TRowAdapter>>>(
                std::move(tabletSnapshot),
                /*produceAllVersions*/ true,
                std::move(columnFilter),
                std::move(lookupKeys),
                std::move(lookupSession))->Run();
        }
    } else {
        if (tabletSnapshot->PhysicalSchema->HasHunkColumns()) {
            return New<TTabletLookupSession<THunkDecodingPipeline<TSimplePipeline<TRowAdapter>>>>(
                std::move(tabletSnapshot),
                produceAllVersions,
                std::move(columnFilter),
                std::move(lookupKeys),
                std::move(lookupSession))->Run();
        } else {
            return New<TTabletLookupSession<TSimplePipeline<TRowAdapter>>>(
                std::move(tabletSnapshot),
                produceAllVersions,
                std::move(columnFilter),
                std::move(lookupKeys),
                std::move(lookupSession))->Run();
        }
    }
}

TFuture<TSharedRef> TTabletLookupRequest::RunTabletLookupSession(
    const TLookupSessionPtr& lookupSession)
{
    VERIFY_INVOKER_AFFINITY(lookupSession->Invoker_);

    auto tabletSnapshot = lookupSession->SnapshotStore_->GetTabletSnapshotOrThrow(
        TabletId,
        CellId,
        MountRevision);

    auto timestamp = lookupSession->TimestampRange_.Timestamp;

    lookupSession->SnapshotStore_->ValidateTabletAccess(
        tabletSnapshot,
        timestamp);

    ThrowUponDistributedThrottlerOverdraft(
        ETabletDistributedThrottlerKind::Lookup,
        tabletSnapshot,
        lookupSession->ChunkReadOptions_);

    ValidateReadTimestamp(timestamp);
    ValidateTabletRetainedTimestamp(tabletSnapshot, timestamp);

    tabletSnapshot->TabletRuntimeData->AccessTime = NProfiling::GetInstant();

    tabletSnapshot->WaitOnLocks(timestamp);

    auto reader = CreateWireProtocolReader(
        RequestData,
        New<TRowBuffer>(TLookupRowsBufferTag()));

    auto command = reader->ReadCommand();

    std::unique_ptr<NTableClient::NProto::TColumnFilter> columnFilterProto;
    switch (command) {
        case EWireProtocolCommand::LookupRows: {
            NTableClient::NProto::TReqLookupRows req;
            reader->ReadMessage(&req);
            columnFilterProto.reset(req.release_column_filter());
            break;
        }

        case EWireProtocolCommand::VersionedLookupRows: {
            NTableClient::NProto::TReqVersionedLookupRows req;
            reader->ReadMessage(&req);
            columnFilterProto.reset(req.release_column_filter());
            break;
        }

        default:
            THROW_ERROR_EXCEPTION("Unknown read command %v",
                command);
    }

    auto columnFilter = DecodeColumnFilter(
        std::move(columnFilterProto),
        tabletSnapshot->PhysicalSchema->GetColumnCount());
    auto lookupKeys = reader->ReadSchemafulRowset(
        IWireProtocolReader::GetSchemaData(*tabletSnapshot->PhysicalSchema->ToKeys()),
        /*captureValues*/ false);

    const auto& Logger = lookupSession->Logger;
    YT_LOG_DEBUG("Creating tablet lookup session (TabletId: %v, CellId: %v, KeyCount: %v)",
        TabletId,
        CellId,
        lookupKeys.Size());

    bool useLookupCache = GetUseLookupCache(tabletSnapshot, lookupSession->UseLookupCache_);

    switch (command) {
        case EWireProtocolCommand::LookupRows: {
            if (!reader->IsFinished()) {
                THROW_ERROR_EXCEPTION("Lookup command message is malformed");
            }

            return DoRunTabletLookupSession<TUnversionedAdapter>(
                useLookupCache,
                std::move(tabletSnapshot),
                /*produceAllVersions*/ false,
                std::move(columnFilter),
                std::move(lookupKeys),
                lookupSession);
        }

        case EWireProtocolCommand::VersionedLookupRows: {
            if (!reader->IsFinished()) {
                THROW_ERROR_EXCEPTION("Versioned lookup command message is malformed");
            }

            if (lookupSession->TimestampRange_.RetentionTimestamp != NullTimestamp) {
                THROW_ERROR_EXCEPTION("Versioned lookup does not support retention timestamp");
            }

            return DoRunTabletLookupSession<TVersionedAdapter>(
                useLookupCache,
                std::move(tabletSnapshot),
                /*produceAllVersions*/ true,
                std::move(columnFilter),
                std::move(lookupKeys),
                lookupSession);
        }

        default:
            YT_ABORT();
    }
}

////////////////////////////////////////////////////////////////////////////////

template <class TPipeline>
TTabletLookupSession<TPipeline>::TTabletLookupSession(
    TTabletSnapshotPtr tabletSnapshot,
    bool produceAllVersions,
    TColumnFilter columnFilter,
    TSharedRange<TUnversionedRow> lookupKeys,
    TLookupSessionPtr lookupSession)
    : TPipeline(
        tabletSnapshot,
        columnFilter,
        lookupSession->RetentionConfig_,
        lookupSession->TimestampRange_,
        lookupSession->ChunkReadOptions_,
        lookupSession->ProfilingUser_,
        lookupSession->Logger)
    , LookupSession_(std::move(lookupSession))
    , TabletSnapshot_(std::move(tabletSnapshot))
    , Timestamp_(LookupSession_->TimestampRange_.Timestamp)
    , ProduceAllVersions_(produceAllVersions)
    , ColumnFilter_(std::move(columnFilter))
    , LookupKeys_(std::move(lookupKeys))
    , ChunkLookupKeys_(TPipeline::Initialize(LookupKeys_))
    , Logger(LookupSession_->Logger)
{ }

template <class TPipeline>
TFuture<TSharedRef> TTabletLookupSession<TPipeline>::Run()
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    // Synchronously fetch store meta and create store readers.
    // However, may impose a WaitFor call during waiting on locks and during slow path obtaining chunk meta for ext-memory.
    // TODO(akozhikhov): Proper memory management: make this slow path for ext-mem asynchronous.

    std::vector<ISortedStorePtr> dynamicEdenStores;
    std::vector<ISortedStorePtr> chunkEdenStores;

    auto edenStores = TabletSnapshot_->GetEdenStores();
    for (const auto& store : edenStores) {
        if (store->IsDynamic()) {
            // Can not check store state via GetStoreState.
            if (TabletSnapshot_->ActiveStore == store) {
                YT_VERIFY(ActiveStoreIndex_ == -1);
                ActiveStoreIndex_ = dynamicEdenStores.size();
            }

            dynamicEdenStores.push_back(store);
        } else {
            chunkEdenStores.push_back(store);
        }
    }

    DynamicEdenSessions_ = CreateStoreSessions(
        dynamicEdenStores,
        LookupKeys_);

    ChunkEdenSessions_ = CreateStoreSessions(
        chunkEdenStores,
        ChunkLookupKeys_);

    auto currentIt = LookupKeys_.Begin();
    int startChunkKeyIndex = 0;
    while (currentIt != LookupKeys_.End()) {
        PartitionSessions_.push_back(CreatePartitionSession(&currentIt, &startChunkKeyIndex));
    }

    InitializationDuration_ = Timer_.GetElapsedTime();

    // Lookup session is synchronous for in-memory tables.
    // However, for compressed in-memory tables is executed asynchronously due to potential block decompression.
    // TODO(akozhikhov): Proper memory management: make fast path for ext-mem (row cache or uncompressed block cache) synchronous.

    Timer_.Restart();

    std::vector<TFuture<void>> openFutures;
    auto openStoreSessions = [&] (const auto& sessions) {
        auto moreOpenFutures = OpenStoreSessions(sessions);
        openFutures.reserve(openFutures.size() + moreOpenFutures.size());
        std::move(moreOpenFutures.begin(), moreOpenFutures.end(), std::back_inserter(openFutures));
    };

    openStoreSessions(DynamicEdenSessions_);
    openStoreSessions(ChunkEdenSessions_);

    YT_VERIFY(!PartitionSessions_.empty());
    PartitionSessions_[0].SessionStarted = true;
    PartitionSessions_[0].StoreSessions = CreateStoreSessions(
        PartitionSessions_[0].PartitionSnapshot->Stores,
        PartitionSessions_[0].ChunkLookupKeys);
    openStoreSessions(PartitionSessions_[0].StoreSessions);

    if (openFutures.empty()) {
        return DoRun();
    } else {
        return AllSucceeded(std::move(openFutures)).Apply(BIND(
            &TTabletLookupSession::DoRun,
            MakeStrong(this))
            .AsyncVia(GetInvoker()));
    }
}

template <class TPipeline>
TStoreSessionList TTabletLookupSession<TPipeline>::CreateStoreSessions(
    const std::vector<ISortedStorePtr>& stores,
    const TSharedRange<TLegacyKey>& keys)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    TStoreSessionList sessions;
    sessions.reserve(stores.size());

    for (const auto& store : stores) {
        YT_LOG_DEBUG("Creating reader (Store: %v, KeyCount: %v)",
            store->GetId(),
            keys.Size());

        sessions.emplace_back(store->CreateReader(
            TabletSnapshot_,
            keys,
            TPipeline::GetReadTimestamp(),
            ProduceAllVersions_,
            ProduceAllVersions_ ? TColumnFilter::MakeUniversal() : ColumnFilter_,
            LookupSession_->ChunkReadOptions_,
            LookupSession_->ChunkReadOptions_.WorkloadDescriptor.Category));
    }

    return sessions;
}

template <class TPipeline>
std::vector<TFuture<void>> TTabletLookupSession<TPipeline>::OpenStoreSessions(
    const TStoreSessionList& sessions)
{
    // NB: Will remain empty for in-memory tables.
    std::vector<TFuture<void>> futures;
    for (const auto& session : sessions) {
        auto future = session.Open();
        if (auto maybeError = future.TryGet()) {
            maybeError->ThrowOnError();
        } else {
            futures.push_back(std::move(future));
        }
    }
    return futures;
}

template <class TPipeline>
TPartitionSession TTabletLookupSession<TPipeline>::CreatePartitionSession(
    decltype(LookupKeys_)::iterator* currentIt,
    int* startChunkKeyIndex)
{
    auto nextPartitionIt = std::upper_bound(
        TabletSnapshot_->PartitionList.begin(),
        TabletSnapshot_->PartitionList.end(),
        **currentIt,
        [] (TLegacyKey lhs, const TPartitionSnapshotPtr& rhs) {
            return lhs < rhs->PivotKey;
        });
    YT_VERIFY(nextPartitionIt != TabletSnapshot_->PartitionList.begin());
    const auto& partitionSnapshot = *(nextPartitionIt - 1);

    auto nextIt = nextPartitionIt == TabletSnapshot_->PartitionList.end()
        ? LookupKeys_.End()
        : std::lower_bound(*currentIt, LookupKeys_.End(), (*nextPartitionIt)->PivotKey);
    int startKeyIndex = *currentIt - LookupKeys_.Begin();
    int endKeyIndex = nextIt - LookupKeys_.Begin();
    int endChunkKeyIndex = *startChunkKeyIndex;
    for (int index = startKeyIndex; index < endKeyIndex; ++index) {
        endChunkKeyIndex += static_cast<int>(TPipeline::IsLookupInChunkNeeded(index));
    }

    TPartitionSession partitionSession{
        .CurrentKeyIndex = startKeyIndex,
        .EndKeyIndex = endKeyIndex,
        .PartitionSnapshot = partitionSnapshot,
        .ChunkLookupKeys = ChunkLookupKeys_.Slice(*startChunkKeyIndex, endChunkKeyIndex),
    };

    *startChunkKeyIndex = endChunkKeyIndex;
    *currentIt = nextIt;

    return partitionSession;
}

template <class TPipeline>
TFuture<TSharedRef> TTabletLookupSession<TPipeline>::DoRun()
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto future = LookupInPartitions();

    if (auto maybeError = future.TryGetUnique()) {
        if (!maybeError->IsOK()) {
            return MakeFuture<TSharedRef>(TError(*maybeError));
        }
        return MakeFuture(FinishSession(std::move(maybeError->Value())));
    }

    return future.ApplyUnique(BIND(
        &TTabletLookupSession::FinishSession,
        MakeStrong(this))
        .AsyncVia(GetInvoker()));
}

template <class TPipeline>
TFuture<std::vector<TSharedRef>> TTabletLookupSession<TPipeline>::LookupInPartitions()
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    while (CurrentPartitionSessionIndex_ < std::ssize(PartitionSessions_)) {
        auto future = LookupInCurrentPartition();

        if (auto maybeError = future.TryGet()) {
            if (!maybeError->IsOK()) {
                return MakeFuture<std::vector<TSharedRef>>(TError(*maybeError));
            }
        } else {
            return future.Apply(BIND(
                &TTabletLookupSession::LookupInPartitions,
                MakeStrong(this))
                .AsyncVia(GetInvoker()));
        }
    }

    UpdateUnmergedStatistics(DynamicEdenSessions_);
    UpdateUnmergedStatistics(ChunkEdenSessions_);

    PartitionsLookupDuration_ = Timer_.GetElapsedTime();
    Timer_.Restart();

    return TPipeline::PostprocessTabletLookup(this);
}

template <class TPipeline>
TFuture<void> TTabletLookupSession<TPipeline>::LookupInCurrentPartition()
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto& partitionSession = PartitionSessions_[CurrentPartitionSessionIndex_];
    if (!partitionSession.SessionStarted) {
        partitionSession.SessionStarted = true;
        partitionSession.StoreSessions = CreateStoreSessions(
            partitionSession.PartitionSnapshot->Stores,
            partitionSession.ChunkLookupKeys);
        auto openFutures = OpenStoreSessions(partitionSession.StoreSessions);
        if (!openFutures.empty()) {
            return AllSucceeded(std::move(openFutures)).Apply(BIND(
                &TTabletLookupSession::DoLookupInCurrentPartition,
                MakeStrong(this))
                .AsyncVia(GetInvoker()));
        }
    }

    return DoLookupInCurrentPartition();
}

template <class TPipeline>
TFuture<void> TTabletLookupSession<TPipeline>::DoLookupInCurrentPartition()
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto& partitionSession = PartitionSessions_[CurrentPartitionSessionIndex_];

    while (partitionSession.CurrentKeyIndex < partitionSession.EndKeyIndex) {
        // Need to insert rows into cache even from active dynamic store.
        // Otherwise, cache misses will occur.
        // Process dynamic store rows firstly.
        LookupFromStoreSessions(&DynamicEdenSessions_, ActiveStoreIndex_);

        if (TPipeline::IsLookupInChunkNeeded(partitionSession.CurrentKeyIndex++)) {
            std::vector<TFuture<void>> futures;
            auto getUnpreparedSessions = [&] (auto* sessions) {
                for (auto& session : *sessions) {
                    if (!session.PrepareBatch()) {
                        auto future = session.GetReadyEvent();
                        // TODO(akozhikhov): Proper block fetcher: make scenario of empty batch and set future here impossible.
                        if (!future.IsSet() || !future.Get().IsOK()) {
                            // NB: In case of error AllSucceeded below will terminate this session
                            // and cancel its other block fetchers.
                            futures.push_back(std::move(future));
                        }
                    }
                }
            };

            getUnpreparedSessions(&partitionSession.StoreSessions);
            getUnpreparedSessions(&ChunkEdenSessions_);

            if (futures.empty()) {
                OnStoreSessionsPrepared();
            } else {
                // NB: When sessions become prepared we read row in OnStoreSessionsPrepared
                // and move to the next key with call to DoLookupInCurrentPartition.

                static constexpr int RecursionDepthLimit = 100;
                bool breakRecursion = ++RecursionDepth_ > RecursionDepthLimit;
                if (breakRecursion) {
                    RecursionDepth_ = 0;
                }

                auto future = AllSucceeded(std::move(futures)).Apply(BIND([
                    =,
                    this_ = MakeStrong(this)
                ] {
                    OnStoreSessionsPrepared();
                    return DoLookupInCurrentPartition();
                })
                    .AsyncVia(GetInvoker()));

                if (!breakRecursion) {
                    return future;
                }

                // This helps to break chain of recursive promise setters.
                return future.Apply(BIND([this_ = MakeStrong(this)] (const TError& error) {
                    error.ThrowOnError();
                    return;
                })
                    .AsyncVia(GetInvoker()));
            }
        } else {
            TPipeline::FinishRow();
        }
    }

    UpdateUnmergedStatistics(partitionSession.StoreSessions);

    ++CurrentPartitionSessionIndex_;

    return VoidFuture;
}

template <class TPipeline>
void TTabletLookupSession<TPipeline>::OnStoreSessionsPrepared()
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto& partitionSession = PartitionSessions_[CurrentPartitionSessionIndex_];

    LookupFromStoreSessions(&partitionSession.StoreSessions, -1);
    LookupFromStoreSessions(&ChunkEdenSessions_, -1);

    TPipeline::FinishRow();
}

template <class TPipeline>
void TTabletLookupSession<TPipeline>::LookupFromStoreSessions(
    TStoreSessionList* sessions,
    int activeStoreIndex)
{
    for (int sessionIndex = 0; sessionIndex < std::ssize(*sessions); ++sessionIndex) {
        auto& session = (*sessions)[sessionIndex];
        // TODO(akozhikhov): Proper block fetcher: make scenario of empty batch here impossible.
        if (!session.PrepareBatch()) {
            auto readyEvent = session.GetReadyEvent();
            YT_VERIFY(readyEvent.IsSet());
            readyEvent.Get().ThrowOnError();
            YT_VERIFY(session.PrepareBatch());
        }
        auto row = session.FetchRow();
        TPipeline::AddPartialRow(row, Timestamp_ + 1, activeStoreIndex == sessionIndex);
    }
}

template <class TPipeline>
TSharedRef TTabletLookupSession<TPipeline>::FinishSession(std::vector<TSharedRef>&& rowset)
{
    VERIFY_INVOKER_AFFINITY(GetInvoker());

    auto hunksDecodingDuration = Timer_.GetElapsedTime();
    Timer_.Restart();
    auto compressedResult = LookupSession_->ResponseCodec_->Compress(rowset);

    if (const auto& throttler = TabletSnapshot_->DistributedThrottlers[ETabletDistributedThrottlerKind::Lookup]) {
        throttler->Acquire(GetFoundDataWeight());
    }

    YT_LOG_DEBUG(
        "Tablet lookup completed "
        "(TabletId: %v, CellId: %v, EnableDetailedProfiling: %v, "
        "FoundRowCount: %v, FoundDataWeight: %v, DecompressionCpuTime: %v, "
        "InitializationTime: %v, PartitionsLookupTime: %v, HunksDecodingTime: %v, ResponseCompressionTime: %v)",
        TabletSnapshot_->TabletId,
        TabletSnapshot_->CellId,
        TabletSnapshot_->Settings.MountConfig->EnableDetailedProfiling,
        GetFoundRowCount(),
        GetFoundDataWeight(),
        DecompressionCpuTime_,
        InitializationDuration_,
        PartitionsLookupDuration_,
        hunksDecodingDuration,
        Timer_.GetElapsedTime());

    return compressedResult;
}

template <class TPipeline>
void TTabletLookupSession<TPipeline>::UpdateUnmergedStatistics(const TStoreSessionList& sessions)
{
    for (const auto& session : sessions) {
        auto statistics = session.GetDataStatistics();
        UnmergedRowCount_ += statistics.row_count();
        UnmergedDataWeight_ += statistics.data_weight();
        DecompressionCpuTime_ += session.GetDecompressionStatistics().GetTotalDuration();
    }
}

template <class TPipeline>
TTabletLookupSession<TPipeline>::~TTabletLookupSession()
{
    LookupSession_->FoundRowCount_.fetch_add(GetFoundRowCount(), std::memory_order_relaxed);
    LookupSession_->FoundDataWeight_.fetch_add(GetFoundDataWeight(), std::memory_order_relaxed);
    LookupSession_->MissingKeyCount_.fetch_add(LookupKeys_.size() - GetFoundRowCount(), std::memory_order_relaxed);
    LookupSession_->UnmergedRowCount_.fetch_add(UnmergedRowCount_, std::memory_order_relaxed);
    LookupSession_->UnmergedDataWeight_.fetch_add(UnmergedDataWeight_, std::memory_order_relaxed);
    LookupSession_->DecompressionCpuTime_.fetch_add(DecompressionCpuTime_.MicroSeconds(), std::memory_order_relaxed);
}

////////////////////////////////////////////////////////////////////////////////

ILookupSessionPtr CreateLookupSession(
    EInMemoryMode inMemoryMode,
    int tabletRequestCount,
    NCompression::ICodec* responseCodec,
    int maxRetryCount,
    int maxConcurrentSubqueries,
    TReadTimestampRange timestampRange,
    std::optional<bool> useLookupCache,
    NChunkClient::TClientChunkReadOptions chunkReadOptions,
    TRetentionConfigPtr retentionConfig,
    bool enablePartialResult,
    const ITabletSnapshotStorePtr& snapshotStore,
    std::optional<TString> profilingUser,
    IInvokerPtr invoker)
{
    return New<TLookupSession>(
        inMemoryMode,
        tabletRequestCount,
        responseCodec,
        maxRetryCount,
        maxConcurrentSubqueries,
        timestampRange,
        useLookupCache,
        std::move(chunkReadOptions),
        std::move(retentionConfig),
        enablePartialResult,
        snapshotStore,
        std::move(profilingUser),
        std::move(invoker));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
