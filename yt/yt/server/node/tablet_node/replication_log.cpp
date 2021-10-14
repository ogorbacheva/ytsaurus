#include "replication_log.h"

#include "tablet.h"

#include <yt/yt/server/lib/tablet_node/config.h>

#include <yt/yt/client/table_client/helpers.h>
#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/unversioned_row.h>

namespace NYT::NTabletNode {

using namespace NApi;
using namespace NTableClient;
using namespace NTabletClient;

////////////////////////////////////////////////////////////////////////////////

i64 GetLogRowIndex(TUnversionedRow logRow)
{
    YT_ASSERT(logRow[1].Type == EValueType::Int64);
    return logRow[1].Data.Int64;
}

TTimestamp GetLogRowTimestamp(TUnversionedRow logRow)
{
    YT_ASSERT(logRow[2].Type == EValueType::Uint64);
    return logRow[2].Data.Uint64;
}

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

TUnversionedRow BuildOrderedLogRow(
    TUnversionedRow row,
    ERowModificationType changeType,
    TUnversionedRowBuilder* rowBuilder)
{
    YT_VERIFY(changeType == ERowModificationType::Write);

    for (int index = 0; index < static_cast<int>(row.GetCount()); ++index) {
        auto value = row[index];
        value.Id += 1;
        rowBuilder->AddValue(value);
    }
    return rowBuilder->GetRow();
}

TUnversionedRow BuildSortedLogRow(
    TUnversionedRow row,
    ERowModificationType changeType,
    const TTableSchemaPtr& tableSchema,
    TUnversionedRowBuilder* rowBuilder)
{
    rowBuilder->AddValue(MakeUnversionedInt64Value(static_cast<int>(changeType), 1));

    int keyColumnCount = tableSchema->GetKeyColumnCount();
    int valueColumnCount = tableSchema->GetValueColumnCount();

    YT_VERIFY(static_cast<int>(row.GetCount()) >= keyColumnCount);
    for (int index = 0; index < keyColumnCount; ++index) {
        auto value = row[index];
        value.Id += 2;
        rowBuilder->AddValue(value);
    }

    if (changeType == ERowModificationType::Write) {
        for (int index = 0; index < valueColumnCount; ++index) {
            rowBuilder->AddValue(MakeUnversionedSentinelValue(
                EValueType::Null,
                index * 2 + keyColumnCount + 2));
            rowBuilder->AddValue(MakeUnversionedUint64Value(
                static_cast<ui64>(EReplicationLogDataFlags::Missing),
                index * 2 + keyColumnCount + 3));
        }
        auto logRow = rowBuilder->GetRow();
        for (int index = keyColumnCount; index < static_cast<int>(row.GetCount()); ++index) {
            auto value = row[index];
            value.Id = (value.Id - keyColumnCount) * 2 + keyColumnCount + 2;
            logRow[value.Id] = value;
            auto& flags = logRow[value.Id + 1].Data.Uint64;
            flags &= ~static_cast<ui64>(EReplicationLogDataFlags::Missing);
            if (Any(value.Flags & EValueFlags::Aggregate)) {
                flags |= static_cast<ui64>(EReplicationLogDataFlags::Aggregate);
            }
        }
    }

    return rowBuilder->GetRow();
}

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

TUnversionedRow BuildLogRow(
    TUnversionedRow row,
    ERowModificationType changeType,
    const TTableSchemaPtr& tableSchema,
    TUnversionedRowBuilder* rowBuilder)
{
    rowBuilder->Reset();
    rowBuilder->AddValue(MakeUnversionedSentinelValue(EValueType::Null, 0));

    if (tableSchema->IsSorted()) {
        return NDetail::BuildSortedLogRow(row, changeType, tableSchema, rowBuilder);
    } else {
        return NDetail::BuildOrderedLogRow(row, changeType, rowBuilder);
    }
}

////////////////////////////////////////////////////////////////////////////////

class TReplicationLogParser
    : public IReplicationLogParser
{
public:
    TReplicationLogParser(
        TTableSchemaPtr tableSchema,
        TTableMountConfigPtr mountConfig,
        const NLogging::TLogger& logger)
        : IsSorted_(tableSchema->IsSorted())
        , PreserveTabletIndex_(mountConfig->PreserveTabletIndex)
        , TabletIndexColumnId_(tableSchema->ToReplicationLog()->GetColumnCount() + 1) /* maxColumnId - 1(timestamp) + 3(header size)*/
        , TimestampColumnId_(
            tableSchema->HasTimestampColumn()
                ? std::make_optional(tableSchema->GetColumnIndex(TimestampColumnName))
                : std::nullopt)
        , Logger(logger)
    { }

    std::optional<int> GetTimestampColumnId() override
    {
        return TimestampColumnId_;
    }

    void ParseLogRow(
        const TTabletSnapshotPtr& tabletSnapshot,
        TUnversionedRow logRow,
        const TRowBufferPtr& rowBuffer,
        TTypeErasedRow* replicationRow,
        ERowModificationType* modificationType,
        i64* rowIndex,
        TTimestamp* timestamp,
        bool isVersioned) override
    {
        *rowIndex = GetLogRowIndex(logRow);
        *timestamp = GetLogRowTimestamp(logRow);
        if (IsSorted_) {
            if (isVersioned) {
                ParseSortedLogRowWithTimestamps(
                    tabletSnapshot,
                    logRow,
                    rowBuffer,
                    *timestamp,
                    replicationRow,
                    modificationType);
            } else {
                ParseSortedLogRow(
                    tabletSnapshot,
                    logRow,
                    rowBuffer,
                    replicationRow,
                    modificationType);
            }
        } else {
            ParseOrderedLogRow(
                logRow,
                rowBuffer,
                replicationRow,
                modificationType,
                isVersioned);
        }
    }

private:
    const bool IsSorted_;
    const bool PreserveTabletIndex_;
    const int TabletIndexColumnId_;
    const std::optional<int> TimestampColumnId_;
    const NLogging::TLogger Logger;
    
    void ParseOrderedLogRow(
        TUnversionedRow logRow,
        const TRowBufferPtr& rowBuffer,
        TTypeErasedRow* replicationRow,
        ERowModificationType* modificationType,
        bool isVersioned)
    {
        constexpr int headerRows = 3;
        YT_VERIFY(static_cast<int>(logRow.GetCount()) >= headerRows);

        auto mutableReplicationRow = rowBuffer->AllocateUnversioned(logRow.GetCount() - headerRows);
        int columnCount = 0;
        for (int index = headerRows; index < static_cast<int>(logRow.GetCount()); ++index) {
            int id = index - headerRows;

            if (logRow[index].Id == TabletIndexColumnId_ && !PreserveTabletIndex_) {
                continue;
            }

            if (id == TimestampColumnId_) {
                continue;
            }

            mutableReplicationRow.Begin()[columnCount] = rowBuffer->CaptureValue(logRow[index]);
            mutableReplicationRow.Begin()[columnCount].Id = id;
            columnCount++;
        }

        if (isVersioned) {
            auto timestamp = GetLogRowTimestamp(logRow);
            YT_VERIFY(TimestampColumnId_);
            mutableReplicationRow.Begin()[columnCount++] = MakeUnversionedUint64Value(timestamp, *TimestampColumnId_);
        }

        mutableReplicationRow.SetCount(columnCount);

        *modificationType = isVersioned ? ERowModificationType::VersionedWrite : ERowModificationType::Write;
        *replicationRow = mutableReplicationRow.ToTypeErasedRow();
    }

    void ParseSortedLogRowWithTimestamps(
        const TTabletSnapshotPtr& tabletSnapshot,
        TUnversionedRow logRow,
        const TRowBufferPtr& rowBuffer,
        TTimestamp timestamp,
        TTypeErasedRow* result,
        ERowModificationType* modificationType)
    {
        TVersionedRow replicationRow;

        YT_ASSERT(logRow[3].Type == EValueType::Int64);
        auto changeType = ERowModificationType(logRow[3].Data.Int64);

        int keyColumnCount = tabletSnapshot->TableSchema->GetKeyColumnCount();
        int valueColumnCount = tabletSnapshot->TableSchema->GetValueColumnCount();
        const auto& mountConfig = tabletSnapshot->Settings.MountConfig;

        YT_ASSERT(static_cast<int>(logRow.GetCount()) == keyColumnCount + valueColumnCount * 2 + 4);

        switch (changeType) {
            case ERowModificationType::Write: {
                YT_ASSERT(static_cast<int>(logRow.GetCount()) >= keyColumnCount + 4);
                int replicationValueCount = 0;
                for (int logValueIndex = 0; logValueIndex < valueColumnCount; ++logValueIndex) {
                    const auto& value = logRow[logValueIndex * 2 + keyColumnCount + 5];
                    auto flags = FromUnversionedValue<EReplicationLogDataFlags>(value);
                    if (None(flags & EReplicationLogDataFlags::Missing)) {
                        ++replicationValueCount;
                    }
                }
                auto mutableReplicationRow = rowBuffer->AllocateVersioned(
                    keyColumnCount,
                    replicationValueCount,
                    1,  // writeTimestampCount
                    0); // deleteTimestampCount
                for (int keyIndex = 0; keyIndex < keyColumnCount; ++keyIndex) {
                    mutableReplicationRow.BeginKeys()[keyIndex] = rowBuffer->CaptureValue(logRow[keyIndex + 4]);
                }
                int replicationValueIndex = 0;
                for (int logValueIndex = 0; logValueIndex < valueColumnCount; ++logValueIndex) {
                    const auto& flagsValue = logRow[logValueIndex * 2 + keyColumnCount + 5];
                    YT_ASSERT(flagsValue.Type == EValueType::Uint64);
                    auto flags = static_cast<EReplicationLogDataFlags>(flagsValue.Data.Uint64);
                    if (None(flags & EReplicationLogDataFlags::Missing)) {
                        TVersionedValue value{};
                        static_cast<TUnversionedValue&>(value) = rowBuffer->CaptureValue(logRow[logValueIndex * 2 + keyColumnCount + 4]);
                        value.Id = logValueIndex + keyColumnCount;
                        if (Any(flags & EReplicationLogDataFlags::Aggregate)) {
                            value.Flags |= EValueFlags::Aggregate;
                        }
                        value.Timestamp = timestamp;
                        mutableReplicationRow.BeginValues()[replicationValueIndex++] = value;
                    }
                }
                YT_VERIFY(replicationValueIndex == replicationValueCount);
                mutableReplicationRow.BeginWriteTimestamps()[0] = timestamp;
                replicationRow = mutableReplicationRow;
                YT_LOG_DEBUG_IF(mountConfig->EnableReplicationLogging, "Replicating write (Row: %v)", replicationRow);
                break;
            }

            case ERowModificationType::Delete: {
                auto mutableReplicationRow = rowBuffer->AllocateVersioned(
                    keyColumnCount,
                    0,  // valueCount
                    0,  // writeTimestampCount
                    1); // deleteTimestampCount
                for (int index = 0; index < keyColumnCount; ++index) {
                    mutableReplicationRow.BeginKeys()[index] = rowBuffer->CaptureValue(logRow[index + 4]);
                }
                mutableReplicationRow.BeginDeleteTimestamps()[0] = timestamp;
                replicationRow = mutableReplicationRow;
                YT_LOG_DEBUG_IF(mountConfig->EnableReplicationLogging, "Replicating delete (Row: %v)", replicationRow);
                break;
            }

            default:
                YT_ABORT();
        }

        *modificationType = ERowModificationType::VersionedWrite;
        *result = replicationRow.ToTypeErasedRow();
    }

    void ParseSortedLogRow(
        const TTabletSnapshotPtr& tabletSnapshot,
        TUnversionedRow logRow,
        const TRowBufferPtr& rowBuffer,
        TTypeErasedRow* result,
        ERowModificationType* modificationType)
    {
        TUnversionedRow replicationRow;

        YT_ASSERT(logRow[3].Type == EValueType::Int64);
        auto changeType = ERowModificationType(logRow[3].Data.Int64);

        int keyColumnCount = tabletSnapshot->TableSchema->GetKeyColumnCount();
        int valueColumnCount = tabletSnapshot->TableSchema->GetValueColumnCount();
        const auto& mountConfig = tabletSnapshot->Settings.MountConfig;

        YT_ASSERT(static_cast<int>(logRow.GetCount()) == keyColumnCount + valueColumnCount * 2 + 4);

        *modificationType = ERowModificationType::Write;

        switch (changeType) {
            case ERowModificationType::Write: {
                YT_ASSERT(static_cast<int>(logRow.GetCount()) >= keyColumnCount + 4);
                int replicationValueCount = 0;
                for (int logValueIndex = 0; logValueIndex < valueColumnCount; ++logValueIndex) {
                    const auto& value = logRow[logValueIndex * 2 + keyColumnCount + 5];
                    auto flags = FromUnversionedValue<EReplicationLogDataFlags>(value);
                    if (None(flags & EReplicationLogDataFlags::Missing)) {
                        ++replicationValueCount;
                    }
                }
                auto mutableReplicationRow = rowBuffer->AllocateUnversioned(
                    keyColumnCount + replicationValueCount);
                for (int keyIndex = 0; keyIndex < keyColumnCount; ++keyIndex) {
                    mutableReplicationRow.Begin()[keyIndex] = rowBuffer->CaptureValue(logRow[keyIndex + 4]);
                    mutableReplicationRow.Begin()[keyIndex].Id = keyIndex;
                }
                int replicationValueIndex = 0;
                for (int logValueIndex = 0; logValueIndex < valueColumnCount; ++logValueIndex) {
                    const auto& flagsValue = logRow[logValueIndex * 2 + keyColumnCount + 5];
                    YT_ASSERT(flagsValue.Type == EValueType::Uint64);
                    auto flags = static_cast<EReplicationLogDataFlags>(flagsValue.Data.Uint64);
                    if (None(flags & EReplicationLogDataFlags::Missing)) {
                        TUnversionedValue value{};
                        static_cast<TUnversionedValue&>(value) = rowBuffer->CaptureValue(logRow[logValueIndex * 2 + keyColumnCount + 4]);
                        value.Id = logValueIndex + keyColumnCount;
                        if (Any(flags & EReplicationLogDataFlags::Aggregate)) {
                            value.Flags |= EValueFlags::Aggregate;
                        }
                        mutableReplicationRow.Begin()[keyColumnCount + replicationValueIndex++] = value;
                    }
                }
                YT_VERIFY(replicationValueIndex == replicationValueCount);
                replicationRow = mutableReplicationRow;
                YT_LOG_DEBUG_IF(mountConfig->EnableReplicationLogging, "Replicating write (Row: %v)", replicationRow);
                break;
            }

            case ERowModificationType::Delete: {
                auto mutableReplicationRow = rowBuffer->AllocateUnversioned(
                    keyColumnCount);
                for (int index = 0; index < keyColumnCount; ++index) {
                    mutableReplicationRow.Begin()[index] = rowBuffer->CaptureValue(logRow[index + 4]);
                    mutableReplicationRow.Begin()[index].Id = index;
                }
                replicationRow = mutableReplicationRow;
                *modificationType = ERowModificationType::Delete;
                YT_LOG_DEBUG_IF(mountConfig->EnableReplicationLogging, "Replicating delete (Row: %v)", replicationRow);
                break;
            }

            default:
                YT_ABORT();
        }

        *result = replicationRow.ToTypeErasedRow();
    }
};

////////////////////////////////////////////////////////////////////////////////

IReplicationLogParserPtr CreateReplicationLogParser(
    TTableSchemaPtr tableSchema,
    TTableMountConfigPtr mountConfig,
    const NLogging::TLogger& logger)
{
    return New<TReplicationLogParser>(std::move(tableSchema), std::move(mountConfig), logger);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
