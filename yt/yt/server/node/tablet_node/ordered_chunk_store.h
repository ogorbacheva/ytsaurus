#pragma once

#include "public.h"
#include "store_detail.h"

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TOrderedChunkStore
    : public TChunkStoreBase
    , public TOrderedStoreBase
{
public:
    TOrderedChunkStore(
        IBootstrap* bootstrap,
        TTabletManagerConfigPtr config,
        TStoreId id,
        TTablet* tablet,
        const NTabletNode::NProto::TAddStoreDescriptor* addStoreDescriptor,
        NChunkClient::IBlockCachePtr blockCache,
        IVersionedChunkMetaManagerPtr chunkMetaManager,
        NDataNode::IChunkRegistryPtr chunkRegistry = nullptr,
        NDataNode::IChunkBlockManagerPtr chunkBlockManager = nullptr,
        NApi::NNative::IClientPtr client = nullptr,
        const NNodeTrackerClient::TNodeDescriptor& localDescriptor = {});

    // IStore implementation.
    TOrderedChunkStorePtr AsOrderedChunk() override;

    // IChunkStore implementation.
    EStoreType GetType() const override;

    // IOrderedStore implementation.
    NTableClient::ISchemafulUnversionedReaderPtr CreateReader(
        const TTabletSnapshotPtr& tabletSnapshot,
        int tabletIndex,
        i64 lowerRowIndex,
        i64 upperRowIndex,
        const TColumnFilter& columnFilter,
        const NChunkClient::TClientChunkReadOptions& chunkReadOptions,
        std::optional<EWorkloadCategory> workloadCategory) override;

    void Save(TSaveContext& context) const override;
    void Load(TLoadContext& context) override;

private:
    class TReader;

    using TIdMapping = TCompactVector<int, NTableClient::TypicalColumnCount>;

    NTableClient::TKeyComparer GetKeyComparer() override;

    NTableClient::ISchemafulUnversionedReaderPtr TryCreateCacheBasedReader(
        const NChunkClient::TClientChunkReadOptions& chunkReadOptions,
        const NChunkClient::TReadRange& readRange,
        const NTableClient::TTableSchemaPtr& readSchema,
        bool enableTabletIndex,
        bool enableRowIndex,
        int tabletIndex,
        i64 lowerRowIndex,
        const TIdMapping& idMapping);
};

DEFINE_REFCOUNTED_TYPE(TOrderedChunkStore)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
