#pragma once

#include "public.h"
#include "chunk_meta_extensions.h"

#include <ytlib/codecs/codec.h>

#include <ytlib/misc/thread_affinity.h>

#include <ytlib/logging/tagged_logger.h>

#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_client/chunk_replica.h>
#include <ytlib/chunk_client/chunk.pb.h>

#include <ytlib/rpc/public.h>

#include <server/chunk_server/public.h>

#include <server/object_server/public.h>

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
        const NObjectClient::TTransactionId& transactionId,
        const Stroka& account);

    ~TFileChunkOutput() throw();

    void Open();
    NChunkClient::TChunkId GetChunkId() const;

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

    const TFileWriterConfigPtr Config;
    const int ReplicationFactor;
    const int UploadReplicationFactor;

    NRpc::IChannelPtr MasterChannel;
    NObjectClient::TTransactionId TransactionId;
    Stroka Account;

    bool IsOpen;
    i64 Size;
    i32 BlockCount;
    NChunkClient::TRemoteWriterPtr Writer;
    std::vector<NChunkClient::TChunkReplica> Replicas;
    NChunkClient::TChunkId ChunkId;
    ICodec* Codec;
    TBlob Buffer;
    NChunkClient::NProto::TChunkMeta Meta;
    NFileClient::NProto::TBlocksExt BlocksExt;

    NLog::TTaggedLogger Logger;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileClient
} // namespace NYT
