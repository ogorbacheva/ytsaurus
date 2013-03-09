#include "stdafx.h"
#include "block_id.h"

namespace NYT {
namespace NChunkClient {

////////////////////////////////////////////////////////////////////////////////

TBlockId::TBlockId(
    const TChunkId& chunkId,
    int blockIndex)
    : ChunkId(chunkId)
    , BlockIndex(blockIndex)
{ }

Stroka ToString(const TBlockId& id)
{
    return Sprintf("%s:%d", ~ToString(id.ChunkId), id.BlockIndex);
}

bool operator == (const TBlockId& lhs, const TBlockId& rhs)
{
    return lhs.ChunkId == rhs.ChunkId &&
           lhs.BlockIndex == rhs.BlockIndex;
}

bool operator != (const TBlockId& lhs, const TBlockId& rhs)
{
    return !(lhs == rhs);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT

