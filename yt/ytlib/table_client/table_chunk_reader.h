#pragma once

#include "public.h"

#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_client/schema.h>
#include <ytlib/chunk_client/key.h>
#include <ytlib/chunk_client/chunk_spec.h>

#include <ytlib/table_client/table_chunk_meta.pb.h>

#include <ytlib/compression/public.h>

#include <ytlib/misc/blob_output.h>
#include <ytlib/misc/thread_affinity.h>
#include <ytlib/misc/async_stream_state.h>


#include <ytlib/ytree/public.h>
#include <ytlib/ytree/yson_string.h>

#include <ytlib/yson/lexer.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TTableChunkReaderFacade
    : public TNonCopyable
{
public:
    const TRow& GetRow() const;
    const NChunkClient::TNonOwningKey& GetKey() const;
    const TNullable<int>& GetTableIndex() const;

private:
    friend class TTableChunkReader;
    TTableChunkReader* Reader;

    explicit TTableChunkReaderFacade(TTableChunkReader* reader);

};

////////////////////////////////////////////////////////////////////////////////

//! Reads single table chunk row-after-row using given #NChunkClient::IAsyncReader.
class TTableChunkReader
    : public virtual TRefCounted
{
public:
    typedef TTableChunkReaderProvider TProvider;
    typedef TTableChunkReaderFacade TFacade;

    TTableChunkReader(
        TTableChunkReaderProviderPtr provider,
        NChunkClient::TSequentialReaderConfigPtr config,
        const NChunkClient::TChannel& channel,
        NChunkClient::IAsyncReaderPtr chunkReader,
        const NChunkClient::NProto::TReadLimit& startLimit,
        const NChunkClient::NProto::TReadLimit& endLimit,
        TNullable<int> tableIndex,
        int partitionTag,
        TChunkReaderOptionsPtr options);

    TAsyncError AsyncOpen();

    bool FetchNext();
    TAsyncError GetReadyEvent();

    const TFacade* GetFacade() const;

    i64 GetRowIndex() const;
    i64 GetRowCount() const;
    TFuture<void> GetFetchingCompleteEvent();

    // Called by facade.
    const TRow& GetRow() const;
    const NChunkClient::TNonOwningKey& GetKey() const;
    const TNullable<int>& GetTableIndex() const;

private:
    struct TColumnInfo
    {
        int KeyIndex;
        bool InChannel;
        i64 RowIndex;

        TColumnInfo()
            : KeyIndex(-1)
            , InChannel(false)
            , RowIndex(-1)
        { }
    };

    class TKeyValidator;

    class TInitializer;

    //! Initializer for regular table chunks.
    class TRegularInitializer;

    //! Initializer for partition table chunks.
    class TPartitionInitializer;

    bool DoFetchNextRow();
    bool ContinueFetchNextRow(int channelIndex, TError error);

    void MakeCurrentRow();
    bool ValidateRow();
    void OnRowFetched(TError error);

    TColumnInfo& GetColumnInfo(const TStringBuf& column);

    TTableChunkReaderProviderPtr Provider;
    TTableChunkReaderFacade Facade;

    volatile bool IsFinished;

    NChunkClient::TSequentialReaderPtr SequentialReader;
    NChunkClient::TChannel Channel;

    TIntrusivePtr<TInitializer> Initializer;

    TAsyncStreamState ReaderState;
    TAsyncStreamState RowState;

    TChunkReaderOptionsPtr Options;

    TRow CurrentRow;
    NChunkClient::TNonOwningKey CurrentKey;
    TNullable<int> TableIndex;

    NYson::TStatelessLexer Lexer;

    yhash_map<TStringBuf, TColumnInfo> ColumnsMap;
    std::vector<Stroka> ColumnNames;

    i64 CurrentRowIndex;
    i64 StartRowIndex;
    i64 EndRowIndex;

    int PartitionTag;

    TCallback<void(TError)> OnRowFetchedCallback;

    std::unique_ptr<TKeyValidator> EndValidator;

    NProto::TKeyColumnsExt KeyColumnsExt;

    /*!
     *  See #DoNextRow for usage.
     */
    const TAsyncErrorPromise SuccessResult;

    std::vector<TChannelReaderPtr> ChannelReaders;

    /*!
     *  If #TReaderOptions::KeepBlocks option is set then the reader keeps references
     *  to all (uncompressed) blocks it has fetched.
     */
    std::vector<TSharedRef> FetchedBlocks;

    DECLARE_THREAD_AFFINITY_SLOT(ClientThread);
    DECLARE_THREAD_AFFINITY_SLOT(ReaderThread);
};

////////////////////////////////////////////////////////////////////////////////

class TTableChunkReaderProvider
    : public TRefCounted
{
    DEFINE_BYVAL_RO_PROPERTY(volatile i64, RowIndex);
    DEFINE_BYVAL_RO_PROPERTY(volatile i64, RowCount);

public:
    TTableChunkReaderProvider(
        const std::vector<NChunkClient::NProto::TChunkSpec>& chunkSpecs,
        const NChunkClient::TSequentialReaderConfigPtr& config,
        const TChunkReaderOptionsPtr& options = New<TChunkReaderOptions>());

    TTableChunkReaderPtr CreateReader(
        const NChunkClient::NProto::TChunkSpec& chunkSpec,
        const NChunkClient::IAsyncReaderPtr& chunkReader);

    void OnReaderOpened(
        TTableChunkReaderPtr reader,
        NChunkClient::NProto::TChunkSpec& chunkSpec);

    void OnReaderFinished(TTableChunkReaderPtr reader);

    bool KeepInMemory() const;

private:
    friend class TTableChunkReader;

    NChunkClient::TSequentialReaderConfigPtr Config;
    TChunkReaderOptionsPtr Options;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
