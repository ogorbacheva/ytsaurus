#pragma once

#include "public.h"
#include "row_base.h"
#include "schema.h"
#include "unversioned_value.h"

#include <yt/ytlib/chunk_client/schema.pb.h>

#include <yt/core/misc/chunked_memory_pool.h>
#include <yt/core/misc/serialize.h>
#include <yt/core/misc/small_vector.h>
#include <yt/core/misc/string.h>
#include <yt/core/misc/varint.h>

#include <yt/core/yson/public.h>

#include <yt/core/ytree/public.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TUnversionedOwningValue
{
public:
    TUnversionedOwningValue() = default;

    TUnversionedOwningValue(TUnversionedOwningValue&& other)
    {
        std::swap(Value_, other.Value_);
    }

    TUnversionedOwningValue(const TUnversionedOwningValue& other)
    {
        Assign(other.Value_);
    }

    TUnversionedOwningValue(const TUnversionedValue& other)
    {
        Assign(other);
    }

    ~TUnversionedOwningValue()
    {
        Clear();
    }

    operator TUnversionedValue() const
    {
        return Value_;
    }

    TUnversionedOwningValue& operator = (TUnversionedOwningValue&& other)
    {
        std::swap(Value_, other.Value_);
        return *this;
    }

    TUnversionedOwningValue& operator = (const TUnversionedOwningValue& other)
    {
        Clear();
        Assign(other.Value_);
        return *this;
    }

    TUnversionedOwningValue& operator = (const TUnversionedValue& other)
    {
        Clear();
        Assign(other);
        return *this;
    }

    void Clear()
    {
        if (Value_.Type == EValueType::Any || Value_.Type == EValueType::String) {
            delete [] Value_.Data.String;
        }
        Value_.Type = EValueType::TheBottom;
        Value_.Length = 0;
    }

private:
    TUnversionedValue Value_ = {0, EValueType::TheBottom, 0, {0}};

    void Assign(const TUnversionedValue& other)
    {
        Value_ = other;
        if (Value_.Type == EValueType::Any || Value_.Type == EValueType::String) {
            auto newString = new char[Value_.Length];
            ::memcpy(newString, Value_.Data.String, Value_.Length);
            Value_.Data.String = newString;
        }
    }

};

static_assert(
    EValueType::Int64 < EValueType::Uint64 &&
    EValueType::Uint64 < EValueType::Double,
    "Incorrect type order.");

////////////////////////////////////////////////////////////////////////////////

inline bool IsIntegralType(EValueType type)
{
    return type == EValueType::Int64 || type == EValueType::Uint64;
}

inline bool IsArithmeticType(EValueType type)
{
    return IsIntegralType(type) || type == EValueType::Double;
}

inline bool IsStringLikeType(EValueType type)
{
    return type == EValueType::String || type == EValueType::Any;
}

inline bool IsComparableType(EValueType type)
{
    return IsArithmeticType(type) || type == EValueType::String || type == EValueType::Boolean;;
}

inline bool IsSentinelType(EValueType type)
{
    return type == EValueType::Min || type == EValueType::Max;
}

////////////////////////////////////////////////////////////////////////////////

inline TUnversionedValue MakeUnversionedSentinelValue(EValueType type, int id = 0)
{
    return MakeSentinelValue<TUnversionedValue>(type, id);
}

inline TUnversionedValue MakeUnversionedInt64Value(i64 value, int id = 0)
{
    return MakeInt64Value<TUnversionedValue>(value, id);
}

inline TUnversionedValue MakeUnversionedUint64Value(ui64 value, int id = 0)
{
    return MakeUint64Value<TUnversionedValue>(value, id);
}

inline TUnversionedValue MakeUnversionedDoubleValue(double value, int id = 0)
{
    return MakeDoubleValue<TUnversionedValue>(value, id);
}

inline TUnversionedValue MakeUnversionedBooleanValue(bool value, int id = 0)
{
    return MakeBooleanValue<TUnversionedValue>(value, id);
}

inline TUnversionedValue MakeUnversionedStringValue(const TStringBuf& value, int id = 0)
{
    return MakeStringValue<TUnversionedValue>(value, id);
}

inline TUnversionedValue MakeUnversionedAnyValue(const TStringBuf& value, int id = 0)
{
    return MakeAnyValue<TUnversionedValue>(value, id);
}

////////////////////////////////////////////////////////////////////////////////

struct TUnversionedRowHeader
{
    ui32 Count;
    ui32 Capacity;
};

static_assert(
    sizeof(TUnversionedRowHeader) == 8,
    "TUnversionedRowHeader has to be exactly 8 bytes.");

////////////////////////////////////////////////////////////////////////////////

int GetByteSize(const TUnversionedValue& value);
int GetDataWeight(const TUnversionedValue& value);
int WriteValue(char* output, const TUnversionedValue& value);
int ReadValue(const char* input, TUnversionedValue* value);

////////////////////////////////////////////////////////////////////////////////

void Save(TStreamSaveContext& context, const TUnversionedValue& value);
void Load(TStreamLoadContext& context, TUnversionedValue& value, TChunkedMemoryPool* pool);

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(const TUnversionedValue& value);

//! Ternary comparison predicate for TUnversionedValue-s.
//! Returns zero, positive or negative value depending on the outcome.
int CompareRowValues(const TUnversionedValue& lhs, const TUnversionedValue& rhs);

bool operator == (const TUnversionedValue& lhs, const TUnversionedValue& rhs);
bool operator != (const TUnversionedValue& lhs, const TUnversionedValue& rhs);
bool operator <= (const TUnversionedValue& lhs, const TUnversionedValue& rhs);
bool operator <  (const TUnversionedValue& lhs, const TUnversionedValue& rhs);
bool operator >= (const TUnversionedValue& lhs, const TUnversionedValue& rhs);
bool operator >  (const TUnversionedValue& lhs, const TUnversionedValue& rhs);

//! Ternary comparison predicate for ranges of TUnversionedValue-s.
int CompareRows(
    const TUnversionedValue* lhsBegin,
    const TUnversionedValue* lhsEnd,
    const TUnversionedValue* rhsBegin,
    const TUnversionedValue* rhsEnd);

//! Ternary comparison predicate for TUnversionedRow-s stripped to a given number of
//! (leading) values.
int CompareRows(
    TUnversionedRow lhs,
    TUnversionedRow rhs,
    int prefixLength = std::numeric_limits<int>::max());

bool operator == (TUnversionedRow lhs, TUnversionedRow rhs);
bool operator != (TUnversionedRow lhs, TUnversionedRow rhs);
bool operator <= (TUnversionedRow lhs, TUnversionedRow rhs);
bool operator <  (TUnversionedRow lhs, TUnversionedRow rhs);
bool operator >= (TUnversionedRow lhs, TUnversionedRow rhs);
bool operator >  (TUnversionedRow lhs, TUnversionedRow rhs);

bool operator == (TUnversionedRow lhs, const TUnversionedOwningRow& rhs);
bool operator != (TUnversionedRow lhs, const TUnversionedOwningRow& rhs);
bool operator <= (TUnversionedRow lhs, const TUnversionedOwningRow& rhs);
bool operator <  (TUnversionedRow lhs, const TUnversionedOwningRow& rhs);
bool operator >= (TUnversionedRow lhs, const TUnversionedOwningRow& rhs);
bool operator >  (TUnversionedRow lhs, const TUnversionedOwningRow& rhs);

bool operator == (const TUnversionedOwningRow& lhs, TUnversionedRow rhs);
bool operator != (const TUnversionedOwningRow& lhs, TUnversionedRow rhs);
bool operator <= (const TUnversionedOwningRow& lhs, TUnversionedRow rhs);
bool operator <  (const TUnversionedOwningRow& lhs, TUnversionedRow rhs);
bool operator >= (const TUnversionedOwningRow& lhs, TUnversionedRow rhs);
bool operator >  (const TUnversionedOwningRow& lhs, TUnversionedRow rhs);

//! Ternary comparison predicate for TUnversionedOwningRow-s stripped to a given number of
//! (leading) values.
int CompareRows(
    const TUnversionedOwningRow& lhs,
    const TUnversionedOwningRow& rhs,
    int prefixLength = std::numeric_limits<int>::max());

bool operator == (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs);
bool operator != (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs);
bool operator <= (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs);
bool operator <  (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs);
bool operator >= (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs);
bool operator >  (const TUnversionedOwningRow& lhs, const TUnversionedOwningRow& rhs);

//! Sets all value types of |row| to |EValueType::Null|. Ids are not changed.
void ResetRowValues(TUnversionedRow* row);

//! Computes hash for a given TUnversionedRow.
ui64 GetHash(TUnversionedRow row, int keyColumnCount = std::numeric_limits<int>::max());

//! Computes FarmHash forever-fixed fingerprint for a given TUnversionedRow.
TFingerprint GetFarmFingerprint(TUnversionedRow row, int keyColumnCount = std::numeric_limits<int>::max());

//! Returns the number of bytes needed to store the fixed part of the row (header + values).
size_t GetUnversionedRowDataSize(int valueCount);

//! Returns the storage-invariant data weight of a given row.
i64 GetDataWeight(TUnversionedRow row);

////////////////////////////////////////////////////////////////////////////////

//! A row with unversioned data.
/*!
 *  A lightweight wrapper around |TUnversionedRowHeader*|.
 *
 *  Provides access to a sequence of unversioned values.
 *  If data is schemaful then the positions of values must exactly match their ids.
 *
 *  Memory layout:
 *  1) TUnversionedRowHeader
 *  2) TUnversionedValue per each value (#TUnversionedRowHeader::ValueCount)
 */
class TUnversionedRow
{
public:
    TUnversionedRow()
        : Header(nullptr)
    { }

    explicit TUnversionedRow(TUnversionedRowHeader* header)
        : Header(header)
    { }

    static TUnversionedRow Allocate(
        TChunkedMemoryPool* pool,
        int valueCount);


    explicit operator bool() const
    {
        return Header != nullptr;
    }


    const TUnversionedRowHeader* GetHeader() const
    {
        return Header;
    }

    TUnversionedRowHeader* GetHeader()
    {
        return Header;
    }


    const TUnversionedValue* Begin() const
    {
        return reinterpret_cast<const TUnversionedValue*>(Header + 1);
    }

    TUnversionedValue* Begin()
    {
        return reinterpret_cast<TUnversionedValue*>(Header + 1);
    }


    const TUnversionedValue* End() const
    {
        return Begin() + GetCount();
    }

    TUnversionedValue* End()
    {
        return Begin() + GetCount();
    }


    int GetCount() const
    {
        return Header->Count;
    }

    void SetCount(int count)
    {
        YASSERT(count >= 0 && count <= Header->Capacity);
        Header->Count = count;
    }


    const TUnversionedValue& operator[] (int index) const
    {
        YASSERT(index >= 0 && index < GetCount());
        return Begin()[index];
    }

    TUnversionedValue& operator[] (int index)
    {
        YASSERT(index >= 0 && index < GetCount());
        return Begin()[index];
    }

private:
    TUnversionedRowHeader* Header;

};

// For TKeyComparer.
inline int GetKeyComparerValueCount(TUnversionedRow row, int prefixLength)
{
    return std::min(row.GetCount(), prefixLength);
}

static_assert(
    sizeof(TUnversionedRow) == sizeof(intptr_t),
    "TUnversionedRow size must match that of a pointer.");

////////////////////////////////////////////////////////////////////////////////

//! Checks that #value type is compatible with the schema column type.
void ValidateValueType(
    const TUnversionedValue& value,
    const TTableSchema& schema,
    int schemaId);

//! Checks that #value is allowed to appear in static tables' data. Throws on failure.
void ValidateStaticValue(const TUnversionedValue& value);

//! Checks that #value is allowed to appear in dynamic tables' data. Throws on failure.
void ValidateDataValue(const TUnversionedValue& value);

//! Checks that #value is allowed to appear in dynamic tables' keys. Throws on failure.
void ValidateKeyValue(const TUnversionedValue& value);

//! Checks that #count represents an allowed number of values in a row. Throws on failure.
void ValidateRowValueCount(int count);

//! Checks that #count represents an allowed number of components in a key. Throws on failure.
void ValidateKeyColumnCount(int count);

//! Checks that #count represents an allowed number of rows in a rowset. Throws on failure.
void ValidateRowCount(int count);

//! Checks that #row is a valid client-side data row. Throws on failure.
/*! The row must obey the following properties:
 *  1. Its value count must pass #ValidateRowValueCount checks.
 *  2. It must contain all key components (values with ids in range [0, keyColumnCount - 1]).
 *  3. Value types must either be null or match those given in schema.
 */
void ValidateClientDataRow(
    TUnversionedRow row,
    int keyColumnCount,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMappingPtr);

//! Checks that #row is a valid server-side data row. Throws on failure.
/*! The row must obey the following properties:
 *  1. Its value count must pass #ValidateRowValueCount checks.
 *  2. It must contain all key components (values with ids in range [0, keyColumnCount - 1])
 *  in this order at the very beginning.
 *  3. Value types must either be null or match those given in schema.
 */
void ValidateServerDataRow(
    TUnversionedRow row,
    int keyColumnCount,
    const TTableSchema& schema);

//! Checks that #key is a valid client-side key. Throws on failure.
/*! The key must obey the following properties:
 *  1. It cannot be null.
 *  2. It must contain exactly #keyColumnCount components.
 *  3. Value ids must be a permutation of {0, ..., keyColumnCount - 1}.
 *  4. Value types must either be null of match those given in schema.
 */
void ValidateClientKey(
    TKey key,
    int keyColumnCount,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMappingPtr);

//! Checks that #key is a valid server-side key. Throws on failure.
/*! The key must obey the following properties:
 *  1. It cannot be null.
 *  2. It must contain exactly #keyColumnCount components with ids
 *  0, ..., keyColumnCount - 1 in this order.
 */
void ValidateServerKey(
    TKey key,
    int keyColumnCount,
    const TTableSchema& schema);

//! Checks if #timestamp is sane and can be used for reading data.
void ValidateReadTimestamp(TTimestamp timestamp);

//! Returns the successor of |key|, i.e. the key obtained from |key|
//! by appending a |EValueType::Min| sentinel.
TOwningKey GetKeySuccessor(TKey key);

//! Returns the successor of |key| trimmed to a given length, i.e. the key
//! obtained by trimming |key| to |prefixLength| and appending
//! a |EValueType::Max| sentinel.
TOwningKey GetKeyPrefixSuccessor(TKey key, int prefixLength);

TOwningKey GetKeyPrefix(TKey key, int prefixLength);

//! Makes a new, wider key padded with null values.
TOwningKey WidenKey(const TOwningKey& key, int keyColumnCount);

//! Returns the key with no components.
const TOwningKey EmptyKey();

//! Returns the key with a single |Min| component.
const TOwningKey MinKey();

//! Returns the key with a single |Max| component.
const TOwningKey MaxKey();

//! Compares two keys, |a| and |b|, and returns a smaller one.
//! Ties are broken in favour of the first argument.
const TOwningKey& ChooseMinKey(const TOwningKey& a, const TOwningKey& b);

//! Compares two keys, |a| and |b|, and returns a bigger one.
//! Ties are broken in favour of the first argument.
const TOwningKey& ChooseMaxKey(const TOwningKey& a, const TOwningKey& b);

Stroka SerializeToString(const TUnversionedValue* begin, const TUnversionedValue* end);

void ToProto(TProtoStringType* protoRow, TUnversionedRow row);
void ToProto(TProtoStringType* protoRow, const TUnversionedOwningRow& row);
void ToProto(TProtoStringType* protoRow, const TUnversionedValue* begin, const TUnversionedValue* end);

void FromProto(TUnversionedOwningRow* row, const TProtoStringType& protoRow);
void FromProto(TUnversionedOwningRow* row, const NChunkClient::NProto::TKey& protoKey);
void FromProto(TUnversionedRow* row, const TProtoStringType& protoRow, const TRowBufferPtr& rowBuffer);

void Serialize(const TKey& key, NYson::IYsonConsumer* consumer);
void Serialize(const TOwningKey& key, NYson::IYsonConsumer* consumer);

void Deserialize(TOwningKey& key, NYTree::INodePtr node);

Stroka ToString(TUnversionedRow row);
Stroka ToString(const TUnversionedOwningRow& row);

////////////////////////////////////////////////////////////////////////////////

//! An immutable owning version of TUnversionedRow.
/*!
 *  Instances of TUnversionedOwningRow are lightweight ref-counted handles.
 *  Fixed part is stored in a (shared) blob.
 *  Variable part is stored in a (shared) string.
 */
class TUnversionedOwningRow
{
public:
    TUnversionedOwningRow()
    { }

    TUnversionedOwningRow(const TUnversionedValue* begin, const TUnversionedValue* end)
    {
        Init(begin, end);
    }

    explicit TUnversionedOwningRow(TUnversionedRow other)
    {
        if (!other)
            return;

        Init(other.Begin(), other.End());
    }

    TUnversionedOwningRow(const TUnversionedOwningRow& other)
        : RowData_(other.RowData_)
        , StringData_(other.StringData_)
    { }

    TUnversionedOwningRow(TUnversionedOwningRow&& other)
        : RowData_(std::move(other.RowData_))
        , StringData_(std::move(other.StringData_))
    { }

    explicit operator bool() const
    {
        return static_cast<bool>(RowData_);
    }

    TUnversionedRow Get() const
    {
        return TUnversionedRow(const_cast<TUnversionedOwningRow*>(this)->GetHeader());
    }

    const TUnversionedRowHeader* GetHeader() const
    {
        return RowData_ ? reinterpret_cast<const TUnversionedRowHeader*>(RowData_.Begin()) : nullptr;
    }

    const TUnversionedValue* Begin() const
    {
        const auto* header = GetHeader();
        return header ? reinterpret_cast<const TUnversionedValue*>(header + 1) : nullptr;
    }

    const TUnversionedValue* End() const
    {
        return Begin() + GetCount();
    }

    int GetCount() const
    {
        const auto* header = GetHeader();
        return header ? static_cast<int>(header->Count) : 0;
    }

    const TUnversionedValue& operator[] (int index) const
    {
        YASSERT(index >= 0 && index < GetCount());
        return Begin()[index];
    }


    friend void swap(TUnversionedOwningRow& lhs, TUnversionedOwningRow& rhs)
    {
        using std::swap;

        swap(lhs.RowData_, rhs.RowData_);
        swap(lhs.StringData_, rhs.StringData_);
    }

    TUnversionedOwningRow& operator=(const TUnversionedOwningRow& other)
    {
        RowData_ = other.RowData_;
        StringData_ = other.StringData_;
        return *this;
    }

    TUnversionedOwningRow& operator=(TUnversionedOwningRow&& other)
    {
        RowData_ = std::move(other.RowData_);
        StringData_ = std::move(other.StringData_);
        return *this;
    }

    int GetSize() const
    {
        return StringData_.length() + RowData_.Size();
    }

    size_t SpaceUsed() const
    {
        return sizeof(*this) + StringData_.capacity() + RowData_.Size();
    }

    void Save(TStreamSaveContext& context) const;
    void Load(TStreamLoadContext& context);

private:
    friend void FromProto(TUnversionedOwningRow* row, const NChunkClient::NProto::TKey& protoKey);
    friend TOwningKey GetKeySuccessorImpl(const TOwningKey& key, int prefixLength, EValueType sentinelType);
    friend TUnversionedOwningRow DeserializeFromString(const Stroka& data);

    friend class TUnversionedOwningRowBuilder;

    TSharedMutableRef RowData_; // TRowHeader plus TValue-s
    Stroka StringData_;         // Holds string data

    TUnversionedOwningRow(TSharedMutableRef rowData, Stroka stringData)
        : RowData_(std::move(rowData))
        , StringData_(std::move(stringData))
    { }

    TUnversionedRowHeader* GetHeader()
    {
        return RowData_ ? reinterpret_cast<TUnversionedRowHeader*>(RowData_.Begin()) : nullptr;
    }

    void Init(const TUnversionedValue* begin, const TUnversionedValue* end);

};

// For TKeyComparer.
inline int GetKeyComparerValueCount(const TUnversionedOwningRow& row, int prefixLength)
{
    return std::min(row.GetCount(), prefixLength);
}

////////////////////////////////////////////////////////////////////////////////

//! A helper used for constructing TUnversionedRow instances.
//! Only row values are kept, strings are only referenced.
class TUnversionedRowBuilder
{
public:
    static const int DefaultValueCapacity = 16;

    explicit TUnversionedRowBuilder(int initialValueCapacity = DefaultValueCapacity);

    int AddValue(const TUnversionedValue& value);
    TUnversionedRow GetRow();
    void Reset();

private:
    static const int DefaultBlobCapacity =
        sizeof(TUnversionedRowHeader) +
        DefaultValueCapacity * sizeof(TUnversionedValue);

    SmallVector<char, DefaultBlobCapacity> RowData_;

    TUnversionedRowHeader* GetHeader();
    TUnversionedValue* GetValue(int index);

};

////////////////////////////////////////////////////////////////////////////////

//! A helper used for constructing TUnversionedOwningRow instances.
//! Keeps both row values and strings.
class TUnversionedOwningRowBuilder
{
public:
    static const int DefaultValueCapacity = 16;

    explicit TUnversionedOwningRowBuilder(int initialValueCapacity = DefaultValueCapacity);

    int AddValue(const TUnversionedValue& value);
    TUnversionedValue* BeginValues();
    TUnversionedValue* EndValues();

    TUnversionedOwningRow FinishRow();

private:
    int InitialValueCapacity_;

    TBlob RowData_;
    Stroka StringData_;

    TUnversionedRowHeader* GetHeader();
    TUnversionedValue* GetValue(int index);
    void Reset();

};

////////////////////////////////////////////////////////////////////////////////

TUnversionedOwningRow BuildRow(
    const Stroka& yson,
    const TKeyColumns& keyColumns,
    const TTableSchema& tableSchema,
    bool treatMissingAsNull = true);

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT

//! A hasher for TUnversionedValue.
template <>
struct hash<NYT::NTableClient::TUnversionedValue>
{
    inline size_t operator()(const NYT::NTableClient::TUnversionedValue& value) const
    {
        return GetHash(value);
    }
};
