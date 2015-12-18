#include "unversioned_row.h"

#include <yt/ytlib/table_client/name_table.h>
#include <yt/ytlib/table_client/row_buffer.h>
#include <yt/ytlib/table_client/schema.h>

#include <yt/core/misc/farm_hash.h>
#include <yt/core/misc/hash.h>
#include <yt/core/misc/string.h>
#include <yt/core/misc/varint.h>

#include <yt/core/yson/consumer.h>

#include <yt/core/ytree/attribute_helpers.h>
#include <yt/core/ytree/node.h>

#include <util/generic/ymath.h>

#include <util/stream/str.h>

#include <cmath>

namespace NYT {
namespace NTableClient {

using namespace NChunkClient;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

static const Stroka SerializedNullRow("");
struct TOwningRowTag { };

////////////////////////////////////////////////////////////////////////////////

int GetByteSize(const TUnversionedValue& value)
{
    int result = MaxVarUint32Size * 2; // id and type

    switch (value.Type) {
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            break;

        case EValueType::Int64:
        case EValueType::Uint64:
            result += MaxVarInt64Size;
            break;

        case EValueType::Double:
            result += sizeof(double);
            break;

        case EValueType::Boolean:
            result += 1;
            break;

        case EValueType::String:
        case EValueType::Any:
            result += MaxVarUint32Size + value.Length;
            break;

        default:
            YUNREACHABLE();
    }

    return result;
}

int GetDataWeight(const TUnversionedValue& value)
{
    switch (value.Type) {
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            return 0;

        case EValueType::Int64:
            return sizeof(i64);

        case EValueType::Uint64:
            return sizeof(ui64);

        case EValueType::Double:
            return sizeof(double);

        case EValueType::Boolean:
            return 1;

        case EValueType::String:
        case EValueType::Any:
            return value.Length;

        default:
            YUNREACHABLE();
    }
}

int WriteValue(char* output, const TUnversionedValue& value)
{
    char* current = output;

    current += WriteVarUint32(current, value.Id);
    current += WriteVarUint32(current, static_cast<ui16>(value.Type));

    switch (value.Type) {
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            break;

        case EValueType::Int64:
            current += WriteVarInt64(current, value.Data.Int64);
            break;

        case EValueType::Uint64:
            current += WriteVarUint64(current, value.Data.Uint64);
            break;

        case EValueType::Double:
            ::memcpy(current, &value.Data.Double, sizeof (double));
            current += sizeof (double);
            break;

        case EValueType::Boolean:
            *current++ = value.Data.Boolean ? '\x01' : '\x00';
            break;

        case EValueType::String:
        case EValueType::Any:
            current += WriteVarUint32(current, value.Length);
            ::memcpy(current, value.Data.String, value.Length);
            current += value.Length;
            break;

        default:
            YUNREACHABLE();
    }

    return current - output;
}

int ReadValue(const char* input, TUnversionedValue* value)
{
    const char* current = input;

    ui32 id;
    current += ReadVarUint32(current, &id);
    value->Id = static_cast<ui16>(id);

    ui32 type;
    current += ReadVarUint32(current, &type);
    value->Type = static_cast<EValueType>(type);

    switch (value->Type) {
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            break;

        case EValueType::Int64:
            current += ReadVarInt64(current, &value->Data.Int64);
            break;

        case EValueType::Uint64:
            current += ReadVarUint64(current, &value->Data.Uint64);
            break;

        case EValueType::Double:
            ::memcpy(&value->Data.Double, current, sizeof (double));
            current += sizeof (double);
            break;

        case EValueType::Boolean:
            value->Data.Boolean = (*current) == 1;
            current += 1;
            break;

        case EValueType::String:
        case EValueType::Any:
            current += ReadVarUint32(current, &value->Length);
            value->Data.String = current;
            current += value->Length;
            break;

        default:
            YUNREACHABLE();
    }

    return current - input;
}

////////////////////////////////////////////////////////////////////////////////

void Save(TStreamSaveContext& context, const TUnversionedValue& value)
{
    auto* output = context.GetOutput();
    if (IsStringLikeType(value.Type)) {
        output->Write(&value, sizeof (ui16) + sizeof (ui16) + sizeof (ui32)); // Id, Type, Length
        if (value.Length != 0) {
            output->Write(value.Data.String, value.Length);
        }
    } else {
        output->Write(&value, sizeof (TUnversionedValue));
    }
}

void Load(TStreamLoadContext& context, TUnversionedValue& value, TChunkedMemoryPool* pool)
{
    auto* input = context.GetInput();
    const size_t fixedSize = sizeof (ui16) + sizeof (ui16) + sizeof (ui32); // Id, Type, Length
    YCHECK(input->Load(&value, fixedSize) == fixedSize);
    if (IsStringLikeType(value.Type)) {
        if (value.Length != 0) {
            value.Data.String = pool->AllocateUnaligned(value.Length);
            YCHECK(input->Load(const_cast<char*>(value.Data.String), value.Length) == value.Length);
        } else {
            value.Data.String = nullptr;
        }
    } else {
        YCHECK(input->Load(&value.Data, sizeof (value.Data)) == sizeof (value.Data));
    }
}

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(const TUnversionedValue& value)
{
    switch (value.Type) {
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            return Format("<%v>", value.Type);

        case EValueType::Int64:
            return Format("%vi", value.Data.Int64);

        case EValueType::Uint64:
            return Format("%vu", value.Data.Uint64);

        case EValueType::Double:
            return Format("%v", value.Data.Double);

        case EValueType::Boolean:
            return Format("%v", value.Data.Boolean);

        case EValueType::String:
            return Stroka(value.Data.String, value.Length).Quote();

        case EValueType::Any:
            return ConvertToYsonString(
                    TYsonString(Stroka(value.Data.String, value.Length)),
                    EYsonFormat::Text)
                .Data();

        default:
            YUNREACHABLE();
    }
}

int CompareRowValues(const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    if (lhs.Type == EValueType::Any || rhs.Type == EValueType::Any) {
        if (lhs.Type != EValueType::Min &&
            lhs.Type != EValueType::Max &&
            rhs.Type != EValueType::Min &&
            rhs.Type != EValueType::Max)
        {
            // Never compare composite values with non-sentinels.
            THROW_ERROR_EXCEPTION(
                EErrorCode::IncomparableType,
                "Composite types are not comparable");
        }
    }

    if (Y_UNLIKELY(lhs.Type != rhs.Type)) {
        return static_cast<int>(lhs.Type) - static_cast<int>(rhs.Type);
    }

    switch (lhs.Type) {
        case EValueType::Int64: {
            auto lhsValue = lhs.Data.Int64;
            auto rhsValue = rhs.Data.Int64;
            if (lhsValue < rhsValue) {
                return -1;
            } else if (lhsValue > rhsValue) {
                return +1;
            } else {
                return 0;
            }
        }

        case EValueType::Uint64: {
            auto lhsValue = lhs.Data.Uint64;
            auto rhsValue = rhs.Data.Uint64;
            if (lhsValue < rhsValue) {
                return -1;
            } else if (lhsValue > rhsValue) {
                return +1;
            } else {
                return 0;
            }
        }

        case EValueType::Double: {
            double lhsValue = lhs.Data.Double;
            double rhsValue = rhs.Data.Double;
            if (lhsValue < rhsValue) {
                return -1;
            } else if (lhsValue > rhsValue) {
                return +1;
            } else {
                return 0;
            }
        }

        case EValueType::Boolean: {
            bool lhsValue = lhs.Data.Boolean;
            bool rhsValue = rhs.Data.Boolean;
            if (lhsValue < rhsValue) {
                return -1;
            } else if (lhsValue > rhsValue) {
                return +1;
            } else {
                return 0;
            }
        }

        case EValueType::String: {
            size_t lhsLength = lhs.Length;
            size_t rhsLength = rhs.Length;
            size_t minLength = std::min(lhsLength, rhsLength);
            int result = ::memcmp(lhs.Data.String, rhs.Data.String, minLength);
            if (result == 0) {
                if (lhsLength < rhsLength) {
                    return -1;
                } else if (lhsLength > rhsLength) {
                    return +1;
                } else {
                    return 0;
                }
            } else {
                return result;
            }
        }

        // NB: All sentinel types are equal.
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
            return 0;

        case EValueType::Any:
        default:
            YUNREACHABLE();
    }
}

bool operator == (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) == 0;
}

bool operator != (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) != 0;
}

bool operator <= (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) <= 0;
}

bool operator < (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) < 0;
}

bool operator >= (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) >= 0;
}

bool operator > (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) > 0;
}

////////////////////////////////////////////////////////////////////////////////

int CompareRows(
    const TUnversionedValue* lhsBegin,
    const TUnversionedValue* lhsEnd,
    const TUnversionedValue* rhsBegin,
    const TUnversionedValue* rhsEnd)
{
    auto* lhsCurrent = lhsBegin;
    auto* rhsCurrent = rhsBegin;
    while (lhsCurrent != lhsEnd && rhsCurrent != rhsEnd) {
        int result = CompareRowValues(*lhsCurrent++, *rhsCurrent++);
        if (result != 0) {
            return result;
        }
    }
    return (lhsEnd - lhsBegin) - (rhsEnd - rhsBegin);
}

int CompareRows(TUnversionedRow lhs, TUnversionedRow rhs, int prefixLength)
{
    if (!lhs && !rhs) {
        return 0;
    }

    if (lhs && !rhs) {
        return +1;
    }

    if (!lhs && rhs) {
        return -1;
    }

    return CompareRows(
        lhs.Begin(),
        lhs.Begin() + std::min(lhs.GetCount(), prefixLength),
        rhs.Begin(),
        rhs.Begin() + std::min(rhs.GetCount(), prefixLength));
}

bool operator == (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) == 0;
}

bool operator != (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) != 0;
}

bool operator <= (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) <= 0;
}

bool operator < (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) < 0;
}

bool operator >= (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) >= 0;
}

bool operator > (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) > 0;
}

////////////////////////////////////////////////////////////////////////////////

bool operator == (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs.Get()) == 0;
}

bool operator != (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs.Get()) != 0;
}

bool operator <= (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs.Get()) <= 0;
}

bool operator < (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs.Get()) < 0;
}

bool operator >= (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs.Get()) >= 0;
}

bool operator > (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs.Get()) > 0;
}

////////////////////////////////////////////////////////////////////////////////

bool operator == (const TUnversionedOwningRow& lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs.Get(), rhs) == 0;
}

bool operator != (const TUnversionedOwningRow& lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs.Get(), rhs) != 0;
}

bool operator <= (const TUnversionedOwningRow& lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs.Get(), rhs) <= 0;
}

bool operator < (const TUnversionedOwningRow& lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs.Get(), rhs) < 0;
}

bool operator >= (const TUnversionedOwningRow& lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs.Get(), rhs) >= 0;
}

bool operator > (const TUnversionedOwningRow& lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs.Get(), rhs) > 0;
}

////////////////////////////////////////////////////////////////////////////////

int CompareRows(const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs, int prefixLength)
{
    return CompareRows(lhs.Get(), rhs.Get(), prefixLength);
}

bool operator == (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) == 0;
}

bool operator != (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) != 0;
}

bool operator <= (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) <= 0;
}

bool operator < (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) < 0;
}

bool operator >= (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) >= 0;
}

bool operator > (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) > 0;
}

void ResetRowValues(TMutableUnversionedRow* row)
{
    for (int index = 0; index < row->GetCount(); ++index) {
        (*row)[index].Type = EValueType::Null;
    }
}

ui64 GetHash(TUnversionedRow row, int keyColumnCount)
{
    // NB: hash function may change in future. Use fingerprints for persistent hashing.
    return GetFarmFingerprint(row, keyColumnCount);
}

TFingerprint GetFarmFingerprint(TUnversionedRow row, int keyColumnCount)
{
    int partCount = std::min(row.GetCount(), keyColumnCount);
    const auto* begin = row.Begin();
    return GetFarmFingerprint(begin, begin + partCount);
}

size_t GetUnversionedRowByteSize(int valueCount)
{
    return sizeof(TUnversionedRowHeader) + sizeof(TUnversionedValue) * valueCount;
}

i64 GetDataWeight(TUnversionedRow row)
{
    return std::accumulate(
        row.Begin(),
        row.End(),
        0ll,
        [] (i64 x, const TUnversionedValue& value) {
            return GetDataWeight(value) + x;
        });
}

////////////////////////////////////////////////////////////////////////////////

TMutableUnversionedRow TMutableUnversionedRow::Allocate(TChunkedMemoryPool* pool, int valueCount)
{
    size_t byteSize = GetUnversionedRowByteSize(valueCount);
    auto* header = reinterpret_cast<TUnversionedRowHeader*>(pool->AllocateAligned(byteSize));
    header->Count = valueCount;
    header->Capacity = valueCount;
    return TMutableUnversionedRow(header);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

void ValidateDynamicValue(const TUnversionedValue& value)
{
    switch (value.Type) {
        case EValueType::String:
        case EValueType::Any:
            if (value.Length > MaxStringValueLength) {
                THROW_ERROR_EXCEPTION("Value is too long: length %v, limit %v",
                    value.Length,
                    MaxStringValueLength);
            }
            break;

        case EValueType::Double:
            if (std::isnan(value.Data.Double)) {
                THROW_ERROR_EXCEPTION("Value of type \"double\" is not a number");
            }
            break;

        default:
            break;
    }
}

int ApplyIdMapping(
    const TUnversionedValue& value,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping* idMappingPtr)
{
    auto id = value.Id;
    int schemaId = id;
    if (idMappingPtr != nullptr) {
        const auto& idMapping = *idMappingPtr;
        if (id >= idMapping.size()) {
            THROW_ERROR_EXCEPTION("Invalid column id: actual %v, expected in range [0,%v]",
                id,
                idMapping.size() - 1);
        }
        schemaId = idMapping[id];
    }
    if (schemaId < 0 || schemaId >= schema.Columns().size()) {
        THROW_ERROR_EXCEPTION("Invalid mapped column id: actual %v, expected in range [0,%v]",
            schemaId,
            schema.Columns().size());
    }
    return schemaId;
}

void ValidateKeyPart(
    TUnversionedRow row,
    int keyColumnCount,
    const TTableSchema& schema)
{
    ValidateKeyColumnCount(keyColumnCount);

    if (row.GetCount() < keyColumnCount) {
        THROW_ERROR_EXCEPTION("Too few values in row: actual %v, expected >= %v",
            row.GetCount(),
            keyColumnCount);
    }

    for (int index = 0; index < keyColumnCount; ++index) {
        const auto& value = row[index];
        ValidateKeyValue(value);
        int schemaId = ApplyIdMapping(value, schema, nullptr);
        ValidateValueType(value, schema, schemaId);
        if (schemaId != index) {
            THROW_ERROR_EXCEPTION("Invalid column: actual %Qv, expected %Qv",
                schema.Columns()[schemaId].Name,
                schema.Columns()[index].Name);
        }
    }
}

void ValidateDataRow(
    TUnversionedRow row,
    int keyColumnCount,
    const TNameTableToSchemaIdMapping* idMappingPtr,
    const TTableSchema& schema)
{
    ValidateRowValueCount(row.GetCount());
    ValidateKeyPart(row, keyColumnCount, schema);

    for (int index = keyColumnCount; index < row.GetCount(); ++index) {
        const auto& value = row[index];
        ValidateDataValue(value);
        int schemaId = ApplyIdMapping(value, schema, idMappingPtr);
        ValidateValueType(value, schema, schemaId);
    }
}

void ValidateKey(
    TKey key,
    int keyColumnCount,
    const TTableSchema& schema)
{
    if (!key) {
        THROW_ERROR_EXCEPTION("Key cannot be null");
    }

    if (key.GetCount() != keyColumnCount) {
        THROW_ERROR_EXCEPTION("Invalid number of key components: expected %v, actual %v",
            keyColumnCount,
            key.GetCount());
    }

    ValidateKeyPart(key, keyColumnCount, schema);
}

void ValidateClientRow(
    TUnversionedRow row,
    int keyColumnCount,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMapping,
    bool isKey)
{
    ValidateRowValueCount(row.GetCount());
    ValidateKeyColumnCount(keyColumnCount);

    bool keyColumnSeen[MaxKeyColumnCount] {};

    for (int index = 0; index < row.GetCount(); ++index) {
        const auto& value = row[index];
        int schemaId = ApplyIdMapping(value, schema, &idMapping);
        const auto& column = schema.Columns()[schemaId];
        ValidateValueType(value, schema, schemaId);

        if (column.Expression) {
            THROW_ERROR_EXCEPTION(
                "Column %Qv is computed automatically and should not be provided by user",
                column.Name);
        }

        if (schemaId < keyColumnCount) {
            if (keyColumnSeen[schemaId]) {
                THROW_ERROR_EXCEPTION("Duplicate key column %Qv",
                    column.Name);
            }

            keyColumnSeen[schemaId] = true;
            ValidateKeyValue(value);
        } else if (isKey) {
                THROW_ERROR_EXCEPTION("Non-key column %Qv in a key",
                    column.Name);
        } else {
            ValidateDataValue(value);
        }
    }

    for (int index = 0; index < keyColumnCount; ++index) {
        if (!keyColumnSeen[index] && !schema.Columns()[index].Expression) {
            THROW_ERROR_EXCEPTION("Missing key column %Qv",
                schema.Columns()[index].Name);
        }
    }
}

} // namespace

void ValidateValueType(
    const TUnversionedValue& value,
    const TTableSchema& schema,
    int schemaId)
{
    if (value.Type != EValueType::Null && value.Type != schema.Columns()[schemaId].Type) {
        THROW_ERROR_EXCEPTION("Invalid type of column %Qv: expected %Qlv or %Qlv but got %Qlv",
            schema.Columns()[schemaId].Name,
            schema.Columns()[schemaId].Type,
            EValueType::Null,
            value.Type);
    }
}

void ValidateStaticValue(const TUnversionedValue& value)
{
    ValidateDataValueType(value.Type);
    switch (value.Type) {
        case EValueType::String:
        case EValueType::Any:
            if (value.Length > MaxRowWeightLimit) {
                THROW_ERROR_EXCEPTION("Value is too long: length %v, limit %v",
                    value.Length,
                    MaxRowWeightLimit);
            }
            break;

        case EValueType::Double:
            if (std::isnan(value.Data.Double)) {
                THROW_ERROR_EXCEPTION("Value of type \"double\" is not a number");
            }
            break;

        default:
            break;
    }
}

void ValidateDataValue(const TUnversionedValue& value)
{
    ValidateDataValueType(value.Type);
    ValidateDynamicValue(value);
}

void ValidateKeyValue(const TUnversionedValue& value)
{
    ValidateKeyValueType(value.Type);
    ValidateDynamicValue(value);
}

void ValidateRowValueCount(int count)
{
    if (count < 0) {
        THROW_ERROR_EXCEPTION("Negative number of values in row");
    }
    if (count > MaxValuesPerRow) {
        THROW_ERROR_EXCEPTION("Too many values in row: actual %v, limit %v",
            count,
            MaxValuesPerRow);
    }
}

void ValidateKeyColumnCount(int count)
{
    if (count < 0) {
        THROW_ERROR_EXCEPTION("Negative number of key columns");
    }
    if (count == 0) {
        THROW_ERROR_EXCEPTION("At least one key column expected");
    }
    if (count > MaxKeyColumnCount) {
        THROW_ERROR_EXCEPTION("Too many columns in key: actual %v, limit %v",
            count,
            MaxKeyColumnCount);
    }
}

void ValidateRowCount(int count)
{
    if (count < 0) {
        THROW_ERROR_EXCEPTION("Negative number of rows in rowset");
    }
    if (count > MaxRowsPerRowset) {
        THROW_ERROR_EXCEPTION("Too many rows in rowset: actual %v, limit %v",
            count,
            MaxRowsPerRowset);
    }
}

void ValidateClientDataRow(
    TUnversionedRow row,
    int keyColumnCount,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMapping)
{
    ValidateClientRow(row, keyColumnCount, schema, idMapping, false);
}

void ValidateServerDataRow(
    TUnversionedRow row,
    int keyColumnCount,
    const TTableSchema& schema)
{
    ValidateDataRow(row, keyColumnCount, nullptr, schema);
}

void ValidateClientKey(
    TKey key,
    int keyColumnCount,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMapping)
{
    ValidateClientRow(key, keyColumnCount, schema, idMapping, true);
}

void ValidateServerKey(
    TKey key,
    int keyColumnCount,
    const TTableSchema& schema)
{
    ValidateKey(key, keyColumnCount, schema);
}

void ValidateReadTimestamp(TTimestamp timestamp)
{
    if (timestamp != SyncLastCommittedTimestamp &&
        timestamp != AsyncLastCommittedTimestamp &&
        (timestamp < MinTimestamp || timestamp > MaxTimestamp))
    {
        THROW_ERROR_EXCEPTION("Invalid timestamp %v", timestamp);
    }
}

////////////////////////////////////////////////////////////////////////////////

TOwningKey GetKeySuccessorImpl(TKey key, int prefixLength, EValueType sentinelType)
{
    auto length = std::min(prefixLength, key.GetCount());
    TUnversionedOwningRowBuilder builder(length + 1);
    for (int index = 0; index < length; ++index) {
        builder.AddValue(key[index]);
    }
    builder.AddValue(MakeUnversionedSentinelValue(sentinelType));
    return builder.FinishRow();
}

TOwningKey GetKeySuccessor(TKey key)
{
    return GetKeySuccessorImpl(
        key,
        key.GetCount(),
        EValueType::Min);
}

TOwningKey GetKeyPrefixSuccessor(TKey key, int prefixLength)
{
    return GetKeySuccessorImpl(
        key,
        prefixLength,
        EValueType::Max);
}

TOwningKey GetKeyPrefix(TKey key, int prefixLength)
{
    return TOwningKey(
        key.Begin(),
        key.Begin() + std::min(key.GetCount(), prefixLength));
}

////////////////////////////////////////////////////////////////////////////////

static TOwningKey MakeSentinelKey(EValueType type)
{
    TUnversionedOwningRowBuilder builder;
    builder.AddValue(MakeUnversionedSentinelValue(type));
    return builder.FinishRow();
}

static const TOwningKey CachedMinKey = MakeSentinelKey(EValueType::Min);
static const TOwningKey CachedMaxKey = MakeSentinelKey(EValueType::Max);

const TOwningKey MinKey()
{
    return CachedMinKey;
}

const TOwningKey MaxKey()
{
    return CachedMaxKey;
}

static TOwningKey MakeEmptyKey()
{
    TUnversionedOwningRowBuilder builder;
    return builder.FinishRow();
}

static const TOwningKey CachedEmptyKey = MakeEmptyKey();

const TOwningKey EmptyKey()
{
    return CachedEmptyKey;
}

const TOwningKey& ChooseMinKey(const TOwningKey& a, const TOwningKey& b)
{
    int result = CompareRows(a, b);
    return result <= 0 ? a : b;
}

const TOwningKey& ChooseMaxKey(const TOwningKey& a, const TOwningKey& b)
{
    int result = CompareRows(a, b);
    return result >= 0 ? a : b;
}

////////////////////////////////////////////////////////////////////////////////

Stroka SerializeToString(const TUnversionedValue* begin, const TUnversionedValue* end)
{
    int size = 2 * MaxVarUint32Size; // header size
    for (auto* it = begin; it != end; ++it) {
        size += GetByteSize(*it);
    }

    Stroka buffer;
    buffer.resize(size);

    char* current = const_cast<char*>(buffer.data());
    current += WriteVarUint32(current, 0); // format version
    current += WriteVarUint32(current, static_cast<ui32>(std::distance(begin, end)));

    for (auto* it = begin; it != end; ++it) {
        current += WriteValue(current, *it);
    }

    buffer.resize(current - buffer.data());

    return buffer;
}

Stroka SerializeToString(TUnversionedRow row)
{
    return row
        ? SerializeToString(row.Begin(), row.End())
        : SerializedNullRow;
}

TUnversionedOwningRow DeserializeFromString(const Stroka& data)
{
    if (data == SerializedNullRow) {
        return TUnversionedOwningRow();
    }

    const char* current = data.data();

    ui32 version;
    current += ReadVarUint32(current, &version);
    YCHECK(version == 0);

    ui32 valueCount;
    current += ReadVarUint32(current, &valueCount);

    size_t fixedSize = GetUnversionedRowByteSize(valueCount);
    auto rowData = TSharedMutableRef::Allocate<TOwningRowTag>(fixedSize, false);
    auto* header = reinterpret_cast<TUnversionedRowHeader*>(rowData.Begin());

    header->Count = static_cast<i32>(valueCount);

    auto* values = reinterpret_cast<TUnversionedValue*>(header + 1);
    for (int index = 0; index < valueCount; ++index) {
        TUnversionedValue* value = values + index;
        current += ReadValue(current, value);
    }

    return TUnversionedOwningRow(std::move(rowData), data);
}

void ToProto(TProtoStringType* protoRow, TUnversionedRow row)
{
    *protoRow = SerializeToString(row);
}

void ToProto(TProtoStringType* protoRow, const TUnversionedOwningRow& row)
{
    ToProto(protoRow, row.Get());
}

void ToProto(TProtoStringType* protoRow, const TUnversionedValue* begin, const TUnversionedValue* end)
{
    *protoRow = SerializeToString(begin, end);
}

void FromProto(TUnversionedOwningRow* row, const TProtoStringType& protoRow)
{
    *row = DeserializeFromString(protoRow);
}

void FromProto(TUnversionedRow* row, const TProtoStringType& protoRow, const TRowBufferPtr& rowBuffer)
{
    if (protoRow == SerializedNullRow) {
        *row = TUnversionedRow();
    }

    const char* current = protoRow.data();

    ui32 version;
    current += ReadVarUint32(current, &version);
    YCHECK(version == 0);

    ui32 valueCount;
    current += ReadVarUint32(current, &valueCount);

    auto mutableRow = TMutableUnversionedRow::Allocate(rowBuffer->GetPool(), valueCount);
    *row = mutableRow;

    auto* values = mutableRow.Begin();
    for (auto* value = values; value < values + valueCount; ++value) {
        current += ReadValue(current, value);
        rowBuffer->Capture(value);
    }
}

Stroka ToString(TUnversionedRow row)
{
    return row
        ? "[" + JoinToString(row.Begin(), row.End()) + "]"
        : "<Null>";
}

Stroka ToString(TMutableUnversionedRow row)
{
    return ToString(TUnversionedRow(row));
}

Stroka ToString(const TUnversionedOwningRow& row)
{
    return ToString(row.Get());
}

void FromProto(TUnversionedOwningRow* row, const NChunkClient::NProto::TKey& protoKey)
{
    TUnversionedOwningRowBuilder rowBuilder(protoKey.parts_size());
    for (int id = 0; id < protoKey.parts_size(); ++id) {
        auto& keyPart = protoKey.parts(id);
        switch (ELegacyKeyPartType(keyPart.type())) {
            case ELegacyKeyPartType::Null:
                rowBuilder.AddValue(MakeUnversionedSentinelValue(EValueType::Null, id));
                break;

            case ELegacyKeyPartType::MinSentinel:
                rowBuilder.AddValue(MakeUnversionedSentinelValue(EValueType::Min, id));
                break;

            case ELegacyKeyPartType::MaxSentinel:
                rowBuilder.AddValue(MakeUnversionedSentinelValue(EValueType::Max, id));
                break;

            case ELegacyKeyPartType::Int64:
                rowBuilder.AddValue(MakeUnversionedInt64Value(keyPart.int64_value(), id));
                break;

            case ELegacyKeyPartType::Double:
                rowBuilder.AddValue(MakeUnversionedDoubleValue(keyPart.double_value(), id));
                break;

            case ELegacyKeyPartType::String:
                rowBuilder.AddValue(MakeUnversionedStringValue(keyPart.str_value(), id));
                break;

            case ELegacyKeyPartType::Composite:
                rowBuilder.AddValue(MakeUnversionedAnyValue(TStringBuf(), id));
                break;

            default:
                YUNREACHABLE();
        }
    }

    *row = rowBuilder.FinishRow();
}

void Serialize(const TKey& key, IYsonConsumer* consumer)
{
    consumer->OnBeginList();
    for (int index = 0; index < key.GetCount(); ++index) {
        consumer->OnListItem();
        const auto& value = key[index];
        auto type = value.Type;
        switch (type) {
            case EValueType::Int64:
                consumer->OnInt64Scalar(value.Data.Int64);
                break;

            case EValueType::Uint64:
                consumer->OnUint64Scalar(value.Data.Uint64);
                break;

            case EValueType::Double:
                consumer->OnDoubleScalar(value.Data.Double);
                break;

            case EValueType::Boolean:
                consumer->OnBooleanScalar(value.Data.Boolean);
                break;

            case EValueType::String:
                consumer->OnStringScalar(TStringBuf(value.Data.String, value.Length));
                break;

            case EValueType::Any:
                THROW_ERROR_EXCEPTION("Key cannot contain \"any\" components");
                break;

            default:
                consumer->OnBeginAttributes();
                consumer->OnKeyedItem("type");
                consumer->OnStringScalar(FormatEnum(type));
                consumer->OnEndAttributes();
                consumer->OnEntity();
                break;
        }
    }
    consumer->OnEndList();
}

void Serialize(const TOwningKey& key, IYsonConsumer* consumer)
{
    return Serialize(key.Get(), consumer);
}

void Deserialize(TOwningKey& key, INodePtr node)
{
    if (node->GetType() != ENodeType::List) {
        THROW_ERROR_EXCEPTION("Key can only be parsed from a list");
    }

    TUnversionedOwningRowBuilder builder;
    int id = 0;
    for (const auto& item : node->AsList()->GetChildren()) {
        switch (item->GetType()) {
            case ENodeType::Int64:
                builder.AddValue(MakeUnversionedInt64Value(item->GetValue<i64>(), id));
                break;

            case ENodeType::Uint64:
                builder.AddValue(MakeUnversionedUint64Value(item->GetValue<ui64>(), id));
                break;

            case ENodeType::Double:
                builder.AddValue(MakeUnversionedDoubleValue(item->GetValue<double>(), id));
                break;

            case ENodeType::Boolean:
                builder.AddValue(MakeUnversionedBooleanValue(item->GetValue<bool>(), id));
                break;

            case ENodeType::String:
                builder.AddValue(MakeUnversionedStringValue(item->GetValue<Stroka>(), id));
                break;

            case ENodeType::Entity: {
                auto valueType = item->Attributes().Get<EValueType>("type");
                builder.AddValue(MakeUnversionedSentinelValue(valueType, id));
                break;
            }

            default:
                THROW_ERROR_EXCEPTION("Key cannot contain %Qlv components",
                    item->GetType());
        }
        ++id;
    }
    key = builder.FinishRow();
}

void TUnversionedOwningRow::Save(TStreamSaveContext& context) const
{
    NYT::Save(context, SerializeToString(Get()));
}

void TUnversionedOwningRow::Load(TStreamLoadContext& context)
{
    Stroka data;
    NYT::Load(context, data);
    *this = DeserializeFromString(data);
}

////////////////////////////////////////////////////////////////////////////////

TUnversionedRowBuilder::TUnversionedRowBuilder(int initialValueCapacity /*= 16*/)
{
    RowData_.resize(GetUnversionedRowByteSize(initialValueCapacity));
    Reset();
    GetHeader()->Capacity = initialValueCapacity;
}

int TUnversionedRowBuilder::AddValue(const TUnversionedValue& value)
{
    auto* header = GetHeader();
    if (header->Count == header->Capacity) {
        auto valueCapacity = 2 * std::max(1U, header->Capacity);
        RowData_.resize(GetUnversionedRowByteSize(valueCapacity));
        header = GetHeader();
        header->Capacity = valueCapacity;
    }

    *GetValue(header->Count) = value;
    return header->Count++;
}

TMutableUnversionedRow TUnversionedRowBuilder::GetRow()
{
    return TMutableUnversionedRow(GetHeader());
}

void TUnversionedRowBuilder::Reset()
{
    auto* header = GetHeader();
    header->Count = 0;
}

TUnversionedRowHeader* TUnversionedRowBuilder::GetHeader()
{
    return reinterpret_cast<TUnversionedRowHeader*>(RowData_.data());
}

TUnversionedValue* TUnversionedRowBuilder::GetValue(int index)
{
    return reinterpret_cast<TUnversionedValue*>(GetHeader() + 1) + index;
}

////////////////////////////////////////////////////////////////////////////////

TUnversionedOwningRowBuilder::TUnversionedOwningRowBuilder(int initialValueCapacity /*= 16*/)
    : InitialValueCapacity_(initialValueCapacity)
    , RowData_(TOwningRowTag())
{
    Reset();
}

int TUnversionedOwningRowBuilder::AddValue(const TUnversionedValue& value)
{
    auto* header = GetHeader();
    if (header->Count == header->Capacity) {
        auto valueCapacity = 2 * std::max(1U, header->Capacity);
        RowData_.Resize(GetUnversionedRowByteSize(valueCapacity));
        header = GetHeader();
        header->Capacity = valueCapacity;
    }

    auto* newValue = GetValue(header->Count);
    *newValue = value;

    if (value.Type == EValueType::String || value.Type == EValueType::Any) {
        if (StringData_.length() + value.Length > StringData_.capacity()) {
            char* oldStringData = const_cast<char*>(StringData_.begin());
            StringData_.reserve(std::max(
                StringData_.capacity() * 2,
                StringData_.length() + value.Length));
            char* newStringData = const_cast<char*>(StringData_.begin());
            for (int index = 0; index < header->Count; ++index) {
                auto* existingValue = GetValue(index);
                if (existingValue->Type == EValueType::String || existingValue->Type == EValueType::Any) {
                    existingValue->Data.String = newStringData + (existingValue->Data.String - oldStringData);
                }
            }
        }
        newValue->Data.String = const_cast<char*>(StringData_.end());
        StringData_.append(value.Data.String, value.Data.String + value.Length);
    }

    return header->Count++;
}

TUnversionedValue* TUnversionedOwningRowBuilder::BeginValues()
{
    return reinterpret_cast<TUnversionedValue*>(GetHeader() + 1);
}

TUnversionedValue* TUnversionedOwningRowBuilder::EndValues()
{
    return BeginValues() + GetHeader()->Count;
}

TUnversionedOwningRow TUnversionedOwningRowBuilder::FinishRow()
{
    auto row = TUnversionedOwningRow(
        TSharedMutableRef::FromBlob(std::move(RowData_)),
        std::move(StringData_));
    Reset();
    return row;
}

void TUnversionedOwningRowBuilder::Reset()
{
    RowData_.Resize(GetUnversionedRowByteSize(InitialValueCapacity_));

    auto* header = GetHeader();
    header->Count = 0;
    header->Capacity = InitialValueCapacity_;
}

TUnversionedRowHeader* TUnversionedOwningRowBuilder::GetHeader()
{
    return reinterpret_cast<TUnversionedRowHeader*>(RowData_.Begin());
}

TUnversionedValue* TUnversionedOwningRowBuilder::GetValue(int index)
{
    return reinterpret_cast<TUnversionedValue*>(GetHeader() + 1) + index;
}

////////////////////////////////////////////////////////////////////////////////

void TUnversionedOwningRow::Init(const TUnversionedValue* begin, const TUnversionedValue* end)
{
    int count = std::distance(begin, end);

    size_t fixedSize = GetUnversionedRowByteSize(count);
    RowData_ = TSharedMutableRef::Allocate<TOwningRowTag>(fixedSize, false);
    auto* header = GetHeader();

    header->Count = count;
    header->Capacity = count;
    ::memcpy(header + 1, begin, reinterpret_cast<const char*>(end) - reinterpret_cast<const char*>(begin));

    size_t variableSize = 0;
    for (auto it = begin; it != end; ++it) {
        const auto& otherValue = *it;
        if (otherValue.Type == EValueType::String || otherValue.Type == EValueType::Any) {
            variableSize += otherValue.Length;
        }
    }

    if (variableSize > 0) {
        StringData_.resize(variableSize);
        char* current = const_cast<char*>(StringData_.data());

        for (int index = 0; index < count; ++index) {
            const auto& otherValue = begin[index];
            auto& value = reinterpret_cast<TUnversionedValue*>(header + 1)[index];;
            if (otherValue.Type == EValueType::String || otherValue.Type == EValueType::Any) {
                ::memcpy(current, otherValue.Data.String, otherValue.Length);
                value.Data.String = current;
                current += otherValue.Length;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

TOwningKey WidenKey(const TOwningKey& key, int keyColumnCount)
{
    YCHECK(keyColumnCount >= key.GetCount());

    if (key.GetCount() == keyColumnCount) {
        return key;
    }

    TUnversionedOwningRowBuilder builder;
    for (const auto* value = key.Begin(); value != key.End(); ++value) {
        builder.AddValue(*value);
    }

    for (int i = key.GetCount(); i < keyColumnCount; ++i) {
        builder.AddValue(MakeUnversionedSentinelValue(EValueType::Null));
    }

    return builder.FinishRow();
}

////////////////////////////////////////////////////////////////////////////////

TUnversionedOwningRow BuildRow(
    const Stroka& yson,
    const TKeyColumns& keyColumns,
    const TTableSchema& tableSchema,
    bool treatMissingAsNull /*= true*/)
{
    auto nameTable = TNameTable::FromSchema(tableSchema);

    auto rowParts = ConvertTo<yhash_map<Stroka, INodePtr>>(
        TYsonString(yson, EYsonType::MapFragment));

    TUnversionedOwningRowBuilder rowBuilder;
    auto addValue = [&] (int id, INodePtr value) {
        switch (value->GetType()) {
            case ENodeType::Int64:
                rowBuilder.AddValue(MakeUnversionedInt64Value(value->GetValue<i64>(), id));
                break;
            case ENodeType::Uint64:
                rowBuilder.AddValue(MakeUnversionedUint64Value(value->GetValue<ui64>(), id));
                break;
            case ENodeType::Double:
                rowBuilder.AddValue(MakeUnversionedDoubleValue(value->GetValue<double>(), id));
                break;
            case ENodeType::Boolean:
                rowBuilder.AddValue(MakeUnversionedBooleanValue(value->GetValue<bool>(), id));
                break;
            case ENodeType::String:
                rowBuilder.AddValue(MakeUnversionedStringValue(value->GetValue<Stroka>(), id));
                break;
            default:
                rowBuilder.AddValue(MakeUnversionedAnyValue(ConvertToYsonString(value).Data(), id));
                break;
        }
    };

    // Key
    for (int id = 0; id < static_cast<int>(keyColumns.size()); ++id) {
        auto it = rowParts.find(nameTable->GetName(id));
        if (it == rowParts.end()) {
            rowBuilder.AddValue(MakeUnversionedSentinelValue(EValueType::Null, id));
        } else {
            addValue(id, it->second);
        }
    }

    // Fixed values
    for (int id = static_cast<int>(keyColumns.size()); id < static_cast<int>(tableSchema.Columns().size()); ++id) {
        auto it = rowParts.find(nameTable->GetName(id));
        if (it != rowParts.end()) {
            addValue(id, it->second);
        } else if (treatMissingAsNull) {
            rowBuilder.AddValue(MakeUnversionedSentinelValue(EValueType::Null, id));
        }
    }

    // Variable values
    for (const auto& pair : rowParts) {
        int id = nameTable->GetIdOrRegisterName(pair.first);
        if (id >= tableSchema.Columns().size()) {
            addValue(id, pair.second);
        }
    }

    return rowBuilder.FinishRow();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT

