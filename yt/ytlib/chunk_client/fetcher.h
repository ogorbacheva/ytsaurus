#pragma once

#include "public.h"

#include <yt/ytlib/node_tracker_client/public.h>

#include <yt/ytlib/api/native/public.h>

#include <yt/core/logging/log.h>

#include <yt/core/misc/error.h>
#include <yt/core/misc/guid.h>

#include <yt/core/rpc/public.h>

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

struct IFetcherChunkScraper
    : public virtual TRefCounted
{
    //! Returns future, which gets set when all chunks become available.
    virtual TFuture<void> ScrapeChunks(const THashSet<TInputChunkPtr>& chunkSpecs) = 0;

    //! Number of currently unavailable chunks.
    virtual i64 GetUnavailableChunkCount() const = 0;
};

IFetcherChunkScraperPtr CreateFetcherChunkScraper(
    const TChunkScraperConfigPtr config,
    const IInvokerPtr invoker,
    TThrottlerManagerPtr throttlerManager,
    NApi::NNative::IClientPtr client,
    NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
    const NLogging::TLogger& logger);

////////////////////////////////////////////////////////////////////////////////

struct IFetcher
    : public virtual TRefCounted
{
    virtual void AddChunk(TInputChunkPtr chunk) = 0;
    virtual int GetChunkCount() const = 0;
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
        IFetcherChunkScraperPtr chunkScraper,
        NApi::NNative::IClientPtr client,
        const NLogging::TLogger& logger);

    virtual void AddChunk(TInputChunkPtr chunk) override;
    virtual int GetChunkCount() const override;
    virtual TFuture<void> Fetch() override;

protected:
    const TFetcherConfigPtr Config_;
    const NNodeTrackerClient::TNodeDirectoryPtr NodeDirectory_;
    const IInvokerPtr Invoker_;
    const IFetcherChunkScraperPtr ChunkScraper_;
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
    NApi::NNative::IClientPtr Client_;

    //! Indexes of chunks for which no info is fetched yet.
    THashSet<int> UnfetchedChunkIndexes_;

    //! Ids of nodes that failed to reply.
    THashSet<NNodeTrackerClient::TNodeId> DeadNodes_;

    //! |(nodeId, chunkId)| pairs for which an error was returned from the node.
    std::set< std::pair<NNodeTrackerClient::TNodeId, TChunkId> > DeadChunks_;

    TPromise<void> Promise_ = NewPromise<void>();

    void OnFetchingRoundCompleted(const TError& error);
    void OnChunkLocated(const TChunkId& chunkId, const TChunkReplicaList& replicas);
};

DEFINE_REFCOUNTED_TYPE(IFetcherChunkScraper)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
