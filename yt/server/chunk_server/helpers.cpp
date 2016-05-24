#include "helpers.h"
#include "chunk_owner_base.h"

#include <yt/server/cypress_server/cypress_manager.h>

#include <yt/ytlib/object_client/public.h>

#include <yt/ytlib/table_client/chunk_meta_extensions.h>
#include <yt/ytlib/table_client/unversioned_row.h>

#include <yt/core/misc/common.h>

#include <yt/core/ytree/fluent.h>

namespace NYT {
namespace NChunkServer {

using namespace NYTree;
using namespace NYson;
using namespace NObjectClient;
using namespace NCypressServer;
using namespace NTableClient;
using namespace NTableClient::NProto;

////////////////////////////////////////////////////////////////////////////////

void SetChunkTreeParent(TChunkList* parent, TChunkTree* child)
{
    switch (child->GetType()) {
        case EObjectType::Chunk:
        case EObjectType::ErasureChunk:
        case EObjectType::JournalChunk:
            child->AsChunk()->Parents().push_back(parent);
            break;
        case EObjectType::ChunkList:
            child->AsChunkList()->Parents().insert(parent);
            break;
        default:
            YUNREACHABLE();
    }
}

void ResetChunkTreeParent(TChunkList* parent, TChunkTree* child)
{
    switch (child->GetType()) {
        case EObjectType::Chunk:
        case EObjectType::ErasureChunk:
        case EObjectType::JournalChunk: {
            auto& parents = child->AsChunk()->Parents();
            auto it = std::find(parents.begin(), parents.end(), parent);
            Y_ASSERT(it != parents.end());
            parents.erase(it);
            break;
        }
        case EObjectType::ChunkList: {
            auto& parents = child->AsChunkList()->Parents();
            auto it = parents.find(parent);
            Y_ASSERT(it != parents.end());
            parents.erase(it);
            break;
        }
        default:
            YUNREACHABLE();
    }
}

TChunkTreeStatistics GetChunkTreeStatistics(TChunkTree* chunkTree)
{
    switch (chunkTree->GetType()) {
        case EObjectType::Chunk:
        case EObjectType::ErasureChunk:
        case EObjectType::JournalChunk:
            return chunkTree->AsChunk()->GetStatistics();
        case EObjectType::ChunkList:
            return chunkTree->AsChunkList()->Statistics();
        default:
            YUNREACHABLE();
    }
}

void AccumulateChildStatistics(
    TChunkList* chunkList,
    TChunkTree* child,
    TChunkTreeStatistics* statistics)
{
    if (!chunkList->Children().empty()) {
        chunkList->RowCountSums().push_back(
            chunkList->Statistics().RowCount +
            statistics->RowCount);
        chunkList->ChunkCountSums().push_back(
            chunkList->Statistics().ChunkCount +
            statistics->ChunkCount);
        chunkList->DataSizeSums().push_back(
            chunkList->Statistics().UncompressedDataSize +
            statistics->UncompressedDataSize);

    }
    statistics->Accumulate(GetChunkTreeStatistics(child));
}

void AccumulateUniqueAncestorsStatistics(
    TChunkList* chunkList,
    const TChunkTreeStatistics& statisticsDelta)
{
    auto mutableStatisticsDelta = statisticsDelta;
    VisitUniqueAncestors(
        chunkList,
        [&] (TChunkList* current) {
            ++mutableStatisticsDelta.Rank;
            current->Statistics().Accumulate(mutableStatisticsDelta);
        });
}

void ResetChunkListStatistics(TChunkList* chunkList)
{
    chunkList->RowCountSums().clear();
    chunkList->ChunkCountSums().clear();
    chunkList->DataSizeSums().clear();
    chunkList->Statistics() = TChunkTreeStatistics();
    chunkList->Statistics().ChunkListCount = 1;
    chunkList->Statistics().Rank = 1;
}

void RecomputeChunkListStatistics(TChunkList* chunkList)
{
    ResetChunkListStatistics(chunkList);

    std::vector<TChunkTree*> children;
    children.swap(chunkList->Children());

    TChunkTreeStatistics statistics;
    for (auto* child : children) {
        AccumulateChildStatistics(chunkList, child, &statistics);
        chunkList->Children().push_back(child);
    }

    ++statistics.Rank;
    chunkList->Statistics() = statistics;
}

void VisitOwningNodes(
    TChunkTree* chunkTree,
    yhash_set<TChunkTree*>* visitedTrees,
    yhash_set<TChunkOwnerBase*>* owningNodes)
{
    if (!visitedTrees->insert(chunkTree).second)
        return;
    
    switch (chunkTree->GetType()) {
        case EObjectType::Chunk:
        case EObjectType::ErasureChunk:
        case EObjectType::JournalChunk: {
            for (auto* parent : chunkTree->AsChunk()->Parents()) {
                VisitOwningNodes(parent, visitedTrees, owningNodes);
            }
            break;
        }
        case EObjectType::ChunkList: {
            auto* chunkList = chunkTree->AsChunkList();
            owningNodes->insert(chunkList->OwningNodes().begin(), chunkList->OwningNodes().end());
            for (auto* parent : chunkList->Parents()) {
                VisitOwningNodes(parent, visitedTrees, owningNodes);
            }
            break;
        }
        default:
            YUNREACHABLE();
    }
}

std::vector<TChunkOwnerBase*> GetOwningNodes(
    TChunkTree* chunkTree)
{
    yhash_set<TChunkOwnerBase*> owningNodes;
    yhash_set<TChunkTree*> visitedTrees;
    VisitOwningNodes(chunkTree, &visitedTrees, &owningNodes);
    return std::vector<TChunkOwnerBase*>(owningNodes.begin(), owningNodes.end());
}

void SerializeOwningNodesPaths(
    TCypressManagerPtr cypressManager,
    TChunkTree* chunkTree,
    IYsonConsumer* consumer)
{
    yhash_set<TChunkOwnerBase*> owningNodes;
    yhash_set<TChunkTree*> visitedTrees;
    VisitOwningNodes(chunkTree, &visitedTrees, &owningNodes);

    BuildYsonFluently(consumer)
        .DoListFor(owningNodes, [&] (TFluentList fluent, TChunkOwnerBase* node) {
            auto proxy = cypressManager->GetNodeProxy(
                node->GetTrunkNode(),
                node->GetTransaction());
            auto path = proxy->GetPath();
            if (node->GetTransaction()) {
                fluent.Item()
                    .BeginAttributes()
                        .Item("transaction_id").Value(node->GetTransaction()->GetId())
                    .EndAttributes()
                    .Value(path);
            } else {
                fluent.Item()
                    .Value(path);
            }
        });
}

////////////////////////////////////////////////////////////////////////////////

TOwningKey GetMaxKey(const TChunk* chunk)
{
    TOwningKey key;
    auto chunkFormat = ETableChunkFormat(chunk->ChunkMeta().version());
    if (chunkFormat == ETableChunkFormat::Old) {
        // Deprecated chunks.
        auto boundaryKeysExt = GetProtoExtension<TOldBoundaryKeysExt>(
            chunk->ChunkMeta().extensions());
        FromProto(&key, boundaryKeysExt.end());
    } else {
        auto boundaryKeysExt = GetProtoExtension<TBoundaryKeysExt>(
            chunk->ChunkMeta().extensions());
        FromProto(&key, boundaryKeysExt.max());
    }

    return GetKeySuccessor(key.Get());
}

TOwningKey GetMaxKey(const TChunkList* chunkList)
{
    const auto& children = chunkList->Children();
    Y_ASSERT(!children.empty());
    return GetMaxKey(children.back());
}

TOwningKey GetMaxKey(const TChunkTree* chunkTree)
{
    switch (chunkTree->GetType()) {
        case EObjectType::Chunk:
        case EObjectType::ErasureChunk:
            return GetMaxKey(chunkTree->AsChunk());

        case EObjectType::ChunkList:
            return GetMaxKey(chunkTree->AsChunkList());

        default:
            YUNREACHABLE();
    }
}

TOwningKey GetMinKey(const TChunk* chunk)
{
    TOwningKey key;
    auto chunkFormat = ETableChunkFormat(chunk->ChunkMeta().version());
    if (chunkFormat == ETableChunkFormat::Old) {
        // Deprecated chunks.
        auto boundaryKeysExt = GetProtoExtension<TOldBoundaryKeysExt>(
            chunk->ChunkMeta().extensions());
        FromProto(&key, boundaryKeysExt.start());
    } else {
        auto boundaryKeysExt = GetProtoExtension<TBoundaryKeysExt>(
            chunk->ChunkMeta().extensions());
        FromProto(&key, boundaryKeysExt.min());
    }

    return key;
}

TOwningKey GetMinKey(const TChunkList* chunkList)
{
    const auto& children = chunkList->Children();
    Y_ASSERT(!children.empty());
    return GetMinKey(children.front());
}

TOwningKey GetMinKey(const TChunkTree* chunkTree)
{
    switch (chunkTree->GetType()) {
        case EObjectType::Chunk:
        case EObjectType::ErasureChunk:
            return GetMinKey(chunkTree->AsChunk());

        case EObjectType::ChunkList:
            return GetMinKey(chunkTree->AsChunkList());

        default:
            YUNREACHABLE();
    }
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
