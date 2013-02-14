#pragma once

#include "public.h"
#include "chunk_tree.h"

#include <ytlib/misc/property.h>
#include <ytlib/misc/small_vector.h>

#include <ytlib/chunk_client/chunk.pb.h>

#include <ytlib/table_client/table_chunk_meta.pb.h>

#include <server/cell_master/public.h>

#include <server/object_server/object_detail.h>

#include <server/security_server/cluster_resources.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunk
    : public TChunkTree
    , public NObjectServer::TStagedObject
{
    DEFINE_BYREF_RW_PROPERTY(NChunkClient::NProto::TChunkMeta, ChunkMeta);
    DEFINE_BYREF_RW_PROPERTY(NChunkClient::NProto::TChunkInfo, ChunkInfo);
    DEFINE_BYVAL_RW_PROPERTY(i16, ReplicationFactor);
    DEFINE_BYVAL_RW_PROPERTY(bool, Movable);
    DEFINE_BYVAL_RW_PROPERTY(bool, Vital);

    typedef TSmallVector<TChunkList*, TypicalChunkParentCount> TParents;
    DEFINE_BYREF_RW_PROPERTY(TParents, Parents);

    // This is usually small, e.g. has the length of 3.
    typedef TSmallVector<TNodeId, TypicalReplicationFactor> TStoredLocations;
    DEFINE_BYREF_RO_PROPERTY(TStoredLocations, StoredLocations);

    // This list is usually empty.
    // Keeping a holder is very space efficient (takes just 8 bytes).
    DEFINE_BYREF_RO_PROPERTY(::THolder< yhash_set<TNodeId> >, CachedLocations);

public:
    static const i64 UnknownSize;

    explicit TChunk(const TChunkId& id);
    ~TChunk();

    TChunkTreeStatistics GetStatistics() const;
    NSecurityServer::TClusterResources GetResourceUsage() const;

    void Save(const NCellMaster::TSaveContext& context) const;
    void Load(const NCellMaster::TLoadContext& context);

    void AddLocation(TNodeId nodeId, bool cached);
    void RemoveLocation(TNodeId nodeId, bool cached);
    TSmallVector<TNodeId, TypicalReplicationFactor> GetLocations() const;

    bool ValidateChunkInfo(const NChunkClient::NProto::TChunkInfo& chunkInfo) const;
    bool IsConfirmed() const;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
