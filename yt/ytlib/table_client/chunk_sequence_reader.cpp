﻿#include "stdafx.h"
#include "chunk_sequence_reader.h"

#include <limits>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

TChunkSequenceReader::TChunkSequenceReader(
    TConfig* config,
    const TChannel& channel,
    const NTransactionClient::TTransactionId& transactionId,
    NRpc::IChannel* masterChannel,
    const yvector<NChunkClient::TChunkId>& chunkIds,
    int startRow,
    int endRow)
    : Config(config)
    , Channel(channel)
    , TransactionId(transactionId)
    , ChunkIds(chunkIds)
    , StartRow(startRow)
    , EndRow(endRow)
    , MasterChannel(masterChannel)
    , NextChunkIndex(-1)
    , NextReader(New< TFuture<TChunkReader::TPtr> >())
{
    PrepareNextChunk();
}

void TChunkSequenceReader::PrepareNextChunk()
{
    YASSERT(!NextReader->IsSet());
    YASSERT(NextChunkIndex < ChunkIds.ysize());

    ++NextChunkIndex;
    if (NextChunkIndex == ChunkIds.ysize()) {
        NextReader->Set(NULL);
        return;
    }

    auto remoteReaderFactory = CreateRemoteReaderFactory(~Config->RemoteReader);
    auto retriableReader = New<TRetriableReader>(
        ~Config->RetriableReader,
        ChunkIds[NextChunkIndex],
        TransactionId,
        ~MasterChannel,
        ~remoteReaderFactory);

    int startRow = NextChunkIndex == 0 ? StartRow : 0;
    int endRow = NextChunkIndex == ChunkIds.ysize() - 1 ? 
        EndRow : std::numeric_limits<int>::max();

    TChunkReader::TPtr chunkReader = New<TChunkReader>(
        ~Config->SequentialReader,
        Channel,
        ~retriableReader,
        startRow,
        endRow);

    chunkReader->AsyncOpen()->Subscribe(FromMethod(
        &TChunkSequenceReader::OnNextReaderOpened,
        TPtr(this),
        chunkReader));
}

void TChunkSequenceReader::OnNextReaderOpened(
    TError error, 
    TChunkReader::TPtr reader)
{
    YASSERT(!NextReader->IsSet());

    if (error.IsOK()) {
        NextReader->Set(reader);
        return;
    }

    State.Fail(error);
    NextReader->Set(NULL);
}

TAsyncError::TPtr TChunkSequenceReader::AsyncOpen()
{
    YASSERT(NextChunkIndex == 0);
    YASSERT(!State.HasRunningOperation());
    State.StartOperation();
    NextReader->Subscribe(FromMethod(
        &TChunkSequenceReader::SetCurrentChunk,
        TPtr(this)));

    return State.GetOperationError();
}

void TChunkSequenceReader::SetCurrentChunk(TChunkReader::TPtr nextReader)
{
    CurrentReader = nextReader;
    if (nextReader) {
        NextReader = New< TFuture<TChunkReader::TPtr> >();
        PrepareNextChunk();

        if (NextChunkIndex > 1) {
            // Current chunk is not the first one.
            YASSERT(CurrentReader->HasNextRow());
            CurrentReader->AsyncNextRow()->Subscribe(FromMethod(
                &TChunkSequenceReader::OnNextRow,
                TPtr(this)));
            return;
        }

    } else {
        YASSERT(!State.IsActive());
    }

    // Finishing AsyncOpen.
    State.FinishOperation();
}

void TChunkSequenceReader::OnNextRow(TError error)
{
    if (!error.IsOK()) {
        State.Fail(error);
    }

    State.FinishOperation();
}

bool TChunkSequenceReader::HasNextRow() const
{
    YASSERT(!State.HasRunningOperation());
    YASSERT(NextChunkIndex > 0);
    return NextChunkIndex < ChunkIds.ysize() || CurrentReader->HasNextRow();
}

TAsyncError::TPtr TChunkSequenceReader::AsyncNextRow()
{
    if (CurrentReader->HasNextRow()) {
        return CurrentReader->AsyncNextRow();
    } else {
        State.StartOperation();

        NextReader->Subscribe(FromMethod(
            &TChunkSequenceReader::SetCurrentChunk,
            TPtr(this)));

        return State.GetOperationError();
    }
}

bool NYT::NTableClient::TChunkSequenceReader::NextColumn()
{
    return CurrentReader->NextColumn();
}

TValue TChunkSequenceReader::GetValue() const
{
    return CurrentReader->GetValue();
}

TColumn TChunkSequenceReader::GetColumn() const
{
    return CurrentReader->GetColumn();
}

void TChunkSequenceReader::Cancel(const TError& error)
{
    State.Cancel(error);
    CurrentReader->Cancel(error);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
