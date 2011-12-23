﻿#pragma once

#include "common.h"
#include "reader.h"
#include "chunk_reader.h"

#include "../chunk_client/retriable_reader.h"
#include "../transaction_client/transaction.h"
#include "../misc/async_stream_state.h"

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TChunkSequenceReader
    : public IAsyncReader
{
public:
    typedef TIntrusivePtr<TChunkSequenceReader> TPtr;

    struct TConfig
        : public TConfigurable
    {
        typedef TIntrusivePtr<TConfig> TPtr;

        NChunkClient::TRetriableReader::TConfig::TPtr RetriableReader;
        NChunkClient::TRemoteReaderConfig::TPtr RemoteReader;
        NChunkClient::TSequentialReader::TConfig::TPtr SequentialReader;

        TConfig()
        {
            Register("retriable_reader", RetriableReader).DefaultNew();
            Register("remote_reader", RemoteReader).DefaultNew();
            Register("sequential_reader", SequentialReader).DefaultNew();
        }
    };

    TChunkSequenceReader(
        TConfig* config,
        const TChannel& channel,
        const NTransactionClient::TTransactionId& transactionId,
        NRpc::IChannel* masterChannel,
        // ToDo: use rvalue reference.
        const yvector<NChunkClient::TChunkId>& chunks,
        int startRow,
        int endRow);

    TAsyncError::TPtr AsyncOpen();

    bool HasNextRow() const;

    TAsyncError::TPtr AsyncNextRow();

    bool NextColumn();

    TValue GetValue() const;

    TColumn GetColumn() const;

    void Cancel(const TError& error);

private:
    void PrepareNextChunk();
    void OnNextReaderOpened(
        TError error, 
        TChunkReader::TPtr reader);
    void SetCurrentChunk(TChunkReader::TPtr nextReader);
    void OnNextRow(TError error);


    const TConfig::TPtr Config;
    const TChannel Channel;
    const NTransactionClient::TTransactionId TransactionId;
    const yvector<NChunkClient::TChunkId> ChunkIds;
    const int StartRow;
    const int EndRow;

    NRpc::IChannel::TPtr MasterChannel;

    TAsyncStreamState State;

    int NextChunkIndex;
    TFuture<TChunkReader::TPtr>::TPtr NextReader;
    TChunkReader::TPtr CurrentReader;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
