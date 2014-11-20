﻿#pragma once

#include "public.h"

#include "schemaless_chunk_reader.h"

#include <ytlib/chunk_client/public.h>

#include <ytlib/node_tracker_client/public.h>

#include <core/rpc/public.h>


namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

ISchemalessMultiChunkReaderPtr CreateSchemalessPartitionSortReader(
    NChunkClient::TMultiChunkReaderConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    NChunkClient::IBlockCachePtr compressedBlockCache,
    NChunkClient::IBlockCachePtr uncompressedBlockCache,
    NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
    const TKeyColumns& keyColumns,
    TNameTablePtr nameTable,
    TClosure onNetworkReleased,
    const std::vector<NChunkClient::NProto::TChunkSpec>& chunks,
    i64 estimatedRowCount,
    bool isApproximate);

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
