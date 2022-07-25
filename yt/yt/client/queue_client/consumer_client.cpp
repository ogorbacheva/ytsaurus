#include "consumer_client.h"
#include "private.h"

#include <yt/yt/client/table_client/comparator.h>
#include <yt/yt/client/table_client/name_table.h>
#include <yt/yt/client/table_client/schema.h>
#include <yt/yt/client/table_client/helpers.h>
#include <yt/yt/client/table_client/check_schema_compatibility.h>

#include <yt/yt/client/api/rowset.h>
#include <yt/yt/client/api/client.h>
#include <yt/yt/client/api/transaction.h>

#include <yt/yt/client/transaction_client/helpers.h>

#include <library/cpp/iterator/functools.h>

namespace NYT::NQueueClient {

using namespace NTableClient;
using namespace NHiveClient;
using namespace NYPath;
using namespace NApi;
using namespace NConcurrency;
using namespace NTransactionClient;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = QueueClientLogger;

////////////////////////////////////////////////////////////////////////////////

static TTableSchemaPtr BigRTConsumerTableSchema = New<TTableSchema>(std::vector<TColumnSchema>{
    TColumnSchema("ShardId", EValueType::Uint64, ESortOrder::Ascending),
    TColumnSchema("Offset", EValueType::Uint64),
}, /*strict*/ true, /*uniqueKeys*/ true);

class TBigRTConsumerClient
    : public IConsumerClient
{
public:
    explicit TBigRTConsumerClient(TYPath path)
        : Path_(std::move(path))
    { }

    void Advance(
        const ITransactionPtr& transaction,
        int partitionIndex,
        std::optional<i64> oldOffset,
        i64 newOffset) const override
    {
        auto nameTable = TNameTable::FromSchema(*BigRTConsumerTableSchema);

        auto shardIdColumnId = nameTable->GetId("ShardId");

        if (oldOffset) {
            TUnversionedRowsBuilder keyRowsBuilder;
            TUnversionedRowBuilder rowBuilder;
            rowBuilder.AddValue(MakeUnversionedUint64Value(partitionIndex, shardIdColumnId));
            keyRowsBuilder.AddRow(rowBuilder.GetRow());

            TVersionedLookupRowsOptions options;
            options.RetentionConfig = New<TRetentionConfig>();
            options.RetentionConfig->MaxDataVersions = 1;

            auto partitionRowset = WaitFor(transaction->VersionedLookupRows(Path_, nameTable, keyRowsBuilder.Build(), options))
                .ValueOrThrow();
            const auto& rows = partitionRowset->GetRows();

            auto offsetColumnIdRead = partitionRowset->GetNameTable()->GetIdOrThrow("Offset");

            THROW_ERROR_EXCEPTION_UNLESS(
                std::ssize(partitionRowset->GetRows()) <= 1,
                "The table for consumer %v should contain at most one row for partition %v when an old offset is specified",
                Path_,
                partitionIndex);

            i64 currentOffset = 0;
            TTimestamp offsetTimestamp = 0;
            // If the key doesn't exist, or the offset value is null, the offset is -1 in BigRT terms and 0 in ours.
            if (!rows.empty()) {
                auto offsetValue = rows[0].BeginValues();
                YT_VERIFY(offsetValue->Id == offsetColumnIdRead);
                offsetTimestamp = offsetValue->Timestamp;
                if (offsetValue->Type == EValueType::Uint64) {
                    // We need to add 1, since BigRT stores the offset of the last read row.
                    currentOffset = static_cast<i64>(offsetValue->Data.Uint64) + 1;
                }

                YT_LOG_DEBUG(
                    "Read current offset (Consumer: %Qv, PartitionIndex: %v, Offset: %v, Timestamp: %v)",
                    Path_,
                    partitionIndex,
                    currentOffset,
                    offsetTimestamp);
            }

            if (currentOffset != *oldOffset) {
                THROW_ERROR_EXCEPTION(
                    EErrorCode::ConsumerOffsetConflict,
                    "Offset conflict at partition %v of consumer %Qv: expected offset %v, found offset %v",
                    partitionIndex,
                    Path_,
                    *oldOffset,
                    currentOffset)
                        << TErrorAttribute("partition", partitionIndex)
                        << TErrorAttribute("consumer", Path_)
                        << TErrorAttribute("expected_offset", *oldOffset)
                        << TErrorAttribute("current_offset", currentOffset)
                        << TErrorAttribute("current_offset_timestamp", offsetTimestamp);
            }
        }

        auto offsetColumnIdWrite = nameTable->GetId("Offset");

        TUnversionedRowsBuilder rowsBuilder;
        TUnversionedRowBuilder rowBuilder;
        rowBuilder.AddValue(MakeUnversionedUint64Value(partitionIndex, shardIdColumnId));
        if (newOffset >= 1) {
            // We need to subtract 1, since BigRT stores the offset of the last read row.
            rowBuilder.AddValue(MakeUnversionedUint64Value(newOffset - 1, offsetColumnIdWrite));
        } else {
            // For BigRT consumers we store 0 (in our terms) by storing null.
            rowBuilder.AddValue(MakeUnversionedNullValue(offsetColumnIdWrite));
        }
        rowsBuilder.AddRow(rowBuilder.GetRow());

        YT_LOG_DEBUG(
            "Advancing consumer offset (Path: %v, Partition: %v, Offset: %v -> %v)",
            Path_,
            partitionIndex,
            oldOffset,
            newOffset);
        transaction->WriteRows(Path_, nameTable, rowsBuilder.Build());
    }

    TFuture<std::vector<TPartitionInfo>> CollectPartitions(
        const IClientPtr& client,
        int expectedPartitionCount,
        bool withLastConsumeTime) const override
    {
        return BIND(
            &TBigRTConsumerClient::DoCollectPartitions,
            MakeStrong(this),
            client,
            expectedPartitionCount,
            withLastConsumeTime)
            .AsyncVia(GetCurrentInvoker())
            .Run();
    }

    static void ValidateSchemaOrThrow(const TTableSchema& schema)
    {
        if (auto [compatibility, error] = CheckTableSchemaCompatibility(
                *BigRTConsumerTableSchema,
                schema,
                /*ignoreSortOrder*/ true);
            compatibility != ESchemaCompatibility::FullyCompatible)
        {
            THROW_ERROR_EXCEPTION(
                "Consumer schema %v is not recognized as a BigRT consumer schema %v",
                schema,
                *BigRTConsumerTableSchema)
                << error;
        }
    }

private:
    TYPath Path_;

    std::vector<TPartitionInfo> DoCollectPartitions(
        const IClientPtr& client,
        int expectedPartitionCount,
        bool withLastConsumeTime) const
    {
        std::vector<TPartitionInfo> result;

        auto selectRowsResult = WaitFor(client->SelectRows(
            Format("[ShardId], [Offset] from [%v]", Path_)))
            .ValueOrThrow();

        // Note that after table construction table schema may have changed.
        // We must be prepared for that.

        ValidateSchemaOrThrow(*selectRowsResult.Rowset->GetSchema());

        std::vector<ui64> shardIndices;
        for (auto row : selectRowsResult.Rowset->GetRows()) {
            YT_VERIFY(row.GetCount() == 2);

            auto shardIdColumnId = selectRowsResult.Rowset->GetNameTable()->GetIdOrThrow("ShardId");
            const auto& shardIdValue = row[shardIdColumnId];
            YT_VERIFY(shardIdValue.Type == EValueType::Uint64);

            if (shardIdValue.Data.Uint64 >= static_cast<ui32>(expectedPartitionCount)) {
                // This row does not correspond to any partition considering the expected partition count,
                // so just skip it.
                continue;
            }

            shardIndices.push_back(shardIdValue.Data.Uint64);

            auto offsetColumnId = selectRowsResult.Rowset->GetNameTable()->GetIdOrThrow("Offset");
            const auto& offsetValue = row[offsetColumnId];

            i64 nextRowIndex;
            if (offsetValue.Type == EValueType::Uint64) {
                nextRowIndex = static_cast<i64>(offsetValue.Data.Uint64) + 1;
            } else if (offsetValue.Type == EValueType::Null) {
                nextRowIndex = 0;
            } else {
                YT_ABORT();
            }

            // NB: in BigRT offsets encode the last read row, while we operate with the first unread row.
            result.emplace_back(TPartitionInfo{
                .PartitionIndex = static_cast<i64>(shardIdValue.Data.Uint64),
                .NextRowIndex = nextRowIndex,
            });
        }

        if (!withLastConsumeTime) {
            return result;
        }

        // Now do versioned lookups in order to obtain timestamps.

        TUnversionedRowsBuilder builder;
        for (ui64 shardIndex : shardIndices) {
            builder.AddRow(shardIndex);
        }

        TVersionedLookupRowsOptions options;
        // This allows easier detection of key set change during the query.
        options.KeepMissingRows = true;

        auto versionedRowset = WaitFor(client->VersionedLookupRows(
            Path_,
            TNameTable::FromKeyColumns(BigRTConsumerTableSchema->GetKeyColumns()),
            builder.Build(),
            options))
            .ValueOrThrow();

        YT_VERIFY(versionedRowset->GetRows().size() == shardIndices.size());

        for (const auto& [index, versionedRow] : Enumerate(versionedRowset->GetRows())) {
            if (versionedRow.GetWriteTimestampCount() < 1) {
                THROW_ERROR_EXCEPTION("Shard set changed during collection");
            }
            auto timestamp = versionedRow.BeginWriteTimestamps()[0];
            result[index].LastConsumeTime = TimestampToInstant(timestamp).first;
        }

        return result;
    }
};

////////////////////////////////////////////////////////////////////////////////

IConsumerClientPtr CreateConsumerClient(
    const TYPath& path,
    const TTableSchema& schema)
{
    if (!schema.IsUniqueKeys()) {
        THROW_ERROR_EXCEPTION("Consumer schema must have unique keys, schema %v does not", schema);
    }

    if (CheckTableSchemaCompatibility(*BigRTConsumerTableSchema, schema, /*ignoreSortOrder*/ false).first == ESchemaCompatibility::FullyCompatible) {
        return New<TBigRTConsumerClient>(path);
    } else {
        THROW_ERROR_EXCEPTION("Table schema %v is not recognized as a valid consumer schema", schema);
    }

}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueueClient
