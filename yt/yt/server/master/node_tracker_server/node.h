#pragma once

#include "public.h"

#include <yt/yt/server/master/cell_master/public.h>

#include <yt/yt/server/master/chunk_server/chunk_replica.h>

#include <yt/yt/server/lib/hydra/entity_map.h>

#include <yt/yt/server/master/node_tracker_server/proto/node_tracker.pb.h>

#include <yt/yt/server/master/cell_server/public.h>

#include <yt/yt/server/master/transaction_server/public.h>

#include <yt/yt/server/master/object_server/object.h>

#include <yt/yt/server/lib/cellar_agent/public.h>

#include <yt/yt/ytlib/node_tracker_client/proto/node_tracker_service.pb.h>
#include <yt/yt/client/node_tracker_client/node_directory.h>
#include <yt/yt/ytlib/node_tracker_client/node_statistics.h>

#include <yt/yt/core/misc/optional.h>
#include <yt/yt/core/misc/property.h>
#include <yt/yt/core/misc/ref_tracked.h>

#include <array>

namespace NYT::NNodeTrackerServer {

////////////////////////////////////////////////////////////////////////////////

struct TCellNodeStatistics
{
    NChunkClient::TMediumMap<i64> ChunkReplicaCount;
    i64 DestroyedChunkReplicaCount = 0;
};

TCellNodeStatistics& operator+=(TCellNodeStatistics& lhs, const TCellNodeStatistics& rhs);

void ToProto(NProto::TReqSetCellNodeDescriptors::TStatistics* protoStatistics, const TCellNodeStatistics& statistics);
void FromProto(TCellNodeStatistics* statistics, const NProto::TReqSetCellNodeDescriptors::TStatistics& protoStatistics);

struct TCellNodeDescriptor
{
    ENodeState State = ENodeState::Unknown;
    TCellNodeStatistics Statistics;
};

void ToProto(NProto::TReqSetCellNodeDescriptors::TNodeDescriptor* protoDescriptor, const TCellNodeDescriptor& descriptor);
void FromProto(TCellNodeDescriptor* descriptor, const NProto::TReqSetCellNodeDescriptors::TNodeDescriptor& protoDescriptor);

class TNode
    : public NObjectServer::TObject
    , public TRefTracked<TNode>
{
public:
    // Import third-party types into the scope.
    using TChunkPtrWithIndexes = NChunkServer::TChunkPtrWithIndexes;
    using TChunkPtrWithIndex = NChunkServer::TChunkPtrWithIndex;
    using TChunkId = NChunkServer::TChunkId;
    using TChunk = NChunkServer::TChunk;
    using TJobId = NChunkServer::TJobId;
    using TJobPtr = NChunkServer::TJobPtr;
    template <typename T>
    using TMediumMap = NChunkClient::TMediumMap<T>;
    using TMediumIndexSet = std::bitset<NChunkClient::MaxMediumCount>;

    DEFINE_BYREF_RO_PROPERTY(TMediumMap<double>, IOWeights);
    DEFINE_BYREF_RO_PROPERTY(TMediumMap<i64>, TotalSpace);
    DEFINE_BYREF_RW_PROPERTY(TMediumMap<int>, ConsistentReplicaPlacementTokenCount);

    // Transient property.
    DEFINE_BYVAL_RW_PROPERTY(ENodeState, LastGossipState, ENodeState::Unknown);

    ui64 GetVisitMark(int mediumIndex);
    void SetVisitMark(int mediumIndex, ui64 mark);

    using TMulticellDescriptors = THashMap<NObjectClient::TCellTag, TCellNodeDescriptor>;
    DEFINE_BYREF_RO_PROPERTY(TMulticellDescriptors, MulticellDescriptors);

    //! Tags specified by user in "user_tags" attribute.
    DEFINE_BYREF_RO_PROPERTY(std::vector<TString>, UserTags);
    //! Tags received from node during registration (those typically come from config).
    DEFINE_BYREF_RO_PROPERTY(std::vector<TString>, NodeTags);
    //! User tags plus node tags.
    DEFINE_BYREF_RO_PROPERTY(THashSet<TString>, Tags);

    DEFINE_BYVAL_RW_PROPERTY(TInstant, RegisterTime);
    DEFINE_BYVAL_RW_PROPERTY(TInstant, LastSeenTime);

    DEFINE_BYVAL_RW_PROPERTY(NYson::TYsonString, Annotations);
    DEFINE_BYVAL_RW_PROPERTY(TString, Version);

    DEFINE_BYREF_RW_PROPERTY(THashSet<ENodeFlavor>, Flavors);

    //! Helpers for |Flavors| access.
    bool IsDataNode() const;
    bool IsExecNode() const;
    bool IsTabletNode() const;
    bool IsCellarNode() const;

    //! This set contains heartbeat types that were reported by the node since last registration.
    //! Node is considered online iff it received all heartbeats corresponded to its flavors.
    DEFINE_BYREF_RW_PROPERTY(THashSet<ENodeHeartbeatType>, ReportedHeartbeats);

    //! Helpers for |ReportedHeartbeats| access.
    bool ReportedClusterNodeHeartbeat() const;
    bool ReportedDataNodeHeartbeat() const;
    bool ReportedExecNodeHeartbeat() const;
    bool ReportedCellarNodeHeartbeat() const;
    bool ReportedTabletNodeHeartbeat() const;

    void ValidateRegistered();

    DEFINE_BYREF_RO_PROPERTY(NNodeTrackerClient::NProto::TClusterNodeStatistics, ClusterNodeStatistics);
    void SetClusterNodeStatistics(NNodeTrackerClient::NProto::TClusterNodeStatistics&& statistics);

    DEFINE_BYREF_RW_PROPERTY(std::vector<TError>, Alerts);

    DEFINE_BYREF_RO_PROPERTY(NNodeTrackerClient::NProto::TNodeResources, ResourceLimits);
    DEFINE_BYREF_RO_PROPERTY(NNodeTrackerClient::NProto::TNodeResources, ResourceUsage);
    DEFINE_BYREF_RW_PROPERTY(NNodeTrackerClient::NProto::TNodeResourceLimitsOverrides, ResourceLimitsOverrides);

    DEFINE_BYREF_RW_PROPERTY(std::vector<NChunkClient::TLocationUuid>, LocationUuids);

    // COMPAT(gritukan): This is used for host creation only.
    DEFINE_BYVAL_RO_PROPERTY(TRack*, LegacyRack);

    DEFINE_BYVAL_RO_PROPERTY(THost*, Host);

    // Lease tracking.
    DEFINE_BYVAL_RW_PROPERTY(NTransactionServer::TTransaction*, LeaseTransaction);

    // Exec Node stuff.
    DEFINE_BYREF_RO_PROPERTY(NNodeTrackerClient::NProto::TExecNodeStatistics, ExecNodeStatistics);
    void SetExecNodeStatistics(NNodeTrackerClient::NProto::TExecNodeStatistics&& statistics);

    // Chunk Manager stuff.
    DEFINE_BYREF_RO_PROPERTY(NNodeTrackerClient::NProto::TDataNodeStatistics, DataNodeStatistics);
    void SetDataNodeStatistics(
        NNodeTrackerClient::NProto::TDataNodeStatistics&& statistics,
        const NChunkServer::TChunkManagerPtr& chunkManager);

    DEFINE_BYVAL_RW_PROPERTY(bool, Banned);
    void ValidateNotBanned();

    DEFINE_BYVAL_RW_PROPERTY(bool, Decommissioned);

    using TFillFactorIterator = std::optional<NChunkServer::TFillFactorToNodeIterator>;
    using TFillFactorIterators = TMediumMap<TFillFactorIterator>;

    using TLoadFactorIterator = std::optional<NChunkServer::TLoadFactorToNodeIterator>;
    using TLoadFactorIterators = TMediumMap<TLoadFactorIterator>;

    DEFINE_BYREF_RW_PROPERTY(TFillFactorIterators, FillFactorIterators);
    DEFINE_BYREF_RW_PROPERTY(TLoadFactorIterators, LoadFactorIterators);
    TFillFactorIterator GetFillFactorIterator(int mediumIndex) const;
    void SetFillFactorIterator(int mediumIndex, TFillFactorIterator iter);
    TLoadFactorIterator GetLoadFactorIterator(int mediumIndex) const;
    void SetLoadFactorIterator(int mediumIndex, TLoadFactorIterator iter);

    DEFINE_BYVAL_RO_PROPERTY(bool, DisableWriteSessions);
    DEFINE_BYVAL_RW_PROPERTY(bool, DisableSchedulerJobs);
    DEFINE_BYVAL_RW_PROPERTY(bool, DisableTabletCells);

    bool GetEffectiveDisableWriteSessions() const;

    // Transient copies of DisableWriteSessions.
    DEFINE_BYVAL_RO_PROPERTY(bool, DisableWriteSessionsSentToNode);
    void SetDisableWriteSessionsSentToNode(bool value);

    DEFINE_BYVAL_RO_PROPERTY(bool, DisableWriteSessionsReportedByNode);
    void SetDisableWriteSessionsReportedByNode(bool value);

    bool IsValidWriteTarget() const;

    // NB: Randomize replica hashing to avoid collisions during balancing.
    using TMediumReplicaSet = THashSet<TChunkPtrWithIndexes>;
    using TReplicaSet = TMediumMap<TMediumReplicaSet>;
    DEFINE_BYREF_RO_PROPERTY(TReplicaSet, Replicas);

    //! Maps replicas to the leader timestamp when this replica was registered by a client.
    using TUnapprovedReplicaMap = THashMap<TChunkPtrWithIndexes, TInstant>;
    DEFINE_BYREF_RW_PROPERTY(TUnapprovedReplicaMap, UnapprovedReplicas);

    using TDestroyedReplicaSet = THashSet<NChunkClient::TChunkIdWithIndexes>;
    DEFINE_BYREF_RO_PROPERTY(TDestroyedReplicaSet, DestroyedReplicas);
    DEFINE_BYVAL_RO_PROPERTY(TDestroyedReplicaSet::iterator, DestroyedReplicasIterator);

    using TJobMap = THashMap<TJobId, TJobPtr>;
    DEFINE_BYREF_RO_PROPERTY(TJobMap, IdToJob);

    //! Indexed by priority. Each map is as follows:
    //! Key:
    //!   Encodes chunk and one of its parts (for erasure chunks only, others use GenericChunkReplicaIndex).
    //!   Medium index indicates the medium where this replica is being stored.
    //! Value:
    //!   Indicates media where acting as replication targets for this chunk.
    using TChunkReplicationQueues = std::vector<THashMap<TChunkPtrWithIndexes, TMediumIndexSet>>;
    DEFINE_BYREF_RW_PROPERTY(TChunkReplicationQueues, ChunkReplicationQueues);

    //! Key:
    //!   Encodes chunk and one of its parts (for erasure chunks only, others use GenericChunkReplicaIndex).
    //! Value:
    //!   Indicates media where removal of this chunk is scheduled.
    using TChunkRemovalQueue = THashMap<NChunkClient::TChunkIdWithIndex, TMediumIndexSet>;
    DEFINE_BYREF_RW_PROPERTY(TChunkRemovalQueue, ChunkRemovalQueue);

    //! Key:
    //!   Indicates an unsealed chunk.
    //!   Medium index indicates the medium where this replica is being stored.
    using TChunkSealQueue = THashSet<TChunkPtrWithIndexes>;
    DEFINE_BYREF_RW_PROPERTY(TChunkSealQueue, ChunkSealQueue);

    //! Chunk replica announcement requests that should be sent to the node upon next heartbeat.
    //! Non-null revision means that the request was already sent and is pending confirmation.
    using TEndorsementMap = THashMap<TChunk*, NHydra::TRevision>;
    DEFINE_BYREF_RW_PROPERTY(TEndorsementMap, ReplicaEndorsements);

    // Cell Manager stuff.
    struct TCellSlot
    {
        NCellServer::TCellBase* Cell = nullptr;
        NHydra::EPeerState PeerState = NHydra::EPeerState::None;
        int PeerId = NHydra::InvalidPeerId;
        bool IsResponseKeeperWarmingUp = false;

        //! Sum of `PreloadPendingStoreCount` over all tablets in slot.
        int PreloadPendingStoreCount = 0;

        //! Sum of `PreloadCompletedStoreCount` over all tablets in slot.
        int PreloadCompletedStoreCount = 0;

        //! Sum of `PreloadFailedStoreCount` over all tablets in slot.
        int PreloadFailedStoreCount = 0;

        void Persist(const NCellMaster::TPersistenceContext& context);

        // Used in cell balancer to check peer state.
        bool IsWarmedUp() const;
    };

    using TCellar = SmallVector<TCellSlot, NCellarClient::TypicalCellarSize>;
    using TCellarMap = THashMap<NCellarClient::ECellarType, TCellar>;
    DEFINE_BYREF_RW_PROPERTY(TCellarMap, Cellars);

public:
    explicit TNode(NObjectServer::TObjectId objectId);

    TNodeId GetId() const;

    TNodeDescriptor GetDescriptor(NNodeTrackerClient::EAddressType addressType = NNodeTrackerClient::EAddressType::InternalRpc) const;

    const TNodeAddressMap& GetNodeAddresses() const;
    void SetNodeAddresses(const TNodeAddressMap& nodeAddresses);
    const TAddressMap& GetAddressesOrThrow(NNodeTrackerClient::EAddressType addressType) const;
    const TString& GetDefaultAddress() const;

    //! Get rack to which this node belongs.
    /*!
     *  May return nullptr if the node belongs to no rack
     */
    TRack* GetRack() const;

    //! Get data center to which this node belongs.
    /*!
     *  May return nullptr if the node belongs to no rack or its rack belongs to
     *  no data center.
     */
    TDataCenter* GetDataCenter() const;

    bool HasTag(const std::optional<TString>& tag) const;

    //! Prepares per-cell state map.
    //! Inserts new entries into the map, fills missing onces with ENodeState::Offline value.
    void InitializeStates(NObjectClient::TCellTag cellTag, const NObjectClient::TCellTagList& secondaryCellTags);

    //! Recomputes node IO weights from statistics.
    void RecomputeIOWeights(const NChunkServer::TChunkManagerPtr& chunkManager);

    //! Gets the local state by dereferencing local state pointer.
    ENodeState GetLocalState() const;
    //! Sets the local state by dereferencing local state pointer.
    void SetLocalState(ENodeState state);

    //! Sets the state and statistics for the given cell.
    void SetCellDescriptor(NObjectClient::TCellTag cellTag, const TCellNodeDescriptor& descriptor);

    //! If states are same for all cells then returns this common value.
    //! Otherwise returns "mixed" state.
    ENodeState GetAggregatedState() const;

    TString GetLowercaseObjectName() const override;
    TString GetCapitalizedObjectName() const override;

    void Save(NCellMaster::TSaveContext& context) const;
    void Load(NCellMaster::TLoadContext& context);

    TJobPtr FindJob(TJobId jobId);
    void RegisterJob(const TJobPtr& job);
    void UnregisterJob(const TJobPtr& job);

    // Chunk Manager stuff.
    void ReserveReplicas(int mediumIndex, int sizeHint);
    //! Returns |true| if the replica was actually added.
    bool AddReplica(TChunkPtrWithIndexes replica);
    //! Returns |true| if the replica was approved
    bool RemoveReplica(TChunkPtrWithIndexes replica);

    bool HasReplica(TChunkPtrWithIndexes) const;
    TChunkPtrWithIndexes PickRandomReplica(int mediumIndex);
    void ClearReplicas();

    void AddUnapprovedReplica(TChunkPtrWithIndexes replica, TInstant timestamp);
    bool HasUnapprovedReplica(TChunkPtrWithIndexes replica) const;
    void ApproveReplica(TChunkPtrWithIndexes replica);

    bool AddDestroyedReplica(const NChunkClient::TChunkIdWithIndexes& replica);
    bool RemoveDestroyedReplica(const NChunkClient::TChunkIdWithIndexes& replica);

    void AddToChunkRemovalQueue(const NChunkClient::TChunkIdWithIndexes& replica);
    void RemoveFromChunkRemovalQueue(const NChunkClient::TChunkIdWithIndexes& replica);

    void AddToChunkReplicationQueue(TChunkPtrWithIndexes replica, int targetMediumIndex, int priority);
    //! Handles the case |targetMediumIndex == AllMediaIndex| correctly.
    void RemoveFromChunkReplicationQueues(TChunkPtrWithIndexes replica, int targetMediumIndex);

    void AddToChunkSealQueue(TChunkPtrWithIndexes chunkWithIndexes);
    void RemoveFromChunkSealQueue(TChunkPtrWithIndexes chunkWithIndexes);

    void ClearSessionHints();
    void AddSessionHint(int mediumIndex, NChunkClient::ESessionType sessionType);

    int GetSessionCount(NChunkClient::ESessionType sessionType) const;
    int GetTotalSessionCount() const;

    int GetCellarSize(NCellarClient::ECellarType) const;

    // Returns true iff the node has at least one location belonging to the
    // specified medium.
    bool HasMedium(int mediumIndex) const;

    //! Returns null if there's no storage of specified medium on this node.
    std::optional<double> GetFillFactor(int mediumIndex) const;
    //! Returns null if there's no storage of specified medium left on this node.
    std::optional<double> GetLoadFactor(int mediumIndex, int chunkHostMasterCellCount) const;

    bool IsWriteEnabled(int mediumIndex) const;

    TCellSlot* FindCellSlot(const NCellServer::TCellBase* cell);
    TCellSlot* GetCellSlot(const NCellServer::TCellBase* cell);

    void DetachTabletCell(const NCellServer::TCellBase* cell);

    TCellar* FindCellar(NCellarClient::ECellarType cellarType);
    const TCellar* FindCellar(NCellarClient::ECellarType cellarType) const;
    TCellar& GetCellar(NCellarClient::ECellarType cellarType);
    const TCellar& GetCellar(NCellarClient::ECellarType cellarType) const;

    void InitCellars();
    void ClearCellars();
    void UpdateCellarSize(NCellarClient::ECellarType cellarType, int newSize);

    void SetCellarNodeStatistics(
        NCellarClient::ECellarType cellarType,
        NNodeTrackerClient::NProto::TCellarNodeStatistics&& statistics);
    void RemoveCellarNodeStatistics(NCellarClient::ECellarType cellarType);

    int GetAvailableSlotCount(NCellarClient::ECellarType cellarType) const;
    int GetTotalSlotCount(NCellarClient::ECellarType cellarType) const;

    void ShrinkHashTables();

    void Reset();

    static ui64 GenerateVisitMark();

    // Computes node statistics for the local cell.
    TCellNodeStatistics ComputeCellStatistics() const;
    // Computes total cluster statistics (over all cells, including the local one).
    TCellNodeStatistics ComputeClusterStatistics() const;

    void ClearCellStatistics();

    void ClearDestroyedReplicas();

    void AdvanceDestroyedReplicasIterator();
    void ResetDestroyedReplicasIterator();

private:
    NNodeTrackerClient::TNodeAddressMap NodeAddresses_;
    TString DefaultAddress_;

    TMediumMap<int> HintedUserSessionCount_;
    TMediumMap<int> HintedReplicationSessionCount_;
    TMediumMap<int> HintedRepairSessionCount_;

    int TotalHintedUserSessionCount_;
    int TotalHintedReplicationSessionCount_;
    int TotalHintedRepairSessionCount_;

    TMediumMap<TMediumReplicaSet::iterator> RandomReplicaIters_;

    TMediumMap<ui64> VisitMarks_{};

    TMediumMap<std::optional<double>> FillFactors_;
    TMediumMap<std::optional<int>> SessionCount_;

    ENodeState* LocalStatePtr_ = nullptr;
    ENodeState AggregatedState_ = ENodeState::Unknown;

    THashMap<NCellarClient::ECellarType, NNodeTrackerClient::NProto::TCellarNodeStatistics> CellarNodeStatistics_;

    int GetHintedSessionCount(int mediumIndex, int chunkHostMasterCellCount) const;

    void ComputeAggregatedState();
    void ComputeDefaultAddress();
    void ComputeFillFactorsAndTotalSpace();
    void ComputeSessionCount();

    bool DoAddReplica(TChunkPtrWithIndexes replica);
    bool DoRemoveReplica(TChunkPtrWithIndexes replica);
    void DoRemoveJournalReplicas(TChunkPtrWithIndexes replica);
    bool DoHasReplica(TChunkPtrWithIndexes replica) const;

    // Private accessors for TNodeTracker.
    friend class TNodeTracker;

    void SetHost(THost* host);
    void SetDisableWriteSessions(bool value);

    void SetNodeTags(const std::vector<TString>& tags);
    void SetUserTags(const std::vector<TString>& tags);
    void RebuildTags();

    void SetResourceUsage(const NNodeTrackerClient::NProto::TNodeResources& resourceUsage);
    void SetResourceLimits(const NNodeTrackerClient::NProto::TNodeResources& resourceLimits);
};

////////////////////////////////////////////////////////////////////////////////

struct TNodePtrAddressFormatter
{
    void operator()(TStringBuilderBase* builder, TNode* node) const;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerServer
