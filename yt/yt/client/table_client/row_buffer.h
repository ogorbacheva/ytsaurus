#pragma once

#include "public.h"
#include "unversioned_row.h"
#include "versioned_row.h"

#include <yt/yt/core/misc/chunked_memory_pool.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

struct TDefaultRowBufferPoolTag { };

//! Holds data for a bunch of rows.
/*!
 *  Acts as a ref-counted wrapped around TChunkedMemoryPool plus a bunch
 *  of helpers.
 */
class TRowBuffer
    : public TRefCounted
{
public:
    TRowBuffer(
        TRefCountedTypeCookie tagCookie,
        IMemoryChunkProviderPtr chunkProvider,
        size_t startChunkSize = TChunkedMemoryPool::DefaultStartChunkSize)
        : Pool_(
            tagCookie,
            std::move(chunkProvider),
            startChunkSize)
    { }

    template <class TTag = TDefaultRowBufferPoolTag>
    explicit TRowBuffer(
        TTag = TDefaultRowBufferPoolTag(),
        size_t startChunkSize = TChunkedMemoryPool::DefaultStartChunkSize)
        : Pool_(
            TTag(),
            startChunkSize)
    { }

    template <class TTag>
    TRowBuffer(
        TTag,
        IMemoryChunkProviderPtr chunkProvider)
        : Pool_(
            GetRefCountedTypeCookie<TTag>(),
            std::move(chunkProvider))
    { }

    TChunkedMemoryPool* GetPool();

    TMutableUnversionedRow AllocateUnversioned(int valueCount);
    TMutableVersionedRow AllocateVersioned(
        int keyCount,
        int valueCount,
        int writeTimestampCount,
        int deleteTimestampCount);

    void CaptureValue(TUnversionedValue* value);
    TVersionedValue CaptureValue(const TVersionedValue& value);
    TUnversionedValue CaptureValue(const TUnversionedValue& value);

    TMutableUnversionedRow CaptureRow(TUnversionedRow row, bool captureValues = true);
    void CaptureValues(TMutableUnversionedRow row);
    TMutableUnversionedRow CaptureRow(TRange<TUnversionedValue> values, bool captureValues = true);
    std::vector<TMutableUnversionedRow> CaptureRows(TRange<TUnversionedRow> rows, bool captureValues = true);

    TMutableVersionedRow CaptureRow(TVersionedRow row, bool captureValues = true);
    void CaptureValues(TMutableVersionedRow row);

    //! Captures the row applying #idMapping to value ids and placing values to the proper positions.
    //! The returned row is schemaful.
    //! Skips values that map to negative ids with via #idMapping.
    TMutableUnversionedRow CaptureAndPermuteRow(
        TUnversionedRow row,
        const TTableSchema& tableSchema,
        const TNameTableToSchemaIdMapping& idMapping,
        std::vector<bool>* columnPresenceBuffer);

    //! Captures the row applying #idMapping to value ids.
    //! Skips values that map to negative ids with via #idMapping.
    TMutableVersionedRow CaptureAndPermuteRow(
        TVersionedRow row,
        const TTableSchema& tableSchema,
        const TNameTableToSchemaIdMapping& idMapping,
        std::vector<bool>* columnPresenceBuffer);

    //! Captures the row applying #idMapping to value ids.
    TMutableUnversionedRow CaptureAndPermuteRow(
        TUnversionedRow row,
        const TNameTableToSchemaIdMapping& idMapping);

    i64 GetSize() const;
    i64 GetCapacity() const;

    void Clear();
    void Purge();

private:
    TChunkedMemoryPool Pool_;
};

DEFINE_REFCOUNTED_TYPE(TRowBuffer)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
