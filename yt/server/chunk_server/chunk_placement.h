#pragma once

#include "public.h"
#include "chunk_replica.h"

#include <core/misc/small_vector.h>
#include <core/misc/small_set.h>
#include <core/misc/nullable.h>

#include <server/node_tracker_server/node_tracker.h>

#include <server/cell_master/public.h>

#include <util/generic/map.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunkPlacement
    : public TRefCounted
{
public:
    TChunkPlacement(
        TChunkManagerConfigPtr config,
        NCellMaster::TBootstrap* bootstrap);

    void Initialize();

    void OnNodeRegistered(TNode* node);
    void OnNodeUnregistered(TNode* node);
    void OnNodeUpdated(TNode* node);

    double GetFillFactor(TNode* node) const;

    TNodeList AllocateWriteTargets(
        int replicaCount,
        const TNodeSet* forbiddenNodes,
        const TNullable<Stroka>& preferredHostName,
        NChunkClient::EWriteSessionType sessionType);

    TNodeList AllocateWriteTargets(
        TChunk* chunk,
        int targetCount,
        NChunkClient::EWriteSessionType sessionType);

    TNodeList GetRemovalTargets(
        TChunkPtrWithIndex chunkWithIndex,
        int replicaCount);

    bool HasBalancingTargets(double maxFillFactor);

    std::vector<TChunkPtrWithIndex> GetBalancingChunks(
        TNode* node,
        int replicaCount);

    TNode* AllocateBalancingTarget(
        TChunkPtrWithIndex chunkWithIndex,
        double maxFillFactor);

private:
    typedef ymultimap<double, TNode*> TFactorToNode;
    typedef yhash_map<TNode*, TFactorToNode::iterator> TNodeToFactorIt;

    TChunkManagerConfigPtr Config;
    NCellMaster::TBootstrap* Bootstrap;

    std::vector<TNode*> LoadRankToNode;

    TFactorToNode FillFactorToNode;
    TNodeToFactorIt NodeToFillFactorIt;

    static int GetLoadFactor(TNode* node);

    TNodeList GetWriteTargets(
        int targetCount,
        const TNodeSet* forbiddenNodes,
        const TNullable<Stroka>& preferredHostName,
        NChunkClient::EWriteSessionType sessionType);

    TNodeList GetWriteTargets(
        TChunk* chunk,
        int targetCount,
        NChunkClient::EWriteSessionType sessionType);

    TNode* GetBalancingTarget(
        TChunkPtrWithIndex chunkWithIndex,
        double maxFillFactor);

    static bool IsFull(TNode* node);

    static bool IsValidWriteTarget(
        TNode* node,
        NChunkClient::EWriteSessionType sessionType);
    
    bool IsValidBalancingTarget(
        TNode* node,
        TChunkPtrWithIndex chunkWithIndex) const;
    
    bool IsValidRemovalTarget(TNode* node);

    void AddSessionHint(
        TNode* node,
        NChunkClient::EWriteSessionType sessionType);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
