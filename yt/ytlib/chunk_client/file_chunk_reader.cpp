#include "file_chunk_reader.h"

namespace NYT
{

using namespace NChunkClient::NProto;

///////////////////////////////////////////////////////////////////////////////

TFileChunkReader::TFileChunkReader( Stroka fileName )
    : FileName(fileName)
{
    File.Reset(new TFile(fileName, OpenExisting|RdOnly));

    TChunkFooter footer;
    File->Seek(sizeof (TChunkFooter), sEnd);
    File->Read(&footer, sizeof (footer));

    if (footer.Singature != TChunkFooter::ExpectedSignature) {
        ythrow yexception() <<
            Sprintf("Chunk footer signature mismatch in %s", ~fileName.Quote());
    }


    YASSERT(footer.MetaSize >= 0);
    YASSERT(footer.MetaOffset >= 0);

    TBlob metaBlob(footer.MetaSize);
    File->Pread(metaBlob.begin(), footer.MetaSize, footer.MetaOffset);

    if (!Meta.ParseFromArray(metaBlob.begin(), footer.MetaSize)) {
        ythrow yexception() << Sprintf("Failed to parse chunk meta in %s",
            ~FileName.Quote());
    }

    TChunkOffset currentOffset = 0;
    BlockOffsets.reserve(GetBlockCount());
    for (int blockIndex = 0; blockIndex < GetBlockCount(); ++blockIndex) {
        BlockOffsets.push_back(currentOffset);
        currentOffset += Meta.GetBlocks(blockIndex).GetSize();
    }
}

i32 TFileChunkReader::GetBlockCount() const
{
    return Meta.BlocksSize();
}

TAsyncResult<IChunkReader::TReadResult>::TPtr
TFileChunkReader::AsyncReadBlocks(const yvector<int>& blockIndexes)
{
    TReadResult result;
    result.Blocks.reserve(blockIndexes.ysize());

    for (int index = 0; index < blockIndexes.ysize(); ++index) {
        i32 blockIndex = blockIndexes[index];
        result.Blocks.push_back(ReadBlock(blockIndex));
    }

    return New< TAsyncResult<TReadResult> >(result);
}

NYT::TSharedRef TFileChunkReader::ReadBlock(int blockIndex)
{
    i32 blockCount = GetBlockCount();

    if (blockIndex > blockCount || blockIndex < -blockCount) {
        return TSharedRef();
    }

    if (blockIndex < 0) {
        blockIndex += blockCount;
    }

    YASSERT(blockIndex >= 0 && blockIndex < blockCount);
    const TBlockInfo& blockInfo = Meta.GetBlocks(blockIndex);

    TBlob data(blockInfo.GetSize());
    File->Pread(data.begin(), data.size(), BlockOffsets[blockIndex]); 

    TSharedRef result(data);

    if (blockInfo.GetChecksum() != GetChecksum(result)) {
        ythrow yexception() << Sprintf("Chunk footer signature mismatch in %s (BlockIndex: %d)",
            ~FileName.Quote(),
            blockIndex);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NYT

