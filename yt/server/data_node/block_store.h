#pragma once

#include "public.h"

#include <yt/server/cell_node/public.h>

#include <yt/ytlib/chunk_client/public.h>

#include <yt/ytlib/node_tracker_client/node_directory.h>

#include <yt/core/misc/async_cache.h>
#include <yt/core/misc/ref.h>

namespace NYT {
namespace NDataNode {

////////////////////////////////////////////////////////////////////////////////

//! Represents a cached block of chunk.
class TCachedBlock
    : public TAsyncCacheValueBase<TBlockId, TCachedBlock>
{
public:
    DEFINE_BYVAL_RO_PROPERTY(TSharedRef, Data);
    DEFINE_BYREF_RO_PROPERTY(TNullable<NNodeTrackerClient::TNodeDescriptor>, Source);

public:
    //! Constructs a new block from id and data.
    TCachedBlock(
        const TBlockId& blockId,
        const TSharedRef& data,
        const TNullable<NNodeTrackerClient::TNodeDescriptor>& source);

};

DEFINE_REFCOUNTED_TYPE(TCachedBlock)

using TCachedBlockCookie = TAsyncSlruCacheBase<TBlockId, TCachedBlock>::TInsertCookie;

////////////////////////////////////////////////////////////////////////////////

//! Manages chunk blocks stored at Data Node.
/*!
 *  \note
 *  Thread affinity: any
 */
class TBlockStore
    : public TRefCounted
{
public:
    TBlockStore(
        TDataNodeConfigPtr config,
        NCellNode::TBootstrap* bootstrap);

    ~TBlockStore();

    //! Synchronously looks up a compressed block in the store's cache.
    TCachedBlockPtr FindCachedBlock(const TBlockId& blockId);

    //! Puts a compressed block into the store's cache.
    /*!
     *  The store may already have another copy of the same block.
     *  In this case the block content is checked for identity.
     */
    void PutCachedBlock(
        const TBlockId& blockId,
        const TSharedRef& data,
        const TNullable<NNodeTrackerClient::TNodeDescriptor>& source);

    //! Starts an asynchronous block load.
    /*!
     *  See TAsyncCacheValueBase for more details.
     */
    TCachedBlockCookie BeginInsertCachedBlock(const TBlockId& blockId);

    //! Asynchronously reads a range of blocks from the store.
    /*!
     *  If some unrecoverable IO error happens during retrieval then the latter error is returned.
     *
     *  The resulting list may contain less blocks than requested.
     *  All returned blocks, however, are not null.
     *  The empty list indicates that the requested blocks are all out of range.
     *
     *  Note that blob chunks will indicate an error if an attempt is made to read a non-existing block.
     *  Journal chunks, however, will silently ignore it.
     */
    TFuture<std::vector<TSharedRef>> ReadBlockRange(
        const TChunkId& chunkId,
        int firstBlockIndex,
        int blockCount,
        const TWorkloadDescriptor& workloadDescriptor,
        NChunkClient::IBlockCachePtr blockCache,
        bool populateCache);

    //! Asynchronously reads a set of blocks from the store.
    /*!
     *  If some unrecoverable IO error happens during retrieval then the latter error is returned.
     *
     *  The resulting list may contain less blocks than requested.
     *  If the whole chunk or some of its blocks does not exist then null block may be returned.
     */
    TFuture<std::vector<TSharedRef>> ReadBlockSet(
        const TChunkId& chunkId,
        const std::vector<int>& blockIndexes,
        const TWorkloadDescriptor& workloadDescriptor,
        NChunkClient::IBlockCachePtr blockCache,
        bool populateCache);

    //! Gets a vector of all blocks stored in the cache. Thread-safe.
    std::vector<TCachedBlockPtr> GetAllBlocks() const;

private:
    class TImpl;
    const TIntrusivePtr<TImpl> Impl_;

};

DEFINE_REFCOUNTED_TYPE(TBlockStore)

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT

