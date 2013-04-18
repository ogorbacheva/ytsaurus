#pragma once

#include "public.h"
#include "chunk_tree.h"
#include "chunk_replica.h"

#include <ytlib/misc/property.h>
#include <ytlib/misc/small_vector.h>
#include <ytlib/misc/ref_tracked.h>

#include <ytlib/chunk_client/chunk.pb.h>

#include <ytlib/table_client/table_chunk_meta.pb.h>

#include <ytlib/erasure/public.h>

#include <server/cell_master/public.h>

#include <server/object_server/object_detail.h>

#include <server/security_server/cluster_resources.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunk
    : public TChunkTree
    , public NObjectServer::TStagedObject
    , public TRefTracked<TChunk>
{
    DEFINE_BYREF_RW_PROPERTY(NChunkClient::NProto::TChunkMeta, ChunkMeta);
    DEFINE_BYREF_RW_PROPERTY(NChunkClient::NProto::TChunkInfo, ChunkInfo);

    typedef TSmallVector<TChunkList*, TypicalChunkParentCount> TParents;
    DEFINE_BYREF_RW_PROPERTY(TParents, Parents);

    // This is usually small, e.g. has the length of 3.
    typedef TSmallVector<TNodePtrWithIndex, TypicalReplicationFactor> TStoredReplicas;
    DEFINE_BYREF_RO_PROPERTY(TStoredReplicas, StoredReplicas);

    // This list is usually empty.
    // Keeping a holder is very space efficient (takes just 8 bytes).
    typedef ::THolder< yhash_set<TNodePtrWithIndex> > TCachedReplicas;
    DEFINE_BYREF_RO_PROPERTY(TCachedReplicas, CachedReplicas);

public:
    static const i64 UnknownSize;

    explicit TChunk(const TChunkId& id);
    ~TChunk();

    TChunkTreeStatistics GetStatistics() const;
    NSecurityServer::TClusterResources GetResourceUsage() const;

    void Save(const NCellMaster::TSaveContext& context) const;
    void Load(const NCellMaster::TLoadContext& context);

    void AddReplica(TNodePtrWithIndex replica, bool cached);
    void RemoveReplica(TNodePtrWithIndex replica, bool cached);
    TSmallVector<TNodePtrWithIndex, TypicalReplicationFactor> GetReplicas() const;

    bool ValidateChunkInfo(const NChunkClient::NProto::TChunkInfo& chunkInfo) const;
    bool IsConfirmed() const;

    bool GetMovable() const;
    void SetMovable(bool value);

    bool GetVital() const;
    void SetVital(bool value);

    bool GetRefreshScheduled() const;
    void SetRefreshScheduled(bool value);

    bool GetRFUpdateScheduled() const;
    void SetRFUpdateScheduled(bool value);

    int GetReplicationFactor() const;
    void SetReplicationFactor(int value);

    NErasure::ECodec GetErasureCodec() const;
    void SetErasureCodec(NErasure::ECodec value);

    bool IsErasure() const;

    NErasure::TBlockIndexSet GetReplicaIndexSet() const;

    bool IsAvailable() const;

private:
    struct {
        bool Movable : 1;
        bool Vital : 1; 
        bool RefreshScheduled : 1;
        bool RFUpdateScheduled : 1;
    } Flags;

    i16 ReplicationFactor;
    i16 ErasureCodec;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
