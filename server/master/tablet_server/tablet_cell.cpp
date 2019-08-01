#include "tablet_cell.h"
#include "tablet_cell_bundle.h"
#include "tablet.h"

#include <yt/server/master/cell_master/serialize.h>

#include <yt/server/master/transaction_server/transaction.h>

#include <yt/server/master/object_server/object.h>

#include <yt/ytlib/tablet_client/config.h>

namespace NYT::NTabletServer {

using namespace NCellMaster;
using namespace NHiveClient;
using namespace NNodeTrackerClient;
using namespace NNodeTrackerServer;
using namespace NTabletClient;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

void TTabletCell::TPeer::Persist(NCellMaster::TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Descriptor);
    Persist(context, Node);
    Persist(context, LastSeenTime);
}

////////////////////////////////////////////////////////////////////////////////

TTabletCell::TTabletCell(TTabletCellId id)
    : TNonversionedObjectBase(id)
    , LeadingPeerId_(0)
    , ConfigVersion_(0)
    , Config_(New<TTabletCellConfig>())
    , PrerequisiteTransaction_(nullptr)
    , CellBundle_(nullptr)
{ }

void TTabletCell::Save(TSaveContext& context) const
{
    TNonversionedObjectBase::Save(context);

    using NYT::Save;
    Save(context, LeadingPeerId_);
    Save(context, Peers_);
    Save(context, ConfigVersion_);
    Save(context, *Config_);
    Save(context, Tablets_);
    Save(context, ClusterStatistics_);
    Save(context, MulticellStatistics_);
    Save(context, PrerequisiteTransaction_);
    Save(context, CellBundle_);
    Save(context, TabletCellLifeStage_);
}

void TTabletCell::Load(TLoadContext& context)
{
    TNonversionedObjectBase::Load(context);

    using NYT::Load;
    Load(context, LeadingPeerId_);
    Load(context, Peers_);
    Load(context, ConfigVersion_);
    Load(context, *Config_);
    Load(context, Tablets_);
    Load(context, ClusterStatistics_);
    // COMPAT(savrus)
    if (context.GetVersion() >= EMasterSnapshotVersion::MulticellForDynamicTables) {
        Load(context, MulticellStatistics_);
    }
    Load(context, PrerequisiteTransaction_);
    Load(context, CellBundle_);
    // COMPAT(savrus)
    if (context.GetVersion() >= EMasterSnapshotVersion::AddTabletCellDecommission) {
        if (context.GetVersion() >= EMasterSnapshotVersion::FixSnapshot) {
            Load(context, TabletCellLifeStage_);
        } else if (context.GetVersion() >= EMasterSnapshotVersion::AddTabletCellLifeStage) {
            // Saved wrong value by accident
            Load<NObjectServer::EObjectLifeStage>(context);
            TabletCellLifeStage_ = ETabletCellLifeStage::Running;
        } else {
            TabletCellLifeStage_ = Load<bool>(context)
                ? ETabletCellLifeStage::Decommissioned
                : ETabletCellLifeStage::Running;
        }
    }
}

TPeerId TTabletCell::FindPeerId(const TString& address) const
{
    for (auto peerId = 0; peerId < Peers_.size(); ++peerId) {
        const auto& peer = Peers_[peerId];
        if (peer.Descriptor.GetDefaultAddress() == address) {
            return peerId;
        }
    }
    return InvalidPeerId;
}

TPeerId TTabletCell::GetPeerId(const TString& address) const
{
    auto peerId = FindPeerId(address);
    YT_VERIFY(peerId != InvalidPeerId);
    return peerId;
}

TPeerId TTabletCell::FindPeerId(TNode* node) const
{
    for (TPeerId peerId = 0; peerId < Peers_.size(); ++peerId) {
        if (Peers_[peerId].Node == node) {
            return peerId;
        }
    }
    return InvalidPeerId;
}

TPeerId TTabletCell::GetPeerId(TNode* node) const
{
    auto peerId = FindPeerId(node);
    YT_VERIFY(peerId != InvalidPeerId);
    return peerId;
}

void TTabletCell::AssignPeer(const TCellPeerDescriptor& descriptor, TPeerId peerId)
{
    auto& peer = Peers_[peerId];
    YT_VERIFY(peer.Descriptor.IsNull());
    YT_VERIFY(!descriptor.IsNull());
    peer.Descriptor = descriptor;
}

void TTabletCell::RevokePeer(TPeerId peerId)
{
    auto& peer = Peers_[peerId];
    YT_VERIFY(!peer.Descriptor.IsNull());
    peer.Descriptor = TCellPeerDescriptor();
    peer.Node = nullptr;
}

void TTabletCell::AttachPeer(TNode* node, TPeerId peerId)
{
    auto& peer = Peers_[peerId];
    YT_VERIFY(peer.Descriptor.GetDefaultAddress() == node->GetDefaultAddress());

    YT_VERIFY(!peer.Node);
    peer.Node = node;
}

void TTabletCell::DetachPeer(TNode* node)
{
    auto peerId = FindPeerId(node);
    if (peerId != InvalidPeerId) {
        Peers_[peerId].Node = nullptr;
    }
}

void TTabletCell::UpdatePeerSeenTime(TPeerId peerId, TInstant when)
{
    auto& peer = Peers_[peerId];
    peer.LastSeenTime = when;
}

ETabletCellHealth TTabletCell::GetHealth() const
{
    const auto& leaderPeer = Peers_[LeadingPeerId_];
    auto* leaderNode = leaderPeer.Node;
    if (!IsObjectAlive(leaderNode)) {
        return Tablets_.empty() ? ETabletCellHealth::Initializing : ETabletCellHealth::Failed;
    }

    const auto* leaderSlot = leaderNode->GetTabletSlot(this);
    if (leaderSlot->PeerState != EPeerState::Leading) {
        return Tablets_.empty() ? ETabletCellHealth::Initializing : ETabletCellHealth::Failed;
    }

    for (auto peerId = 0; peerId < static_cast<int>(Peers_.size()); ++peerId) {
        if (peerId == LeadingPeerId_) {
            continue;
        }
        const auto& peer = Peers_[peerId];
        auto* node = peer.Node;
        if (!IsObjectAlive(node)) {
            return ETabletCellHealth::Degraded;
        }
        const auto* slot = node->GetTabletSlot(this);
        if (slot->PeerState != EPeerState::Following) {
            return ETabletCellHealth::Degraded;
        }
    }

    return ETabletCellHealth::Good;
}

ETabletCellHealth TTabletCell::GetMulticellHealth() const
{
    return CombineHealths(GetHealth(), ClusterStatistics().Health);
}

TCellDescriptor TTabletCell::GetDescriptor() const
{
    TCellDescriptor descriptor;
    descriptor.CellId = Id_;
    descriptor.ConfigVersion = ConfigVersion_;
    for (auto peerId = 0; peerId < static_cast<int>(Peers_.size()); ++peerId) {
        descriptor.Peers.push_back(TCellPeerDescriptor(
            Peers_[peerId].Descriptor,
            peerId == LeadingPeerId_));
    }
    return descriptor;
}

TTabletCellStatistics& TTabletCell::LocalStatistics()
{
    return *LocalStatisticsPtr_;
}

const TTabletCellStatistics& TTabletCell::LocalStatistics() const
{
    return *LocalStatisticsPtr_;
}

TTabletCellStatistics* TTabletCell::GetCellStatistics(NObjectClient::TCellTag cellTag)
{
    auto it = MulticellStatistics_.find(cellTag);
    YT_VERIFY(it != MulticellStatistics_.end());
    return &it->second;
}

void TTabletCell::RecomputeClusterStatistics()
{
    ClusterStatistics_ = TTabletCellStatistics();
    ClusterStatistics_.Decommissioned = true;
    ClusterStatistics_.Health = GetHealth();
    for (const auto& pair : MulticellStatistics_) {
        ClusterStatistics_ += pair.second;
        ClusterStatistics_.Decommissioned &= pair.second.Decommissioned;
        ClusterStatistics_.Health = CombineHealths(ClusterStatistics_.Health, pair.second.Health);
    }
}

ETabletCellHealth TTabletCell::CombineHealths(ETabletCellHealth lhs, ETabletCellHealth rhs)
{
    static constexpr std::array<ETabletCellHealth, 4> HealthOrder{{
        ETabletCellHealth::Failed,
        ETabletCellHealth::Degraded,
        ETabletCellHealth::Initializing,
        ETabletCellHealth::Good}};

    for (auto health : HealthOrder) {
        if (lhs == health || rhs == health) {
            return health;
        }
    }

    return ETabletCellHealth::Failed;
}

bool TTabletCell::DecommissionStarted() const
{
    return TabletCellLifeStage_ == ETabletCellLifeStage::DecommissioningOnMaster ||
        TabletCellLifeStage_ == ETabletCellLifeStage::DecommissioningOnNode ||
        TabletCellLifeStage_ == ETabletCellLifeStage::Decommissioned;
}

bool TTabletCell::DecommissionCompleted() const
{
    return TabletCellLifeStage_ == ETabletCellLifeStage::Decommissioned;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer

