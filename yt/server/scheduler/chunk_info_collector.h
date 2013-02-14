#pragma once

#include "public.h"

#include <ytlib/misc/error.h>

#include <ytlib/table_client/helpers.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

template <class TFetcher>
class TChunkInfoCollector
    : public TRefCounted
{
public:
    typedef TIntrusivePtr<TFetcher> TFetcherPtr;

    TChunkInfoCollector(
        TFetcherPtr fetcher,
        IInvokerPtr invoker);

    void AddChunk(NTableClient::TRefCountedInputChunkPtr chunk);
    TFuture< TValueOrError<void> > Run();

private:
    TFetcherPtr Fetcher;
    IInvokerPtr Invoker;

    TPromise< TValueOrError<void> > Promise;

    //! All chunks for which info is to be fetched.
    std::vector<NTableClient::TRefCountedInputChunkPtr> Chunks;

    //! Indexes of chunks for which no info is fetched yet.
    yhash_set<int> UnfetchedChunkIndexes;

    //! Addresses of nodes that failed to reply.
    yhash_set<Stroka> DeadNodes;

    //! |(address, chunkId)| pairs for which an error was returned from the node.
    // XXX(babenko): need to specialize hash to use yhash_set
    std::set< TPair<Stroka, NChunkClient::TChunkId> > DeadChunks;

    void SendRequests();
    void OnResponse(
        const Stroka& address,
        std::vector<int> chunkIndexes,
        typename TFetcher::TResponsePtr rsp);
    void OnEndRound();
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

#define CHUNK_INFO_COLLECTOR_INL_H_
#include "chunk_info_collector-inl.h"
#undef CHUNK_INFO_COLLECTOR_INL_H_