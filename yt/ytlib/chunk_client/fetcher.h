#pragma once

#include "public.h"

#include <yt/ytlib/node_tracker_client/public.h>

#include <yt/ytlib/api/public.h>

#include <yt/core/logging/log.h>

#include <yt/core/misc/error.h>
#include <yt/core/misc/guid.h>

#include <yt/core/rpc/public.h>

namespace NYT {
namespace NChunkClient {

////////////////////////////////////////////////////////////////////////////////

using TScrapeChunksCallback = TCallback<TFuture<void>(const yhash_set<TInputChunkPtr>& chunkSpecs)>;

TScrapeChunksCallback CreateScrapeChunksSessionCallback(
    const TChunkScraperConfigPtr config,
    const IInvokerPtr invoker,
    TThrottlerManagerPtr throttlerManager,
    NApi::INativeClientPtr client,
    NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
    const NLogging::TLogger& logger);

////////////////////////////////////////////////////////////////////////////////

struct IFetcher
    : public TRefCounted
{
    virtual void AddChunk(TInputChunkPtr chunk) = 0;

    virtual TFuture<void> Fetch() = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TFetcherBase
    : public virtual IFetcher
{
public:
    TFetcherBase(
        TFetcherConfigPtr config,
        NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
        IInvokerPtr invoker,
        NTableClient::TRowBufferPtr rowBuffer,
        TScrapeChunksCallback scraperCallback,
        NApi::INativeClientPtr client,
        const NLogging::TLogger& logger);

    virtual void AddChunk(TInputChunkPtr chunk) override;
    virtual TFuture<void> Fetch() override;

protected:
    const TFetcherConfigPtr Config_;
    const NNodeTrackerClient::TNodeDirectoryPtr NodeDirectory_;
    const IInvokerPtr Invoker_;
    const NTableClient::TRowBufferPtr RowBuffer_;
    const TScrapeChunksCallback ScraperCallback_;
    const NLogging::TLogger Logger;

    //! All chunks for which info is to be fetched.
    std::vector<TInputChunkPtr> Chunks_;

    virtual TFuture<void> FetchFromNode(
        NNodeTrackerClient::TNodeId nodeId,
        std::vector<int> chunkIndexes) = 0;

    virtual void OnFetchingCompleted();

    NRpc::IChannelPtr GetNodeChannel(NNodeTrackerClient::TNodeId nodeId);

    void StartFetchingRound();

    void OnChunkFailed(
        NNodeTrackerClient::TNodeId nodeId,
        int chunkIndex,
        const TError& error);
    void OnNodeFailed(
        NNodeTrackerClient::TNodeId nodeId,
        const std::vector<int>& chunkIndexes);

private:
    NApi::INativeClientPtr Client_;

    //! Indexes of chunks for which no info is fetched yet.
    yhash_set<int> UnfetchedChunkIndexes_;

    //! Ids of nodes that failed to reply.
    yhash_set<NNodeTrackerClient::TNodeId> DeadNodes_;

    //! |(nodeId, chunkId)| pairs for which an error was returned from the node.
    std::set< std::pair<NNodeTrackerClient::TNodeId, TChunkId> > DeadChunks_;

    TPromise<void> Promise_ = NewPromise<void>();


    void OnFetchingRoundCompleted(const TError& error);
    void OnChunkLocated(const TChunkId& chunkId, const TChunkReplicaList& replicas);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
