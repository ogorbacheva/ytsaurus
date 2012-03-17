#pragma once

#include "public.h"

#include <ytlib/actions/invoker.h>
#include <ytlib/misc/guid.h>
#include <ytlib/chunk_server/public.h>
#include <ytlib/chunk_holder/public.h>
// TODO(babenko): replace with public.h
#include <ytlib/bus/server.h>
// TODO(babenko): replace with public.h
#include <ytlib/rpc/channel.h>

namespace NYT {
namespace NCellNode {

////////////////////////////////////////////////////////////////////////////////

class TBootstrap
{
public:
    TBootstrap(
        const Stroka& configFileName,
        TCellNodeConfigPtr config);
    ~TBootstrap();

    TCellNodeConfigPtr GetConfig() const;
    NChunkServer::TIncarnationId GetIncarnationId() const;
    NChunkHolder::TChunkStorePtr GetChunkStore() const;
    NChunkHolder::TChunkCachePtr GetChunkCache() const;
    NChunkHolder::TSessionManagerPtr GetSessionManager() const;
    NChunkHolder::TJobExecutorPtr GetJobExecutor() const;
    IInvoker::TPtr GetControlInvoker() const;
    NChunkHolder::TBlockStorePtr GetBlockStore();
    NBus::IBusServer::TPtr GetBusServer() const;
    NChunkHolder::TPeerBlockTablePtr GetPeerBlockTable() const;
    NChunkHolder::TReaderCachePtr GetReaderCache() const;
    NRpc::IChannel::TPtr GetLeaderChannel() const;
    Stroka GetPeerAddress() const;

    void Run();

private:
    Stroka ConfigFileName;
    TCellNodeConfigPtr Config;
    
    NChunkServer::TIncarnationId IncarnationId;
    NChunkHolder::TChunkStorePtr ChunkStore;
    NChunkHolder::TChunkCachePtr ChunkCache;
    NChunkHolder::TSessionManagerPtr SessionManager;
    NChunkHolder::TJobExecutorPtr JobExecutor;
    IInvoker::TPtr ControlInvoker;
    NChunkHolder::TBlockStorePtr BlockStore;
    NBus::IBusServer::TPtr BusServer;
    NChunkHolder::TPeerBlockTablePtr PeerBlockTable;
    NChunkHolder::TReaderCachePtr ReaderCache;
    NRpc::IChannel::TPtr LeaderChannel;
    Stroka PeerAddress;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellNode
} // namespace NYT
