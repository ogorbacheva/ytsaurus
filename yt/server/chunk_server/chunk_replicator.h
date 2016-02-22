#pragma once

#include "private.h"
#include "chunk.h"
#include "chunk_replica.h"

#include <yt/server/cell_master/public.h>

#include <yt/ytlib/chunk_client/chunk_replica.h>

#include <yt/ytlib/node_tracker_client/node_tracker_service.pb.h>

#include <yt/core/concurrency/public.h>

#include <yt/core/erasure/public.h>

#include <yt/core/misc/error.h>
#include <yt/core/misc/nullable.h>
#include <yt/core/misc/property.h>

#include <yt/core/profiling/timing.h>

#include <deque>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunkReplicator
    : public TRefCounted
{
public:
    TChunkReplicator(
        TChunkManagerConfigPtr config,
        NCellMaster::TBootstrap* bootstrap,
        TChunkPlacementPtr chunkPlacement);

    void Start();
    void Stop();

    void OnNodeRegistered(TNode* node);
    void OnNodeUnregistered(TNode* node);
    void OnNodeDisposed(TNode* node);

    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, LostChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, LostVitalChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, UnderreplicatedChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, OverreplicatedChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, DataMissingChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, ParityMissingChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, QuorumMissingChunks);
    DEFINE_BYREF_RO_PROPERTY(yhash_set<TChunk*>, UnsafelyPlacedChunks);

    void OnChunkDestroyed(TChunk* chunk);
    void OnReplicaRemoved(TNode* node, TChunkPtrWithIndex chunkWithIndex, ERemoveReplicaReason reason);

    void ScheduleChunkRefresh(const TChunkId& chunkId);
    void ScheduleChunkRefresh(TChunk* chunk);
    void ScheduleNodeRefresh(TNode* node);

    void ScheduleUnknownReplicaRemoval(TNode* node, const NChunkClient::TChunkIdWithIndex& chunkdIdWithIndex);
    void ScheduleReplicaRemoval(TNode* node, TChunkPtrWithIndex chunkWithIndex);

    void SchedulePropertiesUpdate(TChunkTree* chunkTree);
    void SchedulePropertiesUpdate(TChunk* chunk);
    void SchedulePropertiesUpdate(TChunkList* chunkList);

    void TouchChunk(TChunk* chunk);

    TJobPtr FindJob(const TJobId& id);

    EChunkStatus ComputeChunkStatus(TChunk* chunk);

    void ScheduleJobs(
        TNode* node,
        const std::vector<TJobPtr>& currentJobs,
        std::vector<TJobPtr>* jobsToStart,
        std::vector<TJobPtr>* jobsToAbort,
        std::vector<TJobPtr>* jobsToRemove);

    bool IsEnabled();

    int GetRefreshListSize() const;
    int GetPropertiesUpdateListSize() const;

private:
    struct TChunkStatistics
    {
        TChunkStatistics();

        EChunkStatus Status;

        //! Number of active replicas, per each replica index.
        int ReplicaCount[NChunkClient::ChunkReplicaIndexBound];
        
        //! Number of decommissioned replicas, per each replica index.
        int DecommissionedReplicaCount[NChunkClient::ChunkReplicaIndexBound];

        //! Indexes of replicas whose replication is advised.
        SmallVector<int, TypicalReplicaCount> ReplicationIndexes;
        
        //! Decommissioned replicas whose removal is advised.
        SmallVector<TNodePtrWithIndex, TypicalReplicaCount> DecommissionedRemovalReplicas;

        //! Indexes of replicas whose removal is advised for balancing.
        SmallVector<int, TypicalReplicaCount> BalancingRemovalIndexes;
    };

    struct TRefreshEntry
    {
        TChunk* Chunk = nullptr;
        NProfiling::TCpuInstant When;
    };

    const TChunkManagerConfigPtr Config_;
    NCellMaster::TBootstrap* const Bootstrap_;
    const TChunkPlacementPtr ChunkPlacement_;

    NProfiling::TCpuDuration ChunkRefreshDelay_;

    NConcurrency::TPeriodicExecutorPtr RefreshExecutor_;
    std::deque<TRefreshEntry> RefreshList_;

    NConcurrency::TPeriodicExecutorPtr PropertiesUpdateExecutor_;
    std::deque<TChunk*> PropertiesUpdateList_;

    yhash_map<TJobId, TJobPtr> JobMap_;

    TChunkRepairQueue ChunkRepairQueue_;

    NConcurrency::TPeriodicExecutorPtr EnabledCheckExecutor_;
    bool Enabled_ = false;


    void ProcessExistingJobs(
        TNode* node,
        const std::vector<TJobPtr>& currentJobs,
        std::vector<TJobPtr>* jobsToAbort,
        std::vector<TJobPtr>* jobsToRemove);

    TJobId GenerateJobId();
    bool CreateReplicationJob(
        TNode* sourceNode,
        TChunkPtrWithIndex chunkWithIndex,
        TJobPtr* job);
    bool CreateBalancingJob(
        TNode* sourceNode,
        TChunkPtrWithIndex chunkWithIndex,
        double maxFillCoeff,
        TJobPtr* jobsToStart);
    bool CreateRemovalJob(
        TNode* node,
        const NChunkClient::TChunkIdWithIndex& chunkIdWithIndex,
        TJobPtr* job);
    bool CreateRepairJob(
        TNode* node,
        TChunk* chunk,
        TJobPtr* job);
    bool CreateSealJob(
        TNode* node,
        TChunk* chunk,
        TJobPtr* job);
    void ScheduleNewJobs(
        TNode* node,
        std::vector<TJobPtr>* jobsToStart,
        std::vector<TJobPtr>* jobsToAbort);

    void OnRefresh();
    void RefreshChunk(TChunk* chunk);

    void ResetChunkStatus(TChunk* chunk);
    void RemoveChunkFromQueues(TChunk* chunk, bool dropRemovals);
    void RemoveReplicaFromQueues(TChunk* chunk, TNodePtrWithIndex nodeWithIndex, bool dropRemovals);
    void CancelChunkJobs(TChunk* chunk);

    TChunkStatistics ComputeChunkStatistics(TChunk* chunk);
    TChunkStatistics ComputeRegularChunkStatistics(TChunk* chunk);
    TChunkStatistics ComputeErasureChunkStatistics(TChunk* chunk);
    TChunkStatistics ComputeJournalChunkStatistics(TChunk* chunk);

    bool IsReplicaDecommissioned(TNodePtrWithIndex replica);

    void OnPropertiesUpdate();

    //! Computes the actual properties the chunk must have.
    TChunkProperties ComputeChunkProperties(TChunk* chunk);

    //! Follows upward parent links.
    //! Stops when some owning nodes are discovered or parents become ambiguous.
    TChunkList* FollowParentLinks(TChunkList* chunkList);

    void RegisterJob(TJobPtr job);

    void UnregisterJob(TJobPtr job, EJobUnregisterFlags flags = EJobUnregisterFlags::All);

    void AddToChunkRepairQueue(TChunk* chunk);
    void RemoveFromChunkRepairQueue(TChunk* chunk);

    void OnCheckEnabled();
    void OnCheckEnabledPrimary();
    void OnCheckEnabledSecondary();

};

DEFINE_REFCOUNTED_TYPE(TChunkReplicator)

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
