#pragma once

#include "../misc/common.h"
#include "../misc/guid.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

//! Represents an offset inside a chunk.
typedef i64 TBlockOffset;

//! Identifies a chunk.
typedef TGuid TChunkId;

//! Means "no chunk".
extern TChunkId NullChunkId;

////////////////////////////////////////////////////////////////////////////////

//! Identifies a block.
/*!
 * Each block is identified by its chunk id and block index (0-based).
 */
struct TBlockId
{
    //! TChunkId of the chunk where the block belongs.
    TChunkId ChunkId;

    //! An offset where the block starts.
    i32 BlockIndex;

    TBlockId(const TChunkId& chunkId, i32 blockIndex)
        : ChunkId(chunkId)
        , BlockIndex(blockIndex)
    { }

    //! Formats the id into the string (for debugging and logging purposes mainly).
    Stroka ToString() const
    {
        return Sprintf("%s:%d",
            ~ChunkId.ToString(),
            BlockIndex);
    }
};

//! Compares TBlockId s for equality.
inline bool operator==(const TBlockId& blockId1, const TBlockId& blockId2)
{
    return blockId1.ChunkId == blockId2.ChunkId &&
           blockId1.BlockIndex == blockId2.BlockIndex;
}

//! Compares TBlockId s for inequality.
inline bool operator!=(const TBlockId& blockId1, const TBlockId& blockId2)
{
    return !(blockId1 == blockId2);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

////////////////////////////////////////////////////////////////////////////////

//! A hasher for TBlockId.
template<>
struct hash<NYT::TBlockId>
{
    i32 operator()(const NYT::TBlockId& blockId) const
    {
        return (i32) THash<NYT::TGuid>()(blockId.ChunkId) * 497 + (i32) blockId.BlockIndex;
    }
};

////////////////////////////////////////////////////////////////////////////////

