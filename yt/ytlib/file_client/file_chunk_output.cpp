#include "stdafx.h"
#include "file_chunk_output.h"
#include "private.h"
#include "config.h"

#include <ytlib/misc/sync.h>
#include <ytlib/misc/address.h>
#include <ytlib/misc/protobuf_helpers.h>

#include <ytlib/chunk_client/chunk_ypath_proxy.h>
#include <ytlib/chunk_client/chunk_meta_extensions.h>
#include <ytlib/chunk_client/node_directory.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>

#include <ytlib/chunk_client/remote_writer.h>
#include <ytlib/chunk_client/chunk_replica.h>

#include <ytlib/object_client/object_service_proxy.h>
#include <ytlib/object_client/master_ypath_proxy.h>

#include <ytlib/meta_state/rpc_helpers.h>

namespace NYT {
namespace NFileClient {

using namespace NYTree;
using namespace NChunkClient;
using namespace NObjectClient;
using namespace NCypressClient;

////////////////////////////////////////////////////////////////////////////////

TFileChunkOutput::TFileChunkOutput(
    TFileWriterConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    const TTransactionId& transactionId,
    const Stroka& account)
    : Config(config)
    , ReplicationFactor(Config->ReplicationFactor)
    , UploadReplicationFactor(std::min(Config->ReplicationFactor, Config->UploadReplicationFactor))
    , MasterChannel(masterChannel)
    , TransactionId(transactionId)
    , Account(account)
    , IsOpen(false)
    , Size(0)
    , BlockCount(0)
    , Logger(FileWriterLogger)
{
    YCHECK(config);
    YCHECK(masterChannel);

    Codec = GetCodec(Config->Codec);
}

void TFileChunkOutput::Open()
{
    LOG_INFO("Opening file chunk output (TransactionId: %s, Account: %s, ReplicationFactor: %d, UploadReplicationFactor: %d)",
        ~ToString(TransactionId),
        ~Account,
        Config->ReplicationFactor,
        Config->UploadReplicationFactor);

    LOG_INFO("Creating chunk");
    auto nodeDirectory = New<TNodeDirectory>();
    {
        TObjectServiceProxy proxy(MasterChannel);

        auto req = TMasterYPathProxy::CreateObject();
        ToProto(req->mutable_transaction_id(), TransactionId);
        req->set_type(EObjectType::Chunk);
        req->set_account(Account);
        NMetaState::GenerateRpcMutationId(req);

        auto* reqExt = req->MutableExtension(NChunkClient::NProto::TReqCreateChunkExt::create_chunk);
        reqExt->set_preferred_host_name(TAddressResolver::Get()->GetLocalHostName());
        reqExt->set_upload_replication_factor(UploadReplicationFactor);
        reqExt->set_replication_factor(ReplicationFactor);
        reqExt->set_movable(Config->ChunkMovable);
        reqExt->set_vital(Config->ChunkVital);

        auto rsp = proxy.Execute(req).Get();
        THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error creating file chunk");

        ChunkId = FromProto<TGuid>(rsp->object_id());

        const auto& rspExt = rsp->GetExtension(NChunkClient::NProto::TRspCreateChunkExt::create_chunk);
        nodeDirectory->MergeFrom(rspExt.node_directory());
        Replicas = FromProto<TChunkReplica>(rspExt.replicas());
        if (Replicas.size() < Config->UploadReplicationFactor) {
            THROW_ERROR_EXCEPTION("Not enough data nodes available: %d received, %d needed",
                static_cast<int>(Replicas.size()),
                UploadReplicationFactor);
        }
    }

    Logger.AddTag(Sprintf("ChunkId: %s", ~ToString(ChunkId)));

    LOG_INFO("Chunk created");

    auto targets = nodeDirectory->GetDescriptors(Replicas);
    Writer = New<TRemoteWriter>(Config, ChunkId, targets);
    Writer->Open();

    IsOpen = true;

    LOG_INFO("File chunk output opened");
}

TFileChunkOutput::~TFileChunkOutput() throw()
{
    LOG_DEBUG_IF(IsOpen, "Writer cancelled");
}

void TFileChunkOutput::DoWrite(const void* buf, size_t len)
{
    YCHECK(IsOpen);

    LOG_DEBUG("Writing data (ChunkId: %s, Size: %d)",
        ~ToString(ChunkId),
        static_cast<int>(len));

    if (len == 0)
        return;

    if (Buffer.empty()) {
        Buffer.reserve(static_cast<size_t>(Config->BlockSize));
    }

    size_t dataSize = len;
    const ui8* dataPtr = static_cast<const ui8*>(buf);
    while (dataSize != 0) {
        // Copy a part of data trying to fill up the current block.
        size_t bufferSize = Buffer.size();
        size_t remainingSize = static_cast<size_t>(Config->BlockSize) - Buffer.size();
        size_t copySize = Min(dataSize, remainingSize);
        Buffer.resize(Buffer.size() + copySize);
        std::copy(dataPtr, dataPtr + copySize, Buffer.begin() + bufferSize);
        dataPtr += copySize;
        dataSize -= copySize;

        // Flush the block if full.
        if (Buffer.size() == Config->BlockSize) {
            FlushBlock();
        }
    }

    Size += len;
}

void TFileChunkOutput::DoFinish()
{
    if (!IsOpen)
        return;

    IsOpen = false;

    LOG_INFO("Closing file writer");

    // Flush the last block.
    FlushBlock();

    LOG_INFO("Closing chunk");
    {
        Meta.set_type(EChunkType::File);
        Meta.set_version(FormatVersion);

        NChunkClient::NProto::TMiscExt miscExt;
        miscExt.set_uncompressed_data_size(Size);
        miscExt.set_compressed_data_size(Size);
        miscExt.set_meta_size(Meta.ByteSize());
        miscExt.set_codec(Config->Codec);

        SetProtoExtension(Meta.mutable_extensions(), miscExt);
        SetProtoExtension(Meta.mutable_extensions(), BlocksExt);

        try {
            Sync(~Writer, &TRemoteWriter::AsyncClose, Meta);
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error closing chunk")
                << ex;
        }
    }
    LOG_INFO("Chunk closed");

    LOG_INFO("Confirming chunk");
    {
        TObjectServiceProxy proxy(MasterChannel);

        auto req = TChunkYPathProxy::Confirm(FromObjectId(ChunkId));
        *req->mutable_chunk_info() = Writer->GetChunkInfo();
        FOREACH (int index, Writer->GetWrittenIndexes()) {
            req->add_replicas(ToProto<ui32>(Replicas[index]));
        }
        *req->mutable_chunk_meta() = Meta;
        NMetaState::GenerateRpcMutationId(req);

        auto rsp = proxy.Execute(req).Get();
        THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error confirming chunk");
    }
    LOG_INFO("Chunk confirmed");

    LOG_INFO("File writer closed");
}

void TFileChunkOutput::FlushBlock()
{
    if (Buffer.empty())
        return;

    LOG_INFO("Writing block (BlockIndex: %d)", BlockCount);
    auto* block = BlocksExt.add_blocks();
    block->set_size(Buffer.size());
    try {
        struct TCompressedFileChunkBlockTag { };
        auto compressedBuffer = Codec->Compress(TSharedRef::FromBlob<TCompressedFileChunkBlockTag>(std::move(Buffer)));

        while (!Writer->TryWriteBlock(compressedBuffer)) {
            Sync(~Writer, &TRemoteWriter::GetReadyEvent);
        }
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error writing file block")
            << ex;
    }
    LOG_INFO("Block written (BlockIndex: %d)", BlockCount);

    Buffer.clear();
    ++BlockCount;
}

TChunkId TFileChunkOutput::GetChunkId() const
{
    return ChunkId;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileClient
} // namespace NYT
