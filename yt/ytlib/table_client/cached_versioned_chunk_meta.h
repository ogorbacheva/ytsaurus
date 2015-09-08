#pragma once

#include "public.h"
#include "chunk_meta_extensions.h"
#include "schema.h"
#include "unversioned_row.h"

#include <ytlib/chunk_client/chunk_meta_extensions.h>

#include <core/misc/property.h>
#include <core/misc/error.h>
#include <core/misc/public.h>

#include <core/actions/future.h>

#include <memory>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TCachedVersionedChunkMeta
    : public TIntrinsicRefCounted
{
public:
    DEFINE_BYREF_RO_PROPERTY(TOwningKey, MinKey);
    DEFINE_BYREF_RO_PROPERTY(TOwningKey, MaxKey);
    DEFINE_BYREF_RO_PROPERTY(std::vector<TOwningKey>, BlockIndexKeys);
    DEFINE_BYREF_RO_PROPERTY(NProto::TBlockMetaExt, BlockMeta);
    DEFINE_BYREF_RO_PROPERTY(NChunkClient::NProto::TChunkMeta, ChunkMeta);
    DEFINE_BYREF_RO_PROPERTY(TTableSchema, ChunkSchema);
    DEFINE_BYREF_RO_PROPERTY(TKeyColumns, KeyColumns);
    DEFINE_BYREF_RO_PROPERTY(NChunkClient::NProto::TMiscExt, Misc);
    DEFINE_BYREF_RO_PROPERTY(std::vector<TColumnIdMapping>, SchemaIdMapping);
    DEFINE_BYVAL_RO_PROPERTY(int, ChunkKeyColumnCount);

    static TFuture<TCachedVersionedChunkMetaPtr> Load(
        NChunkClient::IChunkReaderPtr chunkReader,
        const TTableSchema& schema,
        const TKeyColumns& keyColumns);

    int GetKeyColumnCount() const;

private:
    TCachedVersionedChunkMetaPtr DoLoad(
        NChunkClient::IChunkReaderPtr chunkReader,
        const TTableSchema& readerSchema,
        const TKeyColumns& keyColumns);

    void ValidateChunkMeta();
    void ValidateSchema(const TTableSchema& readerSchema);
    void ValidateKeyColumns(const TKeyColumns& chunkKeyColumns);

};

DEFINE_REFCOUNTED_TYPE(TCachedVersionedChunkMeta)

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
