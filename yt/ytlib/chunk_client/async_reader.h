#pragma once

#include "chunk.pb.h"

#include <ytlib/misc/common.h>
#include <ytlib/misc/ref.h>
#include <ytlib/actions/future.h>
#include <ytlib/misc/enum.h>

#include <ytlib/misc/error.h>

namespace NYT {
namespace NChunkClient {

///////////////////////////////////////////////////////////////////////////////

//! Provides a basic interface for readings chunks from holders.
struct IAsyncReader
    : virtual TRefCounted
{
    typedef TIntrusivePtr<IAsyncReader> TPtr;

    //! Describes a result of #AsyncReadBlocks.
    typedef TValueOrError< yvector<TSharedRef> > TReadResult;
    typedef TFuture<TReadResult> TAsyncReadResult;

    //! Describes a result of #AsyncGetChunkInfo.
    typedef TValueOrError<NChunkHolder::NProto::TChunkInfo> TGetInfoResult;
    typedef TFuture<TGetInfoResult> TAsyncGetInfoResult;

    //! Reads (asynchronously) a given set of blocks.
    /*!
     *  Negative indexes indicate that blocks are numbered from the end.
     *  I.e. -1 means the last block.
     */
    virtual TAsyncReadResult::TPtr AsyncReadBlocks(const yvector<int>& blockIndexes) = 0;

    virtual TAsyncGetInfoResult::TPtr AsyncGetChunkInfo() = 0;
};

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
