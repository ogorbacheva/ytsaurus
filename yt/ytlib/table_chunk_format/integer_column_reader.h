#pragma once

#include "public.h"

#include "column_reader.h"

#include <yt/client/table_chunk_format/proto/column_meta.pb.h>

#include <yt/client/table_client/versioned_row.h>

namespace NYT::NTableChunkFormat {

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IVersionedColumnReader> CreateVersionedInt64ColumnReader(
    const NProto::TColumnMeta& columnMeta,
    int columnId,
    bool aggregate);

std::unique_ptr<IVersionedColumnReader> CreateVersionedUint64ColumnReader(
    const NProto::TColumnMeta& columnMeta,
    int columnId,
    bool aggregate);

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IUnversionedColumnReader> CreateUnversionedInt64ColumnReader(
    const NProto::TColumnMeta& columnMeta,
    int columnIndex,
    int columnId);

std::unique_ptr<IUnversionedColumnReader> CreateUnversionedUint64ColumnReader(
    const NProto::TColumnMeta& columnMeta,
    int columnIndex,
    int columnId);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableChunkFormat
