#pragma once

#include "public.h"

#include <ytlib/misc/cache.h>
#include <ytlib/misc/ref.h>

#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_client/node_directory.h>

namespace NYT {
namespace NChunkHolder {

////////////////////////////////////////////////////////////////////////////////

//! Represents a cached block of chunk.
class TCachedBlock
    : public TCacheValueBase<TBlockId, TCachedBlock>
{
public:
    //! Constructs a new block from id and data.
    TCachedBlock(
        const TBlockId& blockId,
        const TSharedRef& data,
        const TNullable<NChunkClient::TNodeDescriptor>& source);

    ~TCachedBlock();

    DEFINE_BYVAL_RO_PROPERTY(TSharedRef, Data);
    DEFINE_BYREF_RO_PROPERTY(TNullable<NChunkClient::TNodeDescriptor>, Source);
};

////////////////////////////////////////////////////////////////////////////////

//! Manages cached blocks.
class TBlockStore
    : public TRefCounted
{
public:
    //! Constructs a store.
    TBlockStore(
        TDataNodeConfigPtr config,
        TBootstrap* bootstrap);

    ~TBlockStore();

    typedef TValueOrError<TCachedBlockPtr> TGetBlockResult;
    typedef TFuture<TGetBlockResult> TAsyncGetBlockResult;

    //! Gets (asynchronously) a block from the store.
    /*!
     * This call returns an async result that becomes set when the
     * block is fetched. Fetching an already-cached block is cheap
     * (i.e. requires no context switch). Fetching an uncached block
     * enqueues a disk-read action to the appropriate IO queue.
     */
    TAsyncGetBlockResult GetBlock(
        const TBlockId& blockId,
        bool enableCaching);

    //! Tries to find a block in the cache.
    /*!
     *  If the block is not available immediately, it returns NULL.
     *  No IO is queued.
     */
    TCachedBlockPtr FindBlock(const TBlockId& blockId);

    //! Puts a block into the store.
    /*!
     *  The store may already have another copy of the same block.
     *  In this case the block content is checked for identity.
     */
    TCachedBlockPtr PutBlock(
        const TBlockId& blockId,
        const TSharedRef& data,
        const TNullable<NChunkClient::TNodeDescriptor>& source);

    //! Gets a vector of all blocks stored in the cache. Thread-safe.
    std::vector<TCachedBlockPtr> GetAllBlocks() const;

    //! Returns the number of bytes that are scheduled for disk read IO.
    i64 GetPendingReadSize() const;

    //! Returns a caching adapter.
    NChunkClient::IBlockCachePtr GetBlockCache();

private:
    class TStoreImpl;
    friend class TStoreImpl;

    class TCacheImpl;
    friend class TCacheImpl;

    TIntrusivePtr<TStoreImpl> StoreImpl;
    TIntrusivePtr<TCacheImpl> CacheImpl;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkHolder
} // namespace NYT

