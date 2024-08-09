#include "tablet_reader.h"

#include "bootstrap.h"
#include "partition.h"
#include "private.h"
#include "store.h"
#include "tablet.h"
#include "tablet_slot.h"
#include "sorted_chunk_store.h"

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_options.h>

#include <yt/yt/ytlib/table_client/hunks.h>
#include <yt/yt/ytlib/table_client/overlapping_reader.h>
#include <yt/yt/ytlib/table_client/row_merger.h>
#include <yt/yt/ytlib/table_client/schemaful_concatencaing_reader.h>
#include <yt/yt/ytlib/table_client/versioned_row_merger.h>
#include <yt/yt/ytlib/table_client/timestamped_schema_helpers.h>

#include <yt/yt/library/query/engine_api/column_evaluator.h>

#include <yt/yt/library/query/base/coordination_helpers.h>

#include <yt/yt/client/table_client/row_batch.h>
#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/unordered_schemaful_reader.h>
#include <yt/yt/client/table_client/unversioned_reader.h>
#include <yt/yt/client/table_client/versioned_reader.h>
#include <yt/yt/client/table_client/versioned_row.h>

#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt/core/misc/heap.h>

#include <yt/yt/core/misc/error.h>
#include <yt/yt/core/misc/range.h>

#include <library/cpp/yt/memory/chunked_memory_pool.h>

namespace NYT::NTabletNode {

using namespace NChunkClient;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NConcurrency;

using NTransactionClient::TReadTimestampRange;

////////////////////////////////////////////////////////////////////////////////

struct TTabletReaderPoolTag { };

static constexpr auto& Logger = TabletNodeLogger;

static constexpr TDuration DefaultMaxOverdraftDuration = TDuration::Minutes(1);

////////////////////////////////////////////////////////////////////////////////

struct TStoreRangeFormatter
{
    void operator()(TStringBuilderBase* builder, const ISortedStorePtr& store) const
    {
        builder->AppendFormat("<%v:%v>",
            store->GetMinKey(),
            store->GetUpperBoundKey());
    }
};

////////////////////////////////////////////////////////////////////////////////

class TUnversifyingReader
    : public ISchemafulUnversionedReader
{
public:
    TUnversifyingReader(
        IVersionedReaderPtr versionedReader,
        NQueryClient::TColumnEvaluatorPtr columnEvaluator,
        const TColumnFilter& columnFilter,
        int columnCount,
        TTimestamp retentionTimestamp,
        const TTimestampColumnMapping& timestampColumnMapping)
        : VersionedReader_(std::move(versionedReader))
        , ColumnEvaluator_(std::move(columnEvaluator))
        , RetentionTimestamp_(retentionTimestamp)
        , RowBuffer_(New<TRowBuffer>())
    {
        ColumnIdToTimestampColumnId_.assign(static_cast<size_t>(columnCount), -1);
        for (auto [columnId, timestampColumnId] : timestampColumnMapping) {
            ColumnIdToTimestampColumnId_[columnId] = timestampColumnId;
        }

        if (columnFilter.IsUniversal()) {
            ColumnIds_.reserve(columnCount);
            for (int id = 0; id < columnCount; ++id) {
                ColumnIds_.push_back(id);
            }
        } else {
            const auto& indexes = columnFilter.GetIndexes();
            ColumnIds_.reserve(indexes.size());
            for (int id : indexes) {
                ColumnIds_.push_back(id);
            }
        }

        ColumnIdToIndex_.assign(static_cast<size_t>(columnCount), -1);
        for (int index = 0; index < std::ssize(ColumnIds_); ++index) {
            ColumnIdToIndex_[ColumnIds_[index]] = index;
        }

        YT_UNUSED_FUTURE(VersionedReader_->Open());
    }

    IUnversionedRowBatchPtr Read(const TRowBatchReadOptions& options = {}) override
    {
        auto batch = VersionedReader_->Read(options);
        if (!batch) {
            return nullptr;
        }

        RowBuffer_->Clear();
        auto rowsRange = batch->MaterializeRows();
        Rows_.reserve(rowsRange.Size());

        for (auto versionedRow : rowsRange) {
            Rows_.push_back(UnversifyRow(versionedRow));
        }

        return CreateBatchFromUnversionedRows(MakeSharedRange(std::move(Rows_), MakeStrong(this)));
    }

    TUnversionedRow UnversifyRow(TVersionedRow versionedRow) const
    {
        auto unversionedRow = RowBuffer_->AllocateUnversioned(ColumnIds_.size());
        for (int index = 0; index < std::ssize(ColumnIds_); ++index) {
            unversionedRow[index] = MakeUnversionedNullValue(ColumnIds_[index]);
        }

        TTimestamp retentionTimestamp = RetentionTimestamp_;
        if (versionedRow.GetDeleteTimestampCount() > 0) {
            auto deleteTimestamp = versionedRow.DeleteTimestamps()[0];
            retentionTimestamp = std::max(deleteTimestamp + 1, retentionTimestamp);
        }

        for (auto key : versionedRow.Keys()) {
            auto index = ColumnIdToIndex_[key.Id];
            if (index >= 0) {
                unversionedRow[index] = key;
            }
        }

        auto begin = versionedRow.BeginValues();
        while (begin != versionedRow.EndValues()) {
            auto columnId = begin->Id;

            auto& column = unversionedRow[ColumnIdToIndex_[columnId]];

            auto end = begin;
            while (
                end != versionedRow.EndValues() &&
                end->Id == columnId &&
                end->Timestamp >= retentionTimestamp)
            {
                end++;
            }

            if (ColumnEvaluator_->IsAggregate(columnId)) {
                TUnversionedValue state;
                ColumnEvaluator_->InitAggregate(columnId, &state, RowBuffer_);
                for (auto it = begin; it < end; ++it) {
                    ColumnEvaluator_->MergeAggregate(columnId, &state, *it, RowBuffer_);
                }
                ColumnEvaluator_->FinalizeAggregate(columnId, &column, state, RowBuffer_);
            } else if (begin != end) {
                column = *begin;
            } else {
                column = MakeUnversionedNullValue(columnId);
            }

            if (auto timestampColumnId = ColumnIdToTimestampColumnId_[columnId];
                timestampColumnId != -1 && begin != end)
            {
                unversionedRow[ColumnIdToIndex_[timestampColumnId]] = MakeUnversionedUint64Value(begin->Timestamp);
            }

            while (begin != versionedRow.EndValues() && begin->Id == columnId) {
                begin++;
            }
        }

        return unversionedRow;
    }

    NChunkClient::NProto::TDataStatistics GetDataStatistics() const override
    {
        return VersionedReader_->GetDataStatistics();
    }

    TCodecStatistics GetDecompressionStatistics() const override
    {
        return VersionedReader_->GetDecompressionStatistics();
    }

    bool IsFetchingCompleted() const override
    {
        return VersionedReader_->IsFetchingCompleted();
    }

    std::vector<TChunkId> GetFailedChunkIds() const override
    {
        return VersionedReader_->GetFailedChunkIds();
    }

    TFuture<void> GetReadyEvent() const override
    {
        return VersionedReader_->GetReadyEvent();
    }

private:
    const IVersionedReaderPtr VersionedReader_;
    const NQueryClient::TColumnEvaluatorPtr ColumnEvaluator_;
    const TTimestamp RetentionTimestamp_;
    const TRowBufferPtr RowBuffer_;
    TCompactVector<int, TypicalColumnCount> ColumnIds_;
    TCompactVector<int, TypicalColumnCount> ColumnIdToIndex_;
    TCompactVector<int, TypicalColumnCount> ColumnIdToTimestampColumnId_;
    std::vector<TUnversionedRow> Rows_;
};

////////////////////////////////////////////////////////////////////////////////

void ThrowUponDistributedThrottlerOverdraft(
    ETabletDistributedThrottlerKind tabletThrottlerKind,
    const TTabletSnapshotPtr& tabletSnapshot,
    const TClientChunkReadOptions& chunkReadOptions)
{
    const auto& distributedThrottler = tabletSnapshot->DistributedThrottlers[tabletThrottlerKind];
    if (distributedThrottler && distributedThrottler->IsOverdraft()) {
        tabletSnapshot->TableProfiler->GetThrottlerCounter(tabletThrottlerKind)->Increment();
        THROW_ERROR_EXCEPTION(NTabletClient::EErrorCode::RequestThrottled,
            "Read request is throttled due to %Qlv throttler overdraft",
            tabletThrottlerKind)
            << TErrorAttribute("tablet_id", tabletSnapshot->TabletId)
            << TErrorAttribute("read_session_id", chunkReadOptions.ReadSessionId)
            << TErrorAttribute("queue_total_count", distributedThrottler->GetQueueTotalAmount());
    }
}

void ThrowUponNodeThrottlerOverdraft(
    std::optional<TInstant> requestStartTime,
    std::optional<TDuration> requestTimeout,
    const TClientChunkReadOptions& chunkReadOptions,
    IBootstrap* bootstrap)
{
    TDuration maxOverdraftDuration = DefaultMaxOverdraftDuration;
    if (requestStartTime && requestTimeout) {
        maxOverdraftDuration = *requestStartTime + *requestTimeout - NProfiling::GetInstant();
    }

    const auto& nodeThrottler = bootstrap->GetInThrottler(chunkReadOptions.WorkloadDescriptor.Category);
    if (nodeThrottler->GetEstimatedOverdraftDuration() > maxOverdraftDuration) {
        THROW_ERROR_EXCEPTION(NTabletClient::EErrorCode::RequestThrottled,
            "Read request is throttled due to node throttler overdraft")
            << TErrorAttribute("read_session_id", chunkReadOptions.ReadSessionId)
            << TErrorAttribute("queue_total_count", nodeThrottler->GetQueueTotalAmount())
            << TErrorAttribute("max_overdraft_duration", maxOverdraftDuration);
    }
}

////////////////////////////////////////////////////////////////////////////////

namespace {

template <class IReader, class TRow>
class TThrottlerAwareReaderBase
    : public IReader
{
public:
    using IReaderPtr = TIntrusivePtr<IReader>;

    TThrottlerAwareReaderBase(
        IReaderPtr underlying,
        IThroughputThrottlerPtr throttler)
        : Underlying_(std::move(underlying))
        , Throttler_(std::move(throttler))
    { }

    typename TRowBatchTrait<TRow>::IRowBatchPtr Read(const TRowBatchReadOptions& options = {}) override
    {
        auto rawBatch = Underlying_->Read(options);

        auto currentDataWeight = Underlying_->GetDataStatistics().data_weight();
        YT_VERIFY(currentDataWeight >= ThrottledDataWeight_);
        Throttler_->Acquire(currentDataWeight - ThrottledDataWeight_);
        ThrottledDataWeight_ = currentDataWeight;

        return rawBatch;
    }

    NChunkClient::NProto::TDataStatistics GetDataStatistics() const override
    {
        return Underlying_->GetDataStatistics();
    }

    TCodecStatistics GetDecompressionStatistics() const override
    {
        return Underlying_->GetDecompressionStatistics();
    }

    bool IsFetchingCompleted() const override
    {
        return Underlying_->IsFetchingCompleted();
    }

    std::vector<TChunkId> GetFailedChunkIds() const override
    {
        return Underlying_->GetFailedChunkIds();
    }

    TFuture<void> GetReadyEvent() const override
    {
        return Underlying_->GetReadyEvent();
    }

protected:
    const IReaderPtr Underlying_;
    const IThroughputThrottlerPtr Throttler_;

    i64 ThrottledDataWeight_ = 0;
};

class TThrottlerAwareVersionedReader
    : public TThrottlerAwareReaderBase<IVersionedReader, TVersionedRow>
{
public:
    TThrottlerAwareVersionedReader(
        IVersionedReaderPtr underlying,
        IThroughputThrottlerPtr throttler)
        : TThrottlerAwareReaderBase(std::move(underlying), std::move(throttler))
    { }

    TFuture<void> Open() override
    {
        return Underlying_->Open();
    }
};

class TThrottlerAwareSchemafulUnversionedReader
    : public TThrottlerAwareReaderBase<ISchemafulUnversionedReader, TUnversionedRow>
{
public:
    TThrottlerAwareSchemafulUnversionedReader(
        ISchemafulUnversionedReaderPtr underlying,
        IThroughputThrottlerPtr throttler)
        : TThrottlerAwareReaderBase(std::move(underlying), std::move(throttler))
    { }
};

template <class TThrottlerAwareReader, class IReaderPtr>
IReaderPtr MaybeWrapWithThrottlerAwareReader(
    std::optional<ETabletDistributedThrottlerKind> tabletThrottlerKind,
    const TTabletSnapshotPtr& tabletSnapshot,
    IReaderPtr underlyingReader)
{
    const auto& throttler = tabletThrottlerKind
        ? tabletSnapshot->DistributedThrottlers[*tabletThrottlerKind]
        : nullptr;

    if (throttler) {
        return New<TThrottlerAwareReader>(std::move(underlyingReader), throttler);
    } else {
        return underlyingReader;
    }
}

ISchemafulUnversionedReaderPtr WrapSchemafulTabletReader(
    std::optional<ETabletDistributedThrottlerKind> tabletThrottlerKind,
    const TTabletSnapshotPtr& tabletSnapshot,
    const TClientChunkReadOptions& chunkReadOptions,
    const TColumnFilter& columnFilter,
    ISchemafulUnversionedReaderPtr reader)
{
    reader = MaybeWrapWithThrottlerAwareReader<TThrottlerAwareSchemafulUnversionedReader>(
        tabletThrottlerKind,
        tabletSnapshot,
        std::move(reader));

    reader = CreateHunkDecodingSchemafulReader(
        tabletSnapshot->QuerySchema,
        columnFilter,
        tabletSnapshot->Settings.HunkReaderConfig,
        std::move(reader),
        tabletSnapshot->ChunkFragmentReader,
        tabletSnapshot->DictionaryCompressionFactory,
        chunkReadOptions);

    return reader;
}

std::unique_ptr<TSchemafulRowMerger> CreateLatestTimestampRowMerger(
    TRowBufferPtr rowBuffer,
    const TTabletSnapshotPtr& tabletSnapshot,
    const TColumnFilter& columnFilter,
    TTimestamp retentionTimestamp,
    const TTimestampReadOptions& timestampReadOptions)
{
    auto createRowMerger = [&] (int columnCount, const TColumnFilter& rowMergerColumnFilter) {
        return std::make_unique<TSchemafulRowMerger>(
            std::move(rowBuffer),
            columnCount,
            tabletSnapshot->QuerySchema->GetKeyColumnCount(),
            rowMergerColumnFilter,
            tabletSnapshot->ColumnEvaluator,
            retentionTimestamp,
            timestampReadOptions.TimestampColumnMapping);
    };

    if (timestampReadOptions.TimestampColumnMapping.empty()) {
        return createRowMerger(tabletSnapshot->QuerySchema->GetColumnCount(), columnFilter);
    } else {
        return createRowMerger(
            // Add timestamp column for every value column.
            tabletSnapshot->QuerySchema->GetColumnCount() + tabletSnapshot->QuerySchema->GetValueColumnCount(),
            CreateLatestTimestampColumnFilter(columnFilter, tabletSnapshot->QuerySchema, timestampReadOptions));
    }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

ISchemafulUnversionedReaderPtr CreatePartitionScanReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    const TColumnFilter& columnFilter,
    const TSharedRange<TPartitionBounds>& partitionBounds,
    TReadTimestampRange timestampRange,
    const TClientChunkReadOptions& chunkReadOptions,
    std::optional<ETabletDistributedThrottlerKind> tabletThrottlerKind,
    std::optional<EWorkloadCategory> workloadCategory,
    TTimestampReadOptions timestampReadOptions,
    bool mergeVersionedRows)
{
    auto timestamp = timestampRange.Timestamp;
    ValidateTabletRetainedTimestamp(tabletSnapshot, timestamp);

    tabletSnapshot->WaitOnLocks(timestamp);

    if (tabletThrottlerKind) {
        ThrowUponDistributedThrottlerOverdraft(*tabletThrottlerKind, tabletSnapshot, chunkReadOptions);
    }

    auto holder = partitionBounds.GetHolder();

    std::vector<ISortedStorePtr> stores;
    std::vector<TSharedRange<TRowRange>> boundsPerStore;

    std::vector<TRowRange> edenStoreBoundsVector;
    for (const auto& [bounds, partitionIndex] : partitionBounds) {
        const auto& partition = tabletSnapshot->PartitionList[partitionIndex];

        YT_VERIFY(bounds.size() > 0);

        auto lowerBound = std::max<TLegacyKey>(bounds.front().first, partition->PivotKey);
        auto upperBound = std::min<TLegacyKey>(bounds.back().second, partition->NextPivotKey);

        // Enrich bounds for eden stores with partition bounds.
        NQueryClient::ForEachRange(TRange(bounds), TRowRange(lowerBound, upperBound), [&] (auto item) {
            auto [lower, upper] = item;
            edenStoreBoundsVector.emplace_back(lower, upper);
        });

        auto partitionStoreBounds = MakeSharedRange(bounds, holder);

        for (const auto& store : partition->Stores) {
            stores.push_back(store);
            boundsPerStore.push_back(partitionStoreBounds);
        }
    }

    auto edenStoreBounds = MakeSharedRange(edenStoreBoundsVector, holder);

    for (const auto& store : tabletSnapshot->GetEdenStores()) {
        stores.push_back(store);
        boundsPerStore.push_back(edenStoreBounds);
    }

    if (std::ssize(stores) > tabletSnapshot->Settings.MountConfig->MaxReadFanIn) {
        THROW_ERROR_EXCEPTION("Read fan-in limit exceeded; please wait until your data is merged")
            << TErrorAttribute("tablet_id", tabletSnapshot->TabletId)
            << TErrorAttribute("fan_in", std::ssize(stores))
            << TErrorAttribute("fan_in_limit", tabletSnapshot->Settings.MountConfig->MaxReadFanIn);
    }

    TUnversionedRow lowerBound;
    TUnversionedRow upperBound;

    if (!partitionBounds.Empty()) {
        lowerBound = partitionBounds.Front().Bounds.front().first;
        upperBound = partitionBounds.Back().Bounds.back().second;
    }

    YT_LOG_DEBUG("Creating schemaful sorted tablet reader (TabletId: %v, CellId: %v, "
        "WorkloadDescriptor: %v, ReadSessionId: %v, StoreIds: %v, StoreRanges: %v, "
        "Timestamp: %v, BoundCount: %v, LowerBound: %kv, UpperBound: %kv, MergeVersionedRows: %v)",
        tabletSnapshot->TabletId,
        tabletSnapshot->CellId,
        chunkReadOptions.WorkloadDescriptor,
        chunkReadOptions.ReadSessionId,
        MakeFormattableView(stores, TStoreIdFormatter()),
        MakeFormattableView(stores, TStoreRangeFormatter()),
        timestamp,
        std::ssize(partitionBounds),
        lowerBound,
        upperBound,
        mergeVersionedRows);

    ISchemafulUnversionedReaderPtr reader;

    if (mergeVersionedRows) {
        std::vector<TLegacyOwningKey> startStoreBounds;
        startStoreBounds.reserve(std::ssize(stores));
        for (const auto& store : stores) {
            startStoreBounds.push_back(store->GetMinKey());
        }

        TColumnFilter enrichedColumnFilter;
        if (!columnFilter.IsUniversal()) {
            auto indexes = columnFilter.GetIndexes();
            auto keyColumnCount = tabletSnapshot->QuerySchema->GetKeyColumnCount();

            for (int index = 0; index < keyColumnCount; ++index) {
                indexes.push_back(index);
            }

            std::sort(indexes.begin(), indexes.end());
            indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());

            enrichedColumnFilter = TColumnFilter(std::move(indexes));
        }

        auto rowMerger = CreateLatestTimestampRowMerger(
            New<TRowBuffer>(TTabletReaderPoolTag()),
            tabletSnapshot,
            columnFilter,
            timestampRange.RetentionTimestamp,
            timestampReadOptions);

        reader = CreateSchemafulOverlappingRangeReader(
            std::move(startStoreBounds),
            std::move(rowMerger),
            [
                =,
                stores = std::move(stores),
                boundsPerStore = std::move(boundsPerStore)
            ] (int index) {
                YT_VERIFY(index < std::ssize(stores));

                return stores[index]->CreateReader(
                    tabletSnapshot,
                    boundsPerStore[index],
                    timestamp,
                    false,
                    enrichedColumnFilter,
                    chunkReadOptions,
                    workloadCategory);
            },
            [keyComparer = tabletSnapshot->RowKeyComparer] (TUnversionedValueRange lhs, TUnversionedValueRange rhs) {
                return keyComparer(lhs, rhs);
            });
    } else {
        auto storesCount = stores.size();
        auto getNextReader = [
            =,
            timestampReadOptions = std::move(timestampReadOptions),
            stores = std::move(stores),
            boundsPerStore = std::move(boundsPerStore),
            index = 0
        ] () mutable -> ISchemafulUnversionedReaderPtr {
            if (index == std::ssize(stores)) {
                return nullptr;
            }

            auto underlyingReader = stores[index]->CreateReader(
                tabletSnapshot,
                boundsPerStore[index],
                timestamp,
                /*produceAllVersions*/ false,
                columnFilter,
                chunkReadOptions,
                workloadCategory);
            index++;

            auto createReader = [&] (int columnCount, const TColumnFilter& readerColumnFilter) {
                return New<TUnversifyingReader>(
                    std::move(underlyingReader),
                    tabletSnapshot->ColumnEvaluator,
                    readerColumnFilter,
                    columnCount,
                    timestampRange.RetentionTimestamp,
                    timestampReadOptions.TimestampColumnMapping);
            };

            if (timestampReadOptions.TimestampColumnMapping.empty()) {
                return createReader(tabletSnapshot->QuerySchema->GetColumnCount(), columnFilter);
            } else {
                return createReader(
                    // Add timestamp column for every value column.
                    tabletSnapshot->QuerySchema->GetColumnCount() + tabletSnapshot->QuerySchema->GetValueColumnCount(),
                    CreateLatestTimestampColumnFilter(columnFilter, tabletSnapshot->QuerySchema, timestampReadOptions));
            }

            return reader;
        };

        reader = CreateUnorderedSchemafulReader(getNextReader, storesCount);
    }

    return WrapSchemafulTabletReader(
        tabletThrottlerKind,
        tabletSnapshot,
        chunkReadOptions,
        columnFilter,
        std::move(reader));
}

////////////////////////////////////////////////////////////////////////////////

ISchemafulUnversionedReaderPtr CreateSchemafulSortedTabletReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    const TColumnFilter& columnFilter,
    const TSharedRange<TRowRange>& bounds,
    TReadTimestampRange timestampRange,
    const TClientChunkReadOptions& chunkReadOptions,
    std::optional<ETabletDistributedThrottlerKind> tabletThrottlerKind,
    std::optional<EWorkloadCategory> workloadCategory,
    TTimestampReadOptions timestampReadOptions,
    bool mergeVersionedRows)
{
    auto timestamp = timestampRange.Timestamp;
    ValidateTabletRetainedTimestamp(tabletSnapshot, timestamp);

    YT_VERIFY(bounds.Size() > 0);
    auto lowerBound = bounds[0].first;
    auto upperBound = bounds[bounds.Size() - 1].second;

    std::vector<ISortedStorePtr> stores;
    std::vector<TSharedRange<TRowRange>> boundsPerStore;

    tabletSnapshot->WaitOnLocks(timestamp);

    if (tabletThrottlerKind) {
        ThrowUponDistributedThrottlerOverdraft(*tabletThrottlerKind, tabletSnapshot, chunkReadOptions);
    }

    // Pick stores which intersect [lowerBound, upperBound) (excluding upperBound).
    auto takePartition = [&] (const std::vector<ISortedStorePtr>& candidateStores) {
        for (const auto& store : candidateStores) {
            auto begin = std::upper_bound(
                bounds.begin(),
                bounds.end(),
                store->GetMinKey().Get(),
                [] (TUnversionedRow lhs, const TRowRange& rhs) {
                    return lhs < rhs.second;
                });

            auto end = std::lower_bound(
                bounds.begin(),
                bounds.end(),
                store->GetUpperBoundKey().Get(),
                [] (const TRowRange& lhs, TUnversionedRow rhs) {
                    return lhs.first < rhs;
                });

            if (begin != end) {
                auto offsetBegin = std::distance(bounds.begin(), begin);
                auto offsetEnd = std::distance(bounds.begin(), end);

                stores.push_back(store);
                boundsPerStore.push_back(bounds.Slice(offsetBegin, offsetEnd));
            }
        }
    };

    takePartition(tabletSnapshot->GetEdenStores());

    auto range = tabletSnapshot->GetIntersectingPartitions(lowerBound, upperBound);
    for (auto it = range.first; it != range.second; ++it) {
        takePartition((*it)->Stores);
    }

    if (std::ssize(stores) > tabletSnapshot->Settings.MountConfig->MaxReadFanIn) {
        THROW_ERROR_EXCEPTION("Read fan-in limit exceeded; please wait until your data is merged")
            << TErrorAttribute("tablet_id", tabletSnapshot->TabletId)
            << TErrorAttribute("fan_in", stores.size())
            << TErrorAttribute("fan_in_limit", tabletSnapshot->Settings.MountConfig->MaxReadFanIn);
    }

    YT_LOG_DEBUG("Creating schemaful sorted tablet reader (TabletId: %v, CellId: %v, Timestamp: %v, "
        "LowerBound: %v, UpperBound: %v, WorkloadDescriptor: %v, ReadSessionId: %v, StoreIds: %v, "
        "StoreRanges: %v, BoundCount: %v, MergeVersionedRows: %v)",
        tabletSnapshot->TabletId,
        tabletSnapshot->CellId,
        timestamp,
        lowerBound,
        upperBound,
        chunkReadOptions.WorkloadDescriptor,
        chunkReadOptions.ReadSessionId,
        MakeFormattableView(stores, TStoreIdFormatter()),
        MakeFormattableView(stores, TStoreRangeFormatter()),
        bounds.Size(),
        mergeVersionedRows);

    std::vector<TLegacyOwningKey> boundaries;
    boundaries.reserve(stores.size());
    for (const auto& store : stores) {
        boundaries.push_back(store->GetMinKey());

        if (Y_UNLIKELY(!mergeVersionedRows)) {
            auto type = store->GetType();

            THROW_ERROR_EXCEPTION_IF(type != EStoreType::SortedDynamic && type != EStoreType::SortedChunk,
                "Expected a sorted table when not merging versioned rows");

            if (type == EStoreType::SortedChunk) {
                auto format = CheckedEnumCast<EChunkFormat>(store->AsSortedChunk()->GetChunkMeta().format());
                THROW_ERROR_EXCEPTION_IF(format != EChunkFormat::TableVersionedColumnar,
                    "Expected chunks with %Qlv format when not merging versioned rows",
                    EChunkFormat::TableVersionedColumnar);
            }
        }
    }

    ISchemafulUnversionedReaderPtr reader;

    if (mergeVersionedRows) {
        TColumnFilter enrichedColumnFilter;
        if (!columnFilter.IsUniversal()) {
            auto indexes = columnFilter.GetIndexes();
            auto keyColumnCount = tabletSnapshot->QuerySchema->GetKeyColumnCount();

            for (int index = 0; index < keyColumnCount; ++index) {
                indexes.push_back(index);
            }

            std::sort(indexes.begin(), indexes.end());
            indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());

            enrichedColumnFilter = TColumnFilter(std::move(indexes));
        }

        auto rowMerger = CreateLatestTimestampRowMerger(
            New<TRowBuffer>(TTabletReaderPoolTag()),
            tabletSnapshot,
            columnFilter,
            timestampRange.RetentionTimestamp,
            timestampReadOptions);

        reader = CreateSchemafulOverlappingRangeReader(
            std::move(boundaries),
            std::move(rowMerger),
            [
                =,
                stores = std::move(stores),
                boundsPerStore = std::move(boundsPerStore)
            ] (int index) {
                YT_ASSERT(index < std::ssize(stores));

                return stores[index]->CreateReader(
                    tabletSnapshot,
                    boundsPerStore[index],
                    timestamp,
                    false,
                    enrichedColumnFilter,
                    chunkReadOptions,
                    workloadCategory);
            },
            [keyComparer = tabletSnapshot->RowKeyComparer] (TUnversionedValueRange lhs, TUnversionedValueRange rhs) {
                return keyComparer(lhs, rhs);
            });
    } else {
        auto getNextReader = [
            =,
            timestampReadOptions = std::move(timestampReadOptions),
            stores = std::move(stores),
            boundsPerStore = std::move(boundsPerStore),
            index = 0
        ] () mutable -> ISchemafulUnversionedReaderPtr {
            if (index == std::ssize(stores)) {
                return nullptr;
            }

            auto underlyingReader = stores[index]->CreateReader(
                tabletSnapshot,
                boundsPerStore[index],
                timestamp,
                /*produceAllVersions*/ false,
                columnFilter,
                chunkReadOptions,
                workloadCategory);
            index++;

            auto createReader = [&] (int columnCount, const TColumnFilter& readerColumnFilter) {
                return New<TUnversifyingReader>(
                    std::move(underlyingReader),
                    tabletSnapshot->ColumnEvaluator,
                    readerColumnFilter,
                    columnCount,
                    timestampRange.RetentionTimestamp,
                    timestampReadOptions.TimestampColumnMapping);
            };

            if (timestampReadOptions.TimestampColumnMapping.empty()) {
                return createReader(tabletSnapshot->QuerySchema->GetColumnCount(), columnFilter);
            } else {
                return createReader(
                    // Add timestamp column for every value column.
                    tabletSnapshot->QuerySchema->GetColumnCount() + tabletSnapshot->QuerySchema->GetValueColumnCount(),
                    CreateLatestTimestampColumnFilter(columnFilter, tabletSnapshot->QuerySchema, timestampReadOptions));
            }
        };

        reader = CreateUnorderedSchemafulReader(getNextReader, boundaries.size());
    }

    return WrapSchemafulTabletReader(
        tabletThrottlerKind,
        tabletSnapshot,
        chunkReadOptions,
        columnFilter,
        std::move(reader));
}

ISchemafulUnversionedReaderPtr CreateSchemafulOrderedTabletReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    const TColumnFilter& columnFilter,
    TLegacyOwningKey lowerBound,
    TLegacyOwningKey upperBound,
    TReadTimestampRange timestampRange,
    const TClientChunkReadOptions& chunkReadOptions,
    std::optional<ETabletDistributedThrottlerKind> tabletThrottlerKind,
    std::optional<EWorkloadCategory> workloadCategory)
{
    // Deduce tablet index and row range from lower and upper bound.
    YT_VERIFY(lowerBound.GetCount() >= 1);
    YT_VERIFY(upperBound.GetCount() >= 1);

    if (tabletThrottlerKind) {
        ThrowUponDistributedThrottlerOverdraft(*tabletThrottlerKind, tabletSnapshot, chunkReadOptions);
    }

    constexpr i64 infinity = std::numeric_limits<i64>::max() / 2;

    auto valueToInt = [] (const TUnversionedValue& value) {
        switch (value.Type) {
            case EValueType::Int64:
                return std::clamp(value.Data.Int64, -infinity, +infinity);
            case EValueType::Min:
                return -infinity;
            case EValueType::Max:
                return +infinity;
            default:
                YT_ABORT();
        }
    };

    int tabletIndex = 0;
    i64 lowerRowIndex = 0;
    i64 upperRowIndex = infinity;
    if (lowerBound < upperBound) {
        if (lowerBound[0].Type == EValueType::Min) {
            tabletIndex = 0;
        } else {
            YT_VERIFY(lowerBound[0].Type == EValueType::Int64);
            tabletIndex = static_cast<int>(lowerBound[0].Data.Int64);
        }

        YT_VERIFY(upperBound[0].Type == EValueType::Int64 ||
            upperBound[0].Type == EValueType::Max);
        YT_VERIFY(upperBound[0].Type != EValueType::Int64 ||
            tabletIndex == upperBound[0].Data.Int64 ||
            tabletIndex + 1 == upperBound[0].Data.Int64);

        if (lowerBound.GetCount() >= 2) {
            lowerRowIndex = valueToInt(lowerBound[1]);
            if (lowerBound.GetCount() >= 3) {
                ++lowerRowIndex;
            }
        }

        if (upperBound.GetCount() >= 2) {
            upperRowIndex = valueToInt(upperBound[1]);
            if (upperBound.GetCount() >= 3) {
                ++upperRowIndex;
            }
        }
    }

    i64 trimmedRowCount = tabletSnapshot->TabletRuntimeData->TrimmedRowCount;
    if (lowerRowIndex < trimmedRowCount) {
        lowerRowIndex = trimmedRowCount;
    }

    std::vector<int> storeIndices;
    const auto& allStores = tabletSnapshot->OrderedStores;
    if (lowerRowIndex < upperRowIndex && !allStores.empty()) {
        auto lowerIt = std::upper_bound(
            allStores.begin(),
            allStores.end(),
            lowerRowIndex,
            [] (i64 lhs, const IOrderedStorePtr& rhs) {
                return lhs < rhs->GetStartingRowIndex();
            }) - 1;
        auto it = lowerIt;
        while (it != allStores.end()) {
            const auto& store = *it;
            if (store->GetStartingRowIndex() >= upperRowIndex) {
                break;
            }
            storeIndices.push_back(std::distance(allStores.begin(), it));
            ++it;
        }
    }

    YT_LOG_DEBUG("Creating schemaful ordered tablet reader (TabletId: %v, CellId: %v, "
        "LowerRowIndex: %v, UpperRowIndex: %v, WorkloadDescriptor: %v, ReadSessionId: %v, StoreIds: %v)",
        tabletSnapshot->TabletId,
        tabletSnapshot->CellId,
        lowerRowIndex,
        upperRowIndex,
        chunkReadOptions.WorkloadDescriptor,
        chunkReadOptions.ReadSessionId,
        MakeFormattableView(storeIndices, [&] (auto* builder, int storeIndex) {
            FormatValue(builder, allStores[storeIndex]->GetId(), TStringBuf());
        }));

    std::vector<std::function<ISchemafulUnversionedReaderPtr()>> readers;
    for (auto storeIndex : storeIndices) {
        auto store = allStores[storeIndex];
        readers.push_back([=, store = std::move(store)] {
            return store->CreateReader(
                tabletSnapshot,
                tabletIndex,
                lowerRowIndex,
                upperRowIndex,
                timestampRange.Timestamp,
                columnFilter,
                chunkReadOptions,
                workloadCategory);
        });
    }

    auto reader = CreateSchemafulConcatenatingReader(std::move(readers));

    return WrapSchemafulTabletReader(
        tabletThrottlerKind,
        tabletSnapshot,
        chunkReadOptions,
        columnFilter,
        std::move(reader));
}

ISchemafulUnversionedReaderPtr CreateSchemafulRangeTabletReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    const TColumnFilter& columnFilter,
    TLegacyOwningKey lowerBound,
    TLegacyOwningKey upperBound,
    TReadTimestampRange timestampRange,
    const TClientChunkReadOptions& chunkReadOptions,
    std::optional<ETabletDistributedThrottlerKind> tabletThrottlerKind,
    std::optional<EWorkloadCategory> workloadCategory)
{
    if (tabletSnapshot->PhysicalSchema->IsSorted()) {
        return CreateSchemafulSortedTabletReader(
            tabletSnapshot,
            columnFilter,
            MakeSingletonRowRange(lowerBound, upperBound),
            timestampRange,
            chunkReadOptions,
            tabletThrottlerKind,
            workloadCategory,
            /*timestampReadOptions*/ {});
    } else {
        return CreateSchemafulOrderedTabletReader(
            tabletSnapshot,
            columnFilter,
            std::move(lowerBound),
            std::move(upperBound),
            timestampRange,
            chunkReadOptions,
            tabletThrottlerKind,
            workloadCategory);
    }
}

////////////////////////////////////////////////////////////////////////////////

ISchemafulUnversionedReaderPtr CreatePartitionLookupReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    int partitionIndex,
    const TColumnFilter& columnFilter,
    const TSharedRange<TLegacyKey>& keys,
    TReadTimestampRange timestampRange,
    const TClientChunkReadOptions& chunkReadOptions,
    std::optional<ETabletDistributedThrottlerKind> tabletThrottlerKind,
    std::optional<EWorkloadCategory> workloadCategory,
    const TTimestampReadOptions& timestampReadOptions)
{
    auto timestamp = timestampRange.Timestamp;
    ValidateTabletRetainedTimestamp(tabletSnapshot, timestamp);

    tabletSnapshot->WaitOnLocks(timestamp);

    if (tabletThrottlerKind) {
        ThrowUponDistributedThrottlerOverdraft(*tabletThrottlerKind, tabletSnapshot, chunkReadOptions);
    }

    const auto& partition = tabletSnapshot->PartitionList[partitionIndex];

    auto minKey = *keys.Begin();
    auto maxKey = *(keys.End() - 1);
    std::vector<ISortedStorePtr> stores;

    // Pick stores which intersect [minKey, maxKey] (including maxKey).
    auto takeStores = [&] (const std::vector<ISortedStorePtr>& candidateStores) {
        for (const auto& store : candidateStores) {
            if (store->GetMinKey() <= maxKey && store->GetUpperBoundKey() > minKey) {
                stores.push_back(store);
            }
        }
    };

    takeStores(tabletSnapshot->GetEdenStores());
    takeStores(partition->Stores);

    YT_LOG_DEBUG("Creating schemaful tablet reader (TabletId: %v, CellId: %v, Timestamp: %v, WorkloadDescriptor: %v, "
        " ReadSessionId: %v, StoreIds: %v, StoreRanges: %v)",
        tabletSnapshot->TabletId,
        tabletSnapshot->CellId,
        timestamp,
        chunkReadOptions.WorkloadDescriptor,
        chunkReadOptions.ReadSessionId,
        MakeFormattableView(stores, TStoreIdFormatter()),
        MakeFormattableView(stores, TStoreRangeFormatter()));

    auto rowBuffer = New<TRowBuffer>(TTabletReaderPoolTag());

    auto rowMerger = CreateLatestTimestampRowMerger(
        std::move(rowBuffer),
        tabletSnapshot,
        columnFilter,
        timestampRange.RetentionTimestamp,
        timestampReadOptions);

    return CreateSchemafulOverlappingLookupReader(
        std::move(rowMerger),
        [
            =,
            stores = std::move(stores),
            index = 0
        ] () mutable -> IVersionedReaderPtr {
            if (index < std::ssize(stores)) {
                return stores[index++]->CreateReader(
                    tabletSnapshot,
                    keys,
                    timestamp,
                    false,
                    columnFilter,
                    chunkReadOptions,
                    workloadCategory);
            } else {
                return nullptr;
            }
        });
}

ISchemafulUnversionedReaderPtr CreateSchemafulLookupTabletReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    const TColumnFilter& columnFilter,
    const TSharedRange<TLegacyKey>& keys,
    TReadTimestampRange timestampRange,
    const TClientChunkReadOptions& chunkReadOptions,
    std::optional<ETabletDistributedThrottlerKind> tabletThrottlerKind,
    std::optional<EWorkloadCategory> workloadCategory,
    TTimestampReadOptions timestampReadOptions)
{
    if (!tabletSnapshot->PhysicalSchema->IsSorted()) {
        THROW_ERROR_EXCEPTION("Table %v is not sorted",
            tabletSnapshot->TableId);
    }

    std::vector<int> partitionIndexes;
    std::vector<TSharedRange<TLegacyKey>> partitionedKeys;
    auto currentIt = keys.Begin();
    while (currentIt != keys.End()) {
        auto nextPartitionIt = std::upper_bound(
            tabletSnapshot->PartitionList.begin(),
            tabletSnapshot->PartitionList.end(),
            *currentIt,
            [] (TLegacyKey lhs, const TPartitionSnapshotPtr& rhs) {
                return lhs < rhs->PivotKey;
            });
        YT_VERIFY(nextPartitionIt != tabletSnapshot->PartitionList.begin());
        auto nextIt = nextPartitionIt == tabletSnapshot->PartitionList.end()
            ? keys.End()
            : std::lower_bound(currentIt, keys.End(), (*nextPartitionIt)->PivotKey);
        partitionIndexes.push_back((nextPartitionIt - 1) - tabletSnapshot->PartitionList.begin());
        partitionedKeys.push_back(keys.Slice(currentIt, nextIt));
        currentIt = nextIt;
    }

    auto readerFactory = [
        =,
        partitionIndexes = std::move(partitionIndexes),
        partitionedKeys = std::move(partitionedKeys),
        timestampReadOptions = std::move(timestampReadOptions),
        index = 0
    ] () mutable -> ISchemafulUnversionedReaderPtr {
        if (index < std::ssize(partitionedKeys)) {
            auto reader = CreatePartitionLookupReader(
                tabletSnapshot,
                partitionIndexes[index],
                columnFilter,
                partitionedKeys[index],
                timestampRange,
                chunkReadOptions,
                tabletThrottlerKind,
                workloadCategory,
                timestampReadOptions);
            ++index;
            return reader;
        } else {
            return nullptr;
        }
    };

    auto reader = CreatePrefetchingOrderedSchemafulReader(std::move(readerFactory));

    return WrapSchemafulTabletReader(
        tabletThrottlerKind,
        tabletSnapshot,
        chunkReadOptions,
        columnFilter,
        std::move(reader));
}

////////////////////////////////////////////////////////////////////////////////

IVersionedReaderPtr CreateCompactionTabletReader(
    const TTabletSnapshotPtr& tabletSnapshot,
    std::vector<ISortedStorePtr> stores,
    TLegacyOwningKey lowerBound,
    TLegacyOwningKey upperBound,
    TTimestamp currentTimestamp,
    TTimestamp majorTimestamp,
    const TClientChunkReadOptions& chunkReadOptions,
    int minConcurrency,
    ETabletDistributedThrottlerKind tabletThrottlerKind,
    IThroughputThrottlerPtr perTabletThrottler,
    std::optional<EWorkloadCategory> workloadCategory,
    IMemoryUsageTrackerPtr rowMergerMemoryTracker)
{
    if (!tabletSnapshot->PhysicalSchema->IsSorted()) {
        THROW_ERROR_EXCEPTION("Table %v is not sorted",
            tabletSnapshot->TableId);
    }

    tabletSnapshot->WaitOnLocks(majorTimestamp);

    auto throttler = perTabletThrottler;

    if (const auto& distributedThrottler = tabletSnapshot->DistributedThrottlers[tabletThrottlerKind]) {
        throttler = NConcurrency::CreateCombinedThrottler({
            perTabletThrottler,
            distributedThrottler
        });
    }

    auto asyncResult = throttler->Throttle(1);
    if (asyncResult.IsSet()) {
        asyncResult.Get().ThrowOnError();
    } else {
        YT_LOG_DEBUG("Started waiting for compaction inbound throughput throttler");
        WaitFor(asyncResult)
            .ThrowOnError();
        YT_LOG_DEBUG("Finished waiting for compaction inbound throughput throttler");
    }

    YT_LOG_DEBUG(
        "Creating versioned tablet reader (TabletId: %v, CellId: %v, LowerBound: %v, UpperBound: %v, "
        "CurrentTimestamp: %v, MajorTimestamp: %v, WorkloadDescriptor: %v, ReadSessionId: %v, StoreIds: %v, StoreRanges: %v)",
        tabletSnapshot->TabletId,
        tabletSnapshot->CellId,
        lowerBound,
        upperBound,
        currentTimestamp,
        majorTimestamp,
        chunkReadOptions.WorkloadDescriptor,
        chunkReadOptions.ReadSessionId,
        MakeFormattableView(stores, TStoreIdFormatter()),
        MakeFormattableView(stores, TStoreRangeFormatter()));

    const auto& mountConfig = tabletSnapshot->Settings.MountConfig;

    auto rowMerger = CreateVersionedRowMerger(
        mountConfig->RowMergerType,
        New<TRowBuffer>(TTabletReaderPoolTag()),
        tabletSnapshot->QuerySchema,
        TColumnFilter(),
        mountConfig,
        currentTimestamp,
        majorTimestamp,
        tabletSnapshot->ColumnEvaluator,
        tabletSnapshot->CustomRuntimeData,
        /*mergeRowsOnFlush*/ false,
        /*useTtlColumn*/ true,
        /*mergeDeletionsOnFlush*/ false,
        std::move(rowMergerMemoryTracker));

    std::vector<TLegacyOwningKey> boundaries;
    boundaries.reserve(stores.size());
    for (const auto& store : stores) {
        boundaries.push_back(store->GetMinKey());
    }

    auto reader = CreateVersionedOverlappingRangeReader(
        std::move(boundaries),
        std::move(rowMerger),
        [
            =,
            stores = std::move(stores),
            lowerBound = std::move(lowerBound),
            upperBound = std::move(upperBound)
        ] (int index) {
            YT_VERIFY(index < std::ssize(stores));
            const auto& store = stores[index];
            return store->CreateReader(
                tabletSnapshot,
                MakeSingletonRowRange(lowerBound, upperBound),
                AllCommittedTimestamp,
                true,
                TColumnFilter(),
                chunkReadOptions,
                workloadCategory);
        },
        [keyComparer = tabletSnapshot->RowKeyComparer] (TUnversionedValueRange lhs, TUnversionedValueRange rhs) {
            return keyComparer(lhs, rhs);
        },
        minConcurrency);

    if (throttler) {
        return New<TThrottlerAwareVersionedReader>(
            std::move(reader),
            std::move(throttler));
    } else {
        return reader;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode

