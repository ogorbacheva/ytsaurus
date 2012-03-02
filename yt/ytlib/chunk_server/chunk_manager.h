#pragma once

#include "common.h"
#include "config.h"
#include "holder.h"
#include "chunk.h"
#include "chunk_list.h"
#include "job.h"
#include "job_list.h"
#include "chunk_service_proxy.h"
#include "holder_authority.h"
#include "chunk_manager.pb.h"
#include "holder_statistics.h"

#include <ytlib/meta_state/composite_meta_state.h>
#include <ytlib/meta_state/meta_change.h>
#include <ytlib/transaction_server/transaction_manager.h>
#include <ytlib/object_server/object_manager.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunkManager
    : public TRefCounted
{
public:
    typedef TIntrusivePtr<TChunkManager> TPtr;
    typedef TChunkManagerConfig TConfig;
    typedef NProto::TReqHolderHeartbeat::TJobInfo TJobInfo;
    typedef NProto::TRspHolderHeartbeat::TJobStartInfo TJobStartInfo;

    //! Creates an instance.
    TChunkManager(
        TConfig* config,
        NMetaState::IMetaStateManager* metaStateManager,
        NMetaState::TCompositeMetaState* metaState,
        NTransactionServer::TTransactionManager* transactionManager,
        IHolderAuthority* holderAuthority,
        NObjectServer::TObjectManager* objectManager);

    NObjectServer::TObjectManager* GetObjectManager() const;

    NMetaState::TMetaChange< yvector<TChunkId> >::TPtr InitiateCreateChunks(
        const NProto::TMsgCreateChunks& message);

    NMetaState::TMetaChange<THolderId>::TPtr InitiateRegisterHolder(
        const NProto::TMsgRegisterHolder& message);

    NMetaState::TMetaChange<TVoid>::TPtr  InitiateUnregisterHolder(
        const NProto::TMsgUnregisterHolder& message);

    NMetaState::TMetaChange<TVoid>::TPtr InitiateHeartbeatRequest(
        const NProto::TMsgHeartbeatRequest& message);

    NMetaState::TMetaChange<TVoid>::TPtr InitiateHeartbeatResponse(
        const NProto::TMsgHeartbeatResponse& message);

    DECLARE_METAMAP_ACCESSORS(Chunk, TChunk, TChunkId);
    DECLARE_METAMAP_ACCESSORS(ChunkList, TChunkList, TChunkListId);
    DECLARE_METAMAP_ACCESSORS(Holder, THolder, THolderId);
    DECLARE_METAMAP_ACCESSORS(JobList, TJobList, TChunkId);
    DECLARE_METAMAP_ACCESSORS(Job, TJob, TJobId);

    //! Fired when a holder gets registered.
    /*!
     *  \note
     *  Only fired for leaders, not fired during recovery.
     */
    DECLARE_BYREF_RW_PROPERTY(TParamActionList<const THolder&>, HolderRegistered);
    //! Fired when a holder gets unregistered.
    /*!
     *  \note
     *  Only fired for leaders, not fired during recovery.
     */
    DECLARE_BYREF_RW_PROPERTY(TParamActionList<const THolder&>, HolderUnregistered);

    const THolder* FindHolder(const Stroka& address) const;
    THolder* FindHolder(const Stroka& address);
    const TReplicationSink* FindReplicationSink(const Stroka& address);

    yvector<THolderId> AllocateUploadTargets(int replicaCount);

    TChunk& CreateChunk();
    TChunkList& CreateChunkList();

    void AttachToChunkList(TChunkList& chunkList, const yvector<TChunkTreeId>& childrenIds);
    void DetachFromChunkList(TChunkList& chunkList, const yvector<TChunkTreeId>& childrenIds);

    void RunJobControl(
        const THolder& holder,
        const yvector<TJobInfo>& runningJobs,
        yvector<TJobStartInfo>* jobsToStart,
        yvector<TJobId>* jobsToStop);

    //! Fills a given protobuf structure with the list of holder addresses.
    /*!
     *  Not too nice but seemingly fast.
     */
    void FillHolderAddresses(
        ::google::protobuf::RepeatedPtrField< TProtoStringType>* addresses,
        const TChunk& chunk);

    const yhash_set<TChunkId>& LostChunkIds() const;
    const yhash_set<TChunkId>& OverreplicatedChunkIds() const;
    const yhash_set<TChunkId>& UnderreplicatedChunkIds() const;

    TTotalHolderStatistics GetTotalHolderStatistics() const;

private:
    class TImpl;
    class TChunkTypeHandler;
    class TChunkProxy;
    class TChunkListTypeHandler;
    class TChunkListProxy;
    
    TIntrusivePtr<TImpl> Impl;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
