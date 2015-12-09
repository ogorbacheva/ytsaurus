#pragma once

#include "public.h"

#include <yt/ytlib/table_client/unversioned_row.h>

#include <yt/core/misc/chunked_memory_pool.h>
#include <yt/core/misc/enum.h>

#include <atomic>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

// NB: 4-aligned.
struct TDynamicString
{
    ui32 Length;
    char Data[1]; // the actual length is above
};

// NB: TDynamicValueData must be binary compatible with TUnversionedValueData for all simple types.
union TDynamicValueData
{
    //! |Int64| value.
    i64 Int64;
    //! |Uint64| value.
    ui64 Uint64;
    //! |Double| value.
    double Double;
    //! |Boolean| value.
    bool Boolean;
    //! String value for |String| type or YSON-encoded value for |Any| type.
    TDynamicString* String;
};

static_assert(
    sizeof(TDynamicValueData) == sizeof(NTableClient::TUnversionedValueData),
    "TDynamicValueData and TUnversionedValueData must be of the same size.");

struct TDynamicValue
{
    TDynamicValueData Data;
    ui32 Revision;
    bool Null;
    char Padding[3];
};

static_assert(
    sizeof(TDynamicValue) == 16,
    "Wrong TDynamicValue size.");

struct TLockDescriptor
{
    TTransaction* Transaction;
    TTimestamp PrepareTimestamp;
    TEditListHeader* WriteRevisionList;
};

struct TDynamicRowHeader
{
    ui32 NullKeyMask;
    ui32 DeleteLockFlag : 1;
    ui32 Padding : 31;
};

struct TEditListHeader
{
    //! Pointer to the successor list with smaller timestamps.
    std::atomic<TEditListHeader*> Successor;

    //! Number of committed slots in the list.
    //! Only updated _after_ the slot is written to.
    std::atomic<ui16> Size;

    //! Number of uncommitted slots in the list (following the committed ones).
    //! Either 0 or 1.
    ui16 UncommittedSize;

    //! Sum of (committed) sizes of all successors.
    ui16 SuccessorsSize;

    //! Number of slots in the list.
    ui16 Capacity;

    // Variable-size part:
    // * |Capacity| slots, with increasing timestamps.
};

static_assert(
    sizeof(std::atomic<TEditListHeader*>) == sizeof(intptr_t),
    "std::atomic<TEditListHeader*> does not seem to be lock-free.");

static_assert(
    sizeof(std::atomic<ui16>) == sizeof(ui16),
    "std::atomic<ui16> does not seem to be lock-free.");

////////////////////////////////////////////////////////////////////////////////

//! A lightweight wrapper around TEditListHeader*.
template <class T>
class TEditList
{
public:
    TEditList()
        : Header_(nullptr)
    { }

    explicit TEditList(TEditListHeader* header)
        : Header_(header)
    { }

    static TEditList Allocate(
        TChunkedMemoryPool* pool,
        int capacity)
    {
        auto* header = reinterpret_cast<TEditListHeader*>(pool->AllocateAligned(
            sizeof(TEditListHeader) +
            capacity * sizeof(T)));
        ::memset(header, 0, sizeof(TEditListHeader));
        header->Capacity = capacity;
        return TEditList(header);
    }


    explicit operator bool() const
    {
        return Header_ != nullptr;
    }

    bool operator == (TEditList<T> other) const
    {
        return Header_ == other.Header_;
    }

    bool operator != (TEditList<T> other) const
    {
        return !(*this == other);
    }


    TEditList GetSuccessor() const
    {
        return TEditList(Header_->Successor);
    }

    void SetSuccessor(TEditList successor)
    {
        YASSERT(!HasUncommitted());
        YASSERT(successor && !successor.HasUncommitted());
        Header_->Successor = successor.Header_;
        Header_->SuccessorsSize = successor.GetFullSize();
    }


    int GetSize() const
    {
        return Header_->Size;
    }

    bool IsEmpty() const
    {
        return GetSize() == 0;
    }

    int GetSuccessorsSize() const
    {
        return Header_->SuccessorsSize;
    }

    int GetFullSize() const
    {
        return GetSize() + GetSuccessorsSize();
    }

    int GetCapacity() const
    {
        return Header_->Capacity;
    }


    const T* Begin() const
    {
        return reinterpret_cast<T*>(Header_ + 1);
    }

    T* Begin()
    {
        return reinterpret_cast<T*>(Header_ + 1);
    }


    const T* End() const
    {
        return reinterpret_cast<T*>(Header_ + 1) + Header_->Size;
    }

    T* End()
    {
        return reinterpret_cast<T*>(Header_ + 1) + Header_->Size;
    }


    const T& Front() const
    {
        return (*this)[0];
    }

    const T& Back() const
    {
        YASSERT(!IsEmpty());
        return (*this)[GetSize() - 1];
    }


    const T& operator[] (int index) const
    {
        return Begin()[index];
    }

    T& operator[] (int index)
    {
        return Begin()[index];
    }


    const T& GetUncommitted() const
    {
        YASSERT(HasUncommitted());
        return (*this)[GetSize()];
    }

    T& GetUncommitted()
    {
        YASSERT(HasUncommitted());
        return (*this)[GetSize()];
    }


    void Push(T value)
    {
        YASSERT(Header_->Size < Header_->Capacity);
        *End() = value;
        ++Header_->Size;
    }

    void Prepare()
    {
        YASSERT(Header_->UncommittedSize == 0);
        YASSERT(Header_->Size < Header_->Capacity);
        ++Header_->UncommittedSize;
    }

    bool HasUncommitted() const
    {
        return Header_ && Header_->UncommittedSize > 0;
    }

    void Commit()
    {
        YASSERT(Header_->UncommittedSize == 1);
        Header_->UncommittedSize = 0;
        ++Header_->Size;
    }

    void Abort()
    {
        YASSERT(Header_->UncommittedSize == 1);
        Header_->UncommittedSize = 0;
    }

private:
    friend class  TDynamicRow;

    TEditListHeader* Header_;

};

static_assert(
    sizeof(TValueList) == sizeof(intptr_t),
    "TValueList size must match that of a pointer.");
static_assert(
    sizeof(TRevisionList) == sizeof(intptr_t),
    "TRevisionList size must match that of a pointer.");

////////////////////////////////////////////////////////////////////////////////

//! A row within TDynamicMemoryStore.
/*!
 *  A lightweight wrapper around |TDynamicRowHeader*|.
 *
 *  Provides access to the following parts:
 *  1) keys
 *  2) locks
 *  3) edit lists for write and delete timestamps
 *  4) edit lists for versioned values per each fixed non-key column
 *
 *  Memory layout:
 *  1) TDynamicRowHeader
 *  2) TDynamicValueData per each key column
 *  3) TLockDescriptor per each lock group
 *  4) TEditListHeader* for delete timestamps
 *  5) TEditListHeader* per each fixed non-key column
 */
class TDynamicRow
{
public:
    TDynamicRow()
        : Header_(nullptr)
    { }

    explicit TDynamicRow(TDynamicRowHeader* header)
        : Header_(header)
    { }

    static TDynamicRow Allocate(
        TChunkedMemoryPool* pool,
        int keyColumnCount,
        int columnLockCount,
        int schemaColumnCount)
    {
        // One list per each non-key schema column plus delete timestamp.
        int listCount = (schemaColumnCount - keyColumnCount) + 1;
        size_t size =
            sizeof(TDynamicRowHeader) +
            keyColumnCount * sizeof(TDynamicValueData) +
            columnLockCount * sizeof(TLockDescriptor) +
            listCount * sizeof(TEditListHeader*);

        // Allocate memory.
        auto* header = reinterpret_cast<TDynamicRowHeader*>(pool->AllocateAligned(size));
        auto row = TDynamicRow(header);

        // Generic fill.
        ::memset(header, 0, size);

        // Custom fill.
        {
            auto* lock = row.BeginLocks(keyColumnCount);
            for (int index = 0; index < columnLockCount; ++index, ++lock) {
                lock->PrepareTimestamp = NTableClient::NotPreparedTimestamp;
                lock->WriteRevisionList = nullptr;
            }
        }

        return row;
    }


    explicit operator bool() const
    {
        return Header_ != nullptr;
    }

    static const int PrimaryLockIndex = 0;
    static const ui32 PrimaryLockMask = (1 << PrimaryLockIndex);
    static const ui32 AllLocksMask = 0xffffffff;

    const TDynamicValueData* BeginKeys() const
    {
        return reinterpret_cast<const TDynamicValueData*>(Header_ + 1);
    }

    TDynamicValueData* BeginKeys()
    {
        return reinterpret_cast<TDynamicValueData*>(Header_ + 1);
    }


    ui32 GetNullKeyMask() const
    {
        return Header_->NullKeyMask;
    }

    void SetNullKeyMask(ui32 value)
    {
        Header_->NullKeyMask = value;
    }


    bool GetDeleteLockFlag() const
    {
        return Header_->DeleteLockFlag;
    }

    void SetDeleteLockFlag(bool value)
    {
        Header_->DeleteLockFlag = value;
    }


    const TLockDescriptor* BeginLocks(int keyColumnCount) const
    {
        return reinterpret_cast<const TLockDescriptor*>(BeginKeys() + keyColumnCount);
    }

    TLockDescriptor* BeginLocks(int keyColumnCount)
    {
        return reinterpret_cast<TLockDescriptor*>(BeginKeys() + keyColumnCount);
    }


    TValueList GetFixedValueList(int columnIndex, int keyColumnCount, int columnLockCount) const
    {
        YASSERT(columnIndex >= keyColumnCount);
        return TValueList(GetLists(keyColumnCount, columnLockCount)[columnIndex - keyColumnCount + 1]);
    }

    void SetFixedValueList(int columnIndex, TValueList list, int keyColumnCount, int columnLockCount)
    {
        YASSERT(columnIndex >= keyColumnCount);
        GetLists(keyColumnCount, columnLockCount)[columnIndex - keyColumnCount + 1] = list.Header_;
    }


    TRevisionList GetDeleteRevisionList(int keyColumnCount, int columnLockCount) const
    {
        return TRevisionList(GetLists(keyColumnCount, columnLockCount)[0]);
    }

    void SetDeleteRevisionList(TRevisionList list, int keyColumnCount, int columnLockCount)
    {
        GetLists(keyColumnCount, columnLockCount)[0] = list.Header_;
    }


    static TRevisionList GetWriteRevisionList(TLockDescriptor& lock)
    {
        return TRevisionList(lock.WriteRevisionList);
    }

    static void SetWriteRevisionList(TLockDescriptor& lock, TRevisionList list)
    {
        lock.WriteRevisionList = list.Header_;
    }


    bool operator == (TDynamicRow other) const
    {
        return Header_ == other.Header_;
    }

    bool operator != (TDynamicRow other) const
    {
        return Header_ != other.Header_;
    }

private:
    TDynamicRowHeader* Header_;


    TEditListHeader** GetLists(int keyColumnCount, int columnLockCount) const
    {
        return reinterpret_cast<TEditListHeader**>(const_cast<TLockDescriptor*>(BeginLocks(keyColumnCount)) + columnLockCount);
    }
};

static_assert(
    sizeof(TDynamicRow) == sizeof(intptr_t),
    "TDynamicRow size must match that of a pointer.");

////////////////////////////////////////////////////////////////////////////////

struct TDynamicRowRef
{
    TDynamicRowRef()
        : Store(nullptr)
        , Row()
    { }

    TDynamicRowRef(const TDynamicRowRef& other) = default;
    
    TDynamicRowRef(TDynamicMemoryStore* store, TDynamicRow row)
        : Store(store)
        , Row(row)
    { }


    explicit operator bool() const
    {
        return Store != nullptr;
    }


    bool operator == (const TDynamicRowRef& other) const
    {
        return
            Store == other.Store &&
            Row == other.Row;
    }

    bool operator != (const TDynamicRowRef& other) const
    {
        return !(*this == other);
    }


    TDynamicMemoryStore* Store;
    TDynamicRow Row;
};

////////////////////////////////////////////////////////////////////////////////

TOwningKey RowToKey(
    const NTableClient::TTableSchema& schema,
    const NTableClient::TKeyColumns& keyColumns,
    TDynamicRow row);

TOwningKey RowToKey(
    const NTableClient::TTableSchema& schema,
    const NTableClient::TKeyColumns& keyColumns,
    TUnversionedRow row);

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
