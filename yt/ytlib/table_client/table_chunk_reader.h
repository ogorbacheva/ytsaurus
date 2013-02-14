#pragma once

#include "public.h"
#include "schema.h"
#include "key.h"

#include <ytlib/codecs/codec.h>
#include <ytlib/misc/blob_output.h>
#include <ytlib/misc/thread_affinity.h>
#include <ytlib/misc/async_stream_state.h>
#include <ytlib/table_client/table_reader.pb.h>
#include <ytlib/chunk_client/public.h>
#include <ytlib/ytree/public.h>
#include <ytlib/yson/lexer.h>
#include <ytlib/ytree/yson_string.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TTableChunkReaderProvider
    : public TRefCounted
{
public:
    TTableChunkReaderProvider(
        const NChunkClient::TSequentialReaderConfigPtr& config,
        const TReaderOptions& options = TReaderOptions());

    TTableChunkReaderPtr CreateNewReader(
        const NProto::TInputChunk& inputChunk,
        const NChunkClient::IAsyncReaderPtr& chunkReader);

    bool KeepInMemory() const;

private:
    NChunkClient::TSequentialReaderConfigPtr Config;
    TReaderOptions Options;

};

////////////////////////////////////////////////////////////////////////////////


//! Reads single table chunk row-after-row using given #NChunkClient::IAsyncReader.
class TTableChunkReader
    : public virtual TRefCounted
{
public:
    typedef TTableChunkReaderProvider TProvider;

    TTableChunkReader(
        NChunkClient::TSequentialReaderConfigPtr config,
        const TChannel& channel,
        NChunkClient::IAsyncReaderPtr chunkReader,
        const NProto::TReadLimit& startLimit,
        const NProto::TReadLimit& endLimit,
        const NYTree::TYsonString& rowAttributes,
        int partitionTag,
        TReaderOptions options);

    TAsyncError AsyncOpen();

    bool FetchNextItem();
    TAsyncError GetReadyEvent();

    bool IsValid() const;

    const TRow& GetRow() const;
    const TNonOwningKey& GetKey() const;
    const NYTree::TYsonString& GetRowAttributes() const;

    i64 GetRowIndex() const;
    i64 GetRowCount() const;
    TFuture<void> GetFetchingCompleteEvent();

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
    template <template <typename T> class TComparator>
    struct TIndexComparator;

    NChunkClient::TSequentialReaderPtr SequentialReader;
    TChannel Channel;

    bool DoNextRow();
    bool ContinueNextRow(
        int channelIndex,
        TError error);

    void MakeCurrentRow();
    void OnRowFetched(TError error);

    TColumnInfo& GetColumnInfo(const TStringBuf& column);

    class TInitializer;
    TIntrusivePtr<TInitializer> Initializer;

    class TRegularInitializer;
    class TPartitionInitializer;

    TAsyncStreamState ReaderState;
    TAsyncStreamState RowState;

    TReaderOptions Options;

    NYTree::TYsonString RowAttributes;
    TRow CurrentRow;
    TNonOwningKey CurrentKey;

    NYson::TLexer Lexer;

    yhash_map<TStringBuf, TColumnInfo> ColumnsMap;
    std::vector<Stroka> ColumnNames;

    i64 CurrentRowIndex;
    i64 StartRowIndex;
    i64 EndRowIndex;

    int PartitionTag;

    TCallback<void(TError)> OnRowFetchedCallback;

    THolder<TKeyValidator> EndValidator;

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

} // namespace NTableClient
} // namespace NYT
