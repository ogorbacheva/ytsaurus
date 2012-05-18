﻿#include "stdafx.h"
#include "private.h"
#include "chunk_sequence_reader.h"
#include "chunk_reader.h"
#include "config.h"
#include "schema.h"

#include <ytlib/chunk_client/block_cache.h>
#include <ytlib/chunk_client/remote_reader.h>
#include <ytlib/chunk_client/async_reader.h>
#include <ytlib/misc/protobuf_helpers.h>
#include <ytlib/rpc/channel.h>

#include <limits>

namespace NYT {
namespace NTableClient {

using namespace NChunkServer;

static NLog::TLogger& Logger = TableReaderLogger;

////////////////////////////////////////////////////////////////////////////////

TChunkSequenceReader::TChunkSequenceReader(
    TChunkSequenceReaderConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    NChunkClient::IBlockCachePtr blockCache,
    const std::vector<NProto::TInputChunk>& fetchedChunks,
    int partitionTag,
    TReaderOptions options)
    : Config(config)
    , BlockCache(blockCache)
    , InputChunks(fetchedChunks)
    , MasterChannel(masterChannel)
    , NextChunkIndex(-1)
    , NextReader(NewPromise<TChunkReaderPtr>())
    , PartitionTag(partitionTag)
    , Options(options)
    , TotalValueCount(0)
    , TotalRowCount(0)
    , CurrentRowIndex(0)
{
    // Current reader is not set.
    Readers.push_back(TChunkReaderPtr());
    for (int i = 0; i < InputChunks.size(); ++i) {
        auto miscExt = GetProtoExtension<NChunkHolder::NProto::TMiscExt>(InputChunks[i].extensions());
        TotalRowCount += miscExt->row_count();
        TotalValueCount += miscExt->value_count();
    }

    PrepareNextChunk();
}

void TChunkSequenceReader::PrepareNextChunk()
{
    YASSERT(!NextReader.IsSet());
    int chunkSlicesSize = static_cast<int>(InputChunks.size());
    YASSERT(NextChunkIndex < chunkSlicesSize);

    ++NextChunkIndex;
    if (NextChunkIndex == chunkSlicesSize) {
        NextReader.Set(TIntrusivePtr<TChunkReader>());
        return;
    }

    const auto& inputChunk = InputChunks[NextChunkIndex];
    const auto& slice = inputChunk.slice();
    TChunkId chunkId = TChunkId::FromProto(inputChunk.slice().chunk_id());
    auto remoteReader = CreateRemoteReader(
        Config->RemoteReader,
        BlockCache,
        ~MasterChannel,
        chunkId,
        FromProto<Stroka>(inputChunk.holder_addresses()));

    auto chunkReader = New<TChunkReader>(
        Config->SequentialReader,
        TChannel::FromProto(inputChunk.channel()),
        remoteReader,
        slice.start_limit(),
        slice.end_limit(),
        inputChunk.row_attributes(),
        PartitionTag,
        Options); // ToDo(psushin): pass row attributes here.

    chunkReader->AsyncOpen().Subscribe(BIND(
        &TChunkSequenceReader::OnNextReaderOpened,
        MakeWeak(this),
        chunkReader));
}

void TChunkSequenceReader::OnNextReaderOpened(
    TChunkReaderPtr reader,
    TError error)
{
    YASSERT(!NextReader.IsSet());

    if (error.IsOK()) {
        NextReader.Set(reader);
        return;
    }

    State.Fail(error);
    NextReader.Set(TIntrusivePtr<TChunkReader>());
}

TAsyncError TChunkSequenceReader::AsyncOpen()
{
    YASSERT(NextChunkIndex == 0);
    YASSERT(!State.HasRunningOperation());

    if (InputChunks.size() != 0) {
        State.StartOperation();
        NextReader.Subscribe(BIND(
            &TChunkSequenceReader::SetCurrentChunk,
            MakeWeak(this)));
    }

    return State.GetOperationError();
}

void TChunkSequenceReader::SetCurrentChunk(TChunkReaderPtr nextReader)
{
    if (!Options.KeepBlocks) {
        Readers.clear();
    }

    Readers.push_back(nextReader);

    if (nextReader) {
        {
            // Update row count after opening new reader.
            // Take actual number of rows from used readers, estimated from just opened and
            // misc estimation for pending ones.
            TotalRowCount = CurrentRowIndex + nextReader->GetRowCount();
            for (int i = NextChunkIndex + 1; i < InputChunks.size(); ++i) {
                auto miscExt = GetProtoExtension<NChunkHolder::NProto::TMiscExt>(InputChunks[i].extensions());
                TotalRowCount += miscExt->row_count();
            }
        }

        NextReader = NewPromise<TChunkReaderPtr>();
        PrepareNextChunk();

        if (!Readers.back()->IsValid()) {
            NextReader.Subscribe(BIND(
                &TChunkSequenceReader::SetCurrentChunk,
                MakeWeak(this)));
            return;
        }
    } 

    // Finishing AsyncOpen.
    State.FinishOperation();
}

void TChunkSequenceReader::OnRowFetched(TError error)
{
    if (!error.IsOK()) {
        State.Fail(error);
        return;
    }

    if (!Readers.back()->IsValid()) {
        NextReader.Subscribe(BIND(
            &TChunkSequenceReader::SetCurrentChunk,
            MakeWeak(this)));
        return;
    }

    ++CurrentRowIndex;
    State.FinishOperation();
}

bool TChunkSequenceReader::IsValid() const
{
    YASSERT(!State.HasRunningOperation());
    if (!Readers.back())
        return false;

    return Readers.back()->IsValid();
}

TRow& TChunkSequenceReader::GetRow()
{
    YASSERT(!State.HasRunningOperation());
    YASSERT(Readers.back());
    YASSERT(Readers.back()->IsValid());

    return Readers.back()->GetRow();
}

const NYTree::TYson& TChunkSequenceReader::GetRowAttributes() const
{
    YASSERT(!State.HasRunningOperation());
    YASSERT(Readers.back());
    YASSERT(Readers.back()->IsValid());

    return Readers.back()->GetRowAttributes();
}

TAsyncError TChunkSequenceReader::AsyncNextRow()
{
    YASSERT(!State.HasRunningOperation());
    YASSERT(IsValid());

    State.StartOperation();
    
    // This is a performance-critical spot. Try to avoid using callbacks for synchronously fetched rows.
    auto asyncResult = Readers.back()->AsyncNextRow();
    auto error = asyncResult.TryGet();
    if (error) {
        OnRowFetched(error.Get());
    } else {
        asyncResult.Subscribe(BIND(&TChunkSequenceReader::OnRowFetched, MakeWeak(this)));
    }

    return State.GetOperationError();
}

const TNonOwningKey& TChunkSequenceReader::GetKey() const
{
    YASSERT(!State.HasRunningOperation());
    YASSERT(Readers.back());
    YASSERT(Readers.back()->IsValid());

    return Readers.back()->GetKey();
}

double TChunkSequenceReader::GetProgress() const
{
    return CurrentRowIndex / double(TotalRowCount);
}

i64 TChunkSequenceReader::GetRowCount() const
{
    return TotalRowCount;
}

i64 TChunkSequenceReader::GetValueCount() const
{
    return TotalValueCount;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
