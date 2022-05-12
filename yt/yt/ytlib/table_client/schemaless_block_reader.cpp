#include "schemaless_block_reader.h"
#include "private.h"
#include "helpers.h"

#include <yt/yt/client/table_client/key_bound.h>
#include <yt/yt/client/table_client/logical_type.h>
#include <yt/yt/client/table_client/schema.h>

#include <yt/yt/core/misc/algorithm_helpers.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

std::vector<bool> GetCompositeColumnFlags(const TTableSchemaPtr& schema)
{
    std::vector<bool> compositeColumnFlag;
    const auto& schemaColumns = schema->Columns();
    compositeColumnFlag.assign(schemaColumns.size(), false);
    for (int i = 0; i < std::ssize(schemaColumns); ++i) {
        compositeColumnFlag[i] = IsV3Composite(schemaColumns[i].LogicalType());
    }
    return compositeColumnFlag;
}

////////////////////////////////////////////////////////////////////////////////

THorizontalBlockReader::THorizontalBlockReader(
    const TSharedRef& block,
    const NProto::TDataBlockMeta& meta,
    const std::vector<bool>& compositeColumnFlags,
    const std::vector<int>& chunkToReaderIdMapping,
    TRange<ESortOrder> sortOrders,
    int commonKeyPrefix,
    const TKeyWideningOptions& keyWideningOptions,
    int extraColumnCount)
    : Block_(block)
    , Meta_(meta)
    , ChunkToReaderIdMapping_(chunkToReaderIdMapping)
    , CompositeColumnFlags_(compositeColumnFlags)
    , KeyWideningOptions_(keyWideningOptions)
    , SortOrders_(sortOrders.begin(), sortOrders.end())
    , CommonKeyPrefix_(commonKeyPrefix)
    , ExtraColumnCount_(extraColumnCount)
{
    YT_VERIFY(GetKeyColumnCount() >= GetChunkKeyColumnCount());
    YT_VERIFY(Meta_.row_count() > 0);

    auto keyDataSize = GetUnversionedRowByteSize(GetKeyColumnCount());
    KeyBuffer_.reserve(keyDataSize);
    Key_ = TMutableUnversionedRow::Create(KeyBuffer_.data(), GetKeyColumnCount());

    // NB: First key of the block will be initialized below during JumpToRowIndex(0) call.
    // Nulls here are used to widen chunk's key to comparator length.
    for (int index = 0; index < GetKeyColumnCount(); ++index) {
        Key_[index] = MakeUnversionedSentinelValue(EValueType::Null, index);
    }

    i64 offsetsLength = sizeof(ui32) * Meta_.row_count();

    Offsets_ = TRef(Block_.Begin(), Block_.Begin() + offsetsLength);
    Data_ = TRef(Offsets_.End(), Block_.End());

    JumpToRowIndex(0);
}

bool THorizontalBlockReader::NextRow()
{
    return JumpToRowIndex(RowIndex_ + 1);
}

bool THorizontalBlockReader::SkipToRowIndex(i64 rowIndex)
{
    YT_VERIFY(rowIndex >= RowIndex_);
    return JumpToRowIndex(rowIndex);
}

bool THorizontalBlockReader::SkipToKeyBound(const TKeyBoundRef& lowerBound)
{
    auto inBound = [&] (TUnversionedRow row) -> bool {
        // Key is already widened here.
        return TestKey(ToKeyRef(row), lowerBound, SortOrders_);
    };

    if (inBound(GetLegacyKey())) {
        return true;
    }

    // BinarySearch returns first element such that !pred(it).
    // We are looking for the first row such that Comparator_.TestKey(row, lowerBound);
    auto index = BinarySearch(
        RowIndex_,
        Meta_.row_count(),
        [&] (i64 index) {
            YT_VERIFY(JumpToRowIndex(index));
            return !inBound(GetLegacyKey());
        });

    return JumpToRowIndex(index);
}

bool THorizontalBlockReader::SkipToKey(TUnversionedRow lowerBound)
{
    return SkipToKeyBound(MakeKeyBoundRef(lowerBound, /* upper */ false, GetKeyColumnCount()));
}

// TODO(lukyan): Keep one versiond of get key.
TLegacyKey THorizontalBlockReader::GetLegacyKey() const
{
    return Key_;
}

TKey THorizontalBlockReader::GetKey() const
{
    return TKey::FromRowUnchecked(Key_, GetKeyColumnCount());
}

TUnversionedValue THorizontalBlockReader::TransformAnyValue(TUnversionedValue value)
{
    if (value.Type == EValueType::Any) {
        auto data = value.AsStringBuf();
        // Try to unpack any value.
        if (value.Id < CompositeColumnFlags_.size() && CompositeColumnFlags_[value.Id]) {
            value.Type = EValueType::Composite;
        } else {
            value = MakeUnversionedValue(data, value.Id, Lexer_);
        }
    }

    return value;
}

int THorizontalBlockReader::GetChunkKeyColumnCount() const
{
    return CommonKeyPrefix_;
}

int THorizontalBlockReader::GetKeyColumnCount() const
{
    return SortOrders_.Size();
}

TMutableUnversionedRow THorizontalBlockReader::GetRow(TChunkedMemoryPool* memoryPool)
{
    int totalValueCount = ValueCount_ + std::ssize(KeyWideningOptions_.InsertedColumnIds) + ExtraColumnCount_;
    auto row = TMutableUnversionedRow::Allocate(memoryPool, totalValueCount);
    int valueCount = 0;

    auto pushRegularValue = [&]() {
        TUnversionedValue value;
        CurrentPointer_ += ReadRowValue(CurrentPointer_, &value);

        const auto remappedId = ChunkToReaderIdMapping_[value.Id];
        if (remappedId >= 0) {
            value = TransformAnyValue(value);
            value.Id = remappedId;
            row[valueCount] = value;
            ++valueCount;
        }
    };

    auto pushNullValue = [&](int id) {
        row[valueCount] = MakeUnversionedNullValue(id);
        ++valueCount;
    };

    if (KeyWideningOptions_.InsertPosition < 0) {
        for (int i = 0; i < static_cast<int>(ValueCount_); ++i) {
            pushRegularValue();
        }
    } else {
        for (int i = 0; i < KeyWideningOptions_.InsertPosition; ++i) {
            pushRegularValue();
        }
        for (int id : KeyWideningOptions_.InsertedColumnIds) {
            pushNullValue(id);
        }
        for (int i = KeyWideningOptions_.InsertPosition; i < static_cast<int>(ValueCount_); ++i) {
            pushRegularValue();
        }
    }

    row.SetCount(valueCount);
    return row;
}

TMutableVersionedRow THorizontalBlockReader::GetVersionedRow(
    TChunkedMemoryPool* memoryPool,
    TTimestamp timestamp)
{
    int valueCount = 0;
    auto currentPointer = CurrentPointer_;
    for (int i = 0; i < static_cast<int>(ValueCount_); ++i) {
        TUnversionedValue value;
        currentPointer += ReadRowValue(currentPointer, &value);

        if (ChunkToReaderIdMapping_[value.Id] >= GetKeyColumnCount()) {
            ++valueCount;
        }
    }

    auto versionedRow = TMutableVersionedRow::Allocate(
        memoryPool,
        GetKeyColumnCount(),
        valueCount,
        1,
        0);

    for (int index = 0; index < GetKeyColumnCount(); ++index) {
        versionedRow.BeginKeys()[index] = MakeUnversionedSentinelValue(EValueType::Null, index);
    }

    TVersionedValue* currentValue = versionedRow.BeginValues();
    for (int i = 0; i < static_cast<int>(ValueCount_); ++i) {
        TUnversionedValue value;
        CurrentPointer_ += ReadRowValue(CurrentPointer_, &value);

        int id = ChunkToReaderIdMapping_[value.Id];
        if (id >= GetKeyColumnCount()) {
            value = TransformAnyValue(value);
            value.Id = id;
            *currentValue = MakeVersionedValue(value, timestamp);
            ++currentValue;
        } else if (id >= 0) {
            versionedRow.BeginKeys()[id] = value;
        }
    }

    versionedRow.BeginWriteTimestamps()[0] = timestamp;

    return versionedRow;
}


i64 THorizontalBlockReader::GetRowIndex() const
{
    return RowIndex_;
}

bool THorizontalBlockReader::JumpToRowIndex(i64 rowIndex)
{
    if (rowIndex >= Meta_.row_count()) {
        return false;
    }

    RowIndex_ = rowIndex;

    ui32 offset = *reinterpret_cast<const ui32*>(Offsets_.Begin() + rowIndex * sizeof(ui32));
    CurrentPointer_ = Data_.Begin() + offset;

    CurrentPointer_ += ReadVarUint32(CurrentPointer_, &ValueCount_);
    YT_VERIFY(static_cast<int>(ValueCount_) >= GetChunkKeyColumnCount());

    const char* ptr = CurrentPointer_;
    for (int i = 0; i < GetChunkKeyColumnCount(); ++i) {
        auto* currentKeyValue = Key_.Begin() + i;
        ptr += ReadRowValue(ptr, currentKeyValue);
        if (currentKeyValue->Type == EValueType::Any
            && currentKeyValue->Id < CompositeColumnFlags_.size()
            && CompositeColumnFlags_[currentKeyValue->Id])
        {
            currentKeyValue->Type = EValueType::Composite;
        }
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
