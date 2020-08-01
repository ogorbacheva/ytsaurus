#pragma once

#include "chunk.h"
#include "private.h"

#include <yt/server/node/cluster_node/bootstrap.h>

#include <yt/server/node/tablet_node/sorted_dynamic_comparer.h>

#include <yt/client/table_client/unversioned_row.h>

#include <yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/ytlib/chunk_client/proto/data_node_service.pb.h>

#include <yt/client/misc/workload.h>

namespace NYT::NDataNode {

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = DataNodeLogger;

////////////////////////////////////////////////////////////////////////////////

class TLookupSession
{
public:
    TLookupSession(
        NClusterNode::TBootstrap* bootstrap,
        TChunkId chunkId,
        NChunkClient::TReadSessionId readSessionId,
        TWorkloadDescriptor workloadDescriptor,
        NQueryClient::TColumnFilter columnFilter,
        NQueryClient::TTimestamp timestamp,
        bool produceAllVersions,
        TCachedTableSchemaPtr tableSchema,
        const std::vector<TSharedRef>& serializedKeys,
        NCompression::ECodec codecId,
        NQueryClient::TTimestamp chunkTimestamp);

    TSharedRef Run();

    const NChunkClient::TChunkReaderStatisticsPtr& GetChunkReaderStatistics();

    //! Second value in tuple indicates whether we request schema from remote node.
    static std::tuple<TCachedTableSchemaPtr, bool> FindTableSchema(
        TChunkId chunkId,
        NChunkClient::TReadSessionId readSessionId,
        const NChunkClient::NProto::TReqLookupRows::TTableSchemaData& schemaData,
        const TTableSchemaCachePtr& tableSchemaCache);

private:
    struct TKeyReaderBufferTag { };

    NClusterNode::TBootstrap const* Bootstrap_;
    const TChunkId ChunkId_;
    const NChunkClient::TReadSessionId ReadSessionId_;
    const TWorkloadDescriptor WorkloadDescriptor_;
    const NQueryClient::TColumnFilter ColumnFilter_;
    const NQueryClient::TTimestamp Timestamp_;
    const bool ProduceAllVersions_;
    const TCachedTableSchemaPtr TableSchema_;
    NCompression::ICodec* const Codec_;
    const NQueryClient::TTimestamp ChunkTimestamp_;

    IChunkPtr Chunk_;
    TBlockReadOptions Options_;
    NChunkClient::IChunkReaderPtr UnderlyingChunkReader_;
    TSharedRange<NTableClient::TUnversionedRow> RequestedKeys_;
    const NTableClient::TRowBufferPtr KeyReaderRowBuffer_ = New<NTableClient::TRowBuffer>(TKeyReaderBufferTag());
    const NChunkClient::TChunkReaderStatisticsPtr ChunkReaderStatistics_ = New<NChunkClient::TChunkReaderStatistics>();

    void Verify();

    TSharedRef DoRun();
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
