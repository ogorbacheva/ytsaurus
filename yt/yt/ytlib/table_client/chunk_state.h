#pragma once

#include "public.h"

#include <yt/yt/ytlib/chunk_client/chunk_spec.h>
#include <yt/yt/ytlib/chunk_client/data_source.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

//! Extracted chunk state to avoid unnecessary reference counting.
struct TChunkState
    : public TRefCounted
{
    TChunkState(
        NChunkClient::IBlockCachePtr blockCache = nullptr,
        NChunkClient::NProto::TChunkSpec chunkSpec = {},
        TCachedVersionedChunkMetaPtr chunkMeta = nullptr,
        NTransactionClient::TTimestamp overrideTimestamp = NTransactionClient::NullTimestamp,
        IChunkLookupHashTablePtr lookupHashTable = nullptr,
        TChunkReaderPerformanceCountersPtr performanceCounters = nullptr,
        TKeyComparer keyComparer = {},
        TVirtualValueDirectoryPtr virtualValueDirectory = nullptr,
        TTableSchemaPtr tableSchema = nullptr,
        TChunkColumnMappingPtr chunkColumnMapping = nullptr);

    NChunkClient::IBlockCachePtr BlockCache;
    NChunkClient::NProto::TChunkSpec ChunkSpec;
    TCachedVersionedChunkMetaPtr ChunkMeta;
    NTransactionClient::TTimestamp OverrideTimestamp;
    IChunkLookupHashTablePtr LookupHashTable;
    TChunkReaderPerformanceCountersPtr PerformanceCounters;
    TKeyComparer KeyComparer;
    TVirtualValueDirectoryPtr VirtualValueDirectory;
    TTableSchemaPtr TableSchema;
    std::optional<NChunkClient::TDataSource> DataSource;
    TChunkColumnMappingPtr ChunkColumnMapping;
};

DEFINE_REFCOUNTED_TYPE(TChunkState)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient

