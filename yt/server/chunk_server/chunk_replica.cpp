#include "chunk_replica.h"
#include "chunk.h"

#include <yt/server/node_tracker_server/node.h>

#include <yt/ytlib/chunk_client/chunk_replica.h>
#include <yt/ytlib/chunk_client/public.h>

#include <yt/core/misc/common.h>
#include <yt/core/misc/string.h>

namespace NYT {
namespace NChunkServer {

using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(TNodePtrWithIndex value)
{
    return Format("%v/%v",
        value.GetPtr()->GetDefaultAddress(),
        value.GetIndex());
}

Stroka ToString(TChunkPtrWithIndex value)
{
    auto* chunk = value.GetPtr();
    int index = value.GetIndex();
    if (chunk->IsErasure() && index != GenericChunkReplicaIndex) {
        return Format("%v/%v",
            chunk->GetId(),
            index);
    } else if (chunk->IsJournal()) {
        return Format("%v/%v",
            chunk->GetId(),
            EJournalReplicaType(index));
    } else {
        return ToString(chunk->GetId());
    }
}

void ToProto(ui32* protoValue, TNodePtrWithIndex value)
{
    NChunkClient::TChunkReplica clientReplica(
        value.GetPtr()->GetId(),
        value.GetIndex());
    NChunkClient::ToProto(protoValue, clientReplica);
}

TChunkId EncodeChunkId(TChunkPtrWithIndex chunkWithIndex)
{
    auto* chunk = chunkWithIndex.GetPtr();
    return chunk->IsErasure()
        ? ErasurePartIdFromChunkId(chunk->GetId(), chunkWithIndex.GetIndex())
        : chunk->GetId();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
