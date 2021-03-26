#pragma once

#include "public.h"

#include <yt/yt/server/master/chunk_server/chunk_replica.h>

#include <yt/yt/server/master/table_server/shared_table_schema.h>

#include <yt/yt/server/lib/chunk_server/proto/job.pb.h>

#include <yt/yt/client/chunk_client/chunk_replica.h>

#include <yt/yt/client/node_tracker_client/proto/node.pb.h>

#include <yt/yt/library/erasure/public.h>

#include <yt/yt/core/misc/error.h>
#include <yt/yt/core/misc/property.h>

namespace NYT::NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TJob
    : public TRefCounted
{
public:
    DEFINE_BYVAL_RO_PROPERTY(TJobId, JobId);
    DEFINE_BYVAL_RO_PROPERTY(EJobType, Type);
    DEFINE_BYVAL_RO_PROPERTY(bool, Decommission);

    //! Could be null for removal jobs issued against non-existing chunks.
    DEFINE_BYVAL_RO_PROPERTY(TChunk*, Chunk);
    //! Can't make it TChunkPtrWithIndexes since removal jobs may refer to nonexistent chunks.
    DEFINE_BYVAL_RO_PROPERTY(NChunkClient::TChunkIdWithIndexes, ChunkIdWithIndexes);
    DEFINE_BYVAL_RO_PROPERTY(NNodeTrackerServer::TNode*, Node);
    DEFINE_BYREF_RO_PROPERTY(TNodePtrWithIndexesList, TargetReplicas);
    DEFINE_BYVAL_RO_PROPERTY(TInstant, StartTime);
    DEFINE_BYREF_RO_PROPERTY(NNodeTrackerClient::NProto::TNodeResources, ResourceUsage);
    using TChunkVector = SmallVector<TChunk*, 16>;
    DEFINE_BYVAL_RO_PROPERTY(TChunkVector, Chunks);
    DEFINE_BYREF_RO_PROPERTY(NChunkClient::NProto::TChunkMergerWriterOptions, ChunkMergerWriterOptions);

    //! Current state (as reported by node).
    DEFINE_BYVAL_RW_PROPERTY(EJobState, State);
    //! Failure reason (as reported by node).
    DEFINE_BYREF_RW_PROPERTY(TError, Error);

public:
    static TJobPtr CreateReplicate(
        TJobId jobId,
        TChunkPtrWithIndexes chunkWithIndexes,
        NNodeTrackerServer::TNode* node,
        const TNodePtrWithIndexesList& targetReplicas);

    static TJobPtr CreateRemove(
        TJobId jobId,
        TChunk* chunk,
        const NChunkClient::TChunkIdWithIndexes& chunkIdWithIndexes,
        NNodeTrackerServer::TNode* node);

    static TJobPtr CreateRepair(
        TJobId jobId,
        TChunk* chunk,
        NNodeTrackerServer::TNode* node,
        const TNodePtrWithIndexesList& targetReplicas,
        i64 memoryUsage,
        bool decommission);

    static TJobPtr CreateSeal(
        TJobId jobId,
        TChunkPtrWithIndexes chunkWithIndexes,
        NNodeTrackerServer::TNode* node);

    static TJobPtr CreateMerge(
        TJobId jobId,
        TChunkId chunkId,
        int mediumIndex,
        TChunkVector chunks,
        NNodeTrackerServer::TNode* node,
        NChunkClient::NProto::TChunkMergerWriterOptions chunkMergerWriterOptions);

private:
    TJob(
        EJobType type,
        TJobId jobId,
        TChunk* chunk,
        const NChunkClient::TChunkIdWithIndexes& chunkIdWithIndexes,
        NNodeTrackerServer::TNode* node,
        const TNodePtrWithIndexesList& targetReplicas,
        TInstant startTime,
        const NNodeTrackerClient::NProto::TNodeResources& resourceUsage,
        TChunkVector chunks,
        NChunkClient::NProto::TChunkMergerWriterOptions chunkMergerWriterOptions,
        bool decommission = false);
    DECLARE_NEW_FRIEND();

};

DEFINE_REFCOUNTED_TYPE(TJob)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkServer
