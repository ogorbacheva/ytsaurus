#include "stdafx.h"
#include "file_writer.h"

#include <ytlib/misc/fs.h>
#include <ytlib/misc/serialize.h>
#include <ytlib/logging/log.h>

namespace NYT {
namespace NChunkClient {

using namespace NChunkHolder::NProto;

///////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ChunkClientLogger;

///////////////////////////////////////////////////////////////////////////////

TChunkFileWriter::TChunkFileWriter(const TChunkId& id, const Stroka& fileName)
    : Id(id)
    , FileName(fileName)
    , IsOpen(false)
    , IsClosed(false)
    , DataSize(0)
{
    ChunkMeta.set_id(id.ToProto());
}

void TChunkFileWriter::Open()
{
    YASSERT(!IsOpen);
    YASSERT(!IsClosed);

    DataFile.Reset(new TFile(
        FileName + NFS::TempFileSuffix,
        CreateAlways | WrOnly | Seq));

    IsOpen = true;
}

TAsyncError::TPtr TChunkFileWriter::AsyncWriteBlock(const TSharedRef& data)
{
    YASSERT(IsOpen);

    auto* blockInfo = ChunkMeta.add_blocks();
    blockInfo->set_offset(DataFile->GetPosition());
    blockInfo->set_size(static_cast<int>(data.Size()));
    blockInfo->set_checksum(GetChecksum(data));

    DataFile->Write(data.Begin(), data.Size());

    DataSize += data.Size();

    return MakeFuture(TError());
}

TAsyncError::TPtr TChunkFileWriter::AsyncClose(const TChunkAttributes& attributes)
{
    if (!IsOpen)
        return MakeFuture(TError());

    IsOpen = false;
    IsClosed = true;

    DataFile->Close();
    DataFile.Destroy();

    // Write meta.
    *ChunkMeta.mutable_attributes() = attributes;
    
    TBlob metaBlob(ChunkMeta.ByteSize());
    if (!ChunkMeta.SerializeToArray(metaBlob.begin(), metaBlob.ysize())) {
        LOG_FATAL("Failed to serialize chunk meta (FileName: %s)",
            ~FileName);
    }

    TChunkMetaHeader header;
    header.Signature = header.ExpectedSignature;
    header.Checksum = GetChecksum(metaBlob);

    Stroka chunkMetaFileName = FileName + ChunkMetaSuffix;

    TFile chunkMetaFile(
        chunkMetaFileName + NFS::TempFileSuffix,
        CreateAlways | WrOnly | Seq);
    Write(chunkMetaFile, header);
    chunkMetaFile.Write(metaBlob.begin(), metaBlob.ysize());
    chunkMetaFile.Close();

    if (!NFS::Rename(chunkMetaFileName + NFS::TempFileSuffix, chunkMetaFileName)) {
        ythrow yexception() << Sprintf("Error renaming temp chunk meta file %s",
            ~chunkMetaFileName.Quote());
    }

    if (!NFS::Rename(FileName + NFS::TempFileSuffix, FileName)) {
        ythrow yexception() << Sprintf("Error renaming temp chunk file %s",
            ~FileName.Quote());
    }

    ChunkInfo.set_id(Id.ToProto());
    ChunkInfo.set_meta_checksum(header.Checksum);
    ChunkInfo.set_size(DataSize + metaBlob.size() + sizeof (TChunkMetaHeader));
    ChunkInfo.mutable_blocks()->MergeFrom(ChunkMeta.blocks());
    ChunkInfo.mutable_attributes()->CopyFrom(ChunkMeta.attributes());

    return MakeFuture(TError());
}

TChunkId TChunkFileWriter::GetChunkId() const
{
    return Id;
}

TChunkInfo TChunkFileWriter::GetChunkInfo() const
{
    YASSERT(IsClosed);
    return ChunkInfo;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT

