#pragma once

#include "public.h"

#include <ytlib/codecs/codec.h>
#include <ytlib/misc/thread_affinity.h>
#include <ytlib/logging/tagged_logger.h>
#include <ytlib/object_server/public.h>
#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_server/public.h>
#include <ytlib/chunk_holder/chunk.pb.h>
#include <ytlib/rpc/public.h>

namespace NYT {
namespace NFileClient {

////////////////////////////////////////////////////////////////////////////////

//! A client-side facade for writing files.
/*!
 *  The client must call #Open and then feed the data in by calling #Write.
 *  Finally it must call #Finish.
 */
class TFileChunkOutput
    : public TOutputStream
{
public:
    //! Initializes an instance.
    TFileChunkOutput(
        TFileWriterConfigPtr config,
        NRpc::IChannelPtr masterChannel,
        NObjectServer::TTransactionId transactionId);

    ~TFileChunkOutput() throw();

    void Open();
    NChunkServer::TChunkId GetChunkId() const;

private:
    //! Adds another portion of data.
    /*!
     *  This portion does not necessary makes up a block. The writer maintains an internal buffer
     *  and splits the input data into parts of equal size (see #TConfig::BlockSize).
     */
    void DoWrite(const void* buf, size_t len);

    //! Closes the writer.
    void DoFinish();
    void FlushBlock();

    TFileWriterConfigPtr Config;

    NRpc::IChannelPtr MasterChannel;
    NObjectServer::TTransactionId TransactionId;

    bool IsOpen;
    i64 Size;
    i32 BlockCount;
    NChunkClient::TRemoteWriterPtr Writer;
    NChunkServer::TChunkId ChunkId;
    TCodecPtr Codec;
    TBlob Buffer;
    NChunkHolder::NProto::TChunkMeta Meta;

    NLog::TTaggedLogger Logger;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileClient
} // namespace NYT
