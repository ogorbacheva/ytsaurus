#pragma once

#include <ytlib/logging/log.h>
#include <ytlib/chunk_server/public.h>
#include <ytlib/actions/action_queue.h>

#include <ytlib/misc/lazy_ptr.h>


namespace NYT {
namespace NChunkClient {

////////////////////////////////////////////////////////////////////////////////

extern NLog::TLogger ChunkClientLogger;

////////////////////////////////////////////////////////////////////////////////

using NChunkServer::TChunkId;
using NChunkServer::NullChunkId;

////////////////////////////////////////////////////////////////////////////////

/*!
 * This thread is used for background operations in #TRemoteChunkReader
 * #TSequentialChunkReader, #TTableChunkReader and #TableReader
 */
extern TLazyPtr<TActionQueue> ReaderThread;

/*!
 *  This thread is used for background operations in 
 *  #TRemoteChunkWriter, #NTableClient::TChunkWriter and 
 *  #NTableClient::TChunkSetReader
 */
extern TLazyPtr<TActionQueue> WriterThread;

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT

