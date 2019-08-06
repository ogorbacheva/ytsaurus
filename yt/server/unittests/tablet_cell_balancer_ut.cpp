#include "helpers.h"

#include <yt/core/test_framework/framework.h>

#include <yt/server/master/node_tracker_server/public.h>
#include <yt/server/master/node_tracker_server/node.h>

#include <yt/server/master/tablet_server/tablet_cell_balancer.h>
#include <yt/server/master/tablet_server/tablet_cell_bundle.h>
#include <yt/server/master/tablet_server/tablet_cell.h>

#include <yt/ytlib/tablet_client/config.h>

#include <yt/core/ytree/fluent.h>

namespace NYT::NTabletServer {
namespace {

using namespace NYTree;
using namespace NNodeTrackerServer;
using namespace NNodeTrackerClient;
using namespace NYson;
using namespace NHydra;

////////////////////////////////////////////////////////////////////////////////

using TSettingParam = std::tuple<const char*, const char*, const char*, int, const char*>;

class TSetting
    : public ITabletCellBalancerProvider
{
public:
    explicit TSetting(const TSettingParam& param)
    {
        auto peersPerCell = ConvertTo<THashMap<TString, int>>(
            TYsonString(TString(std::get<0>(param)), EYsonType::Node));
        auto cellLists = ConvertTo<THashMap<TString, std::vector<int>>>(
            TYsonString(TString(std::get<1>(param)), EYsonType::Node));
        auto nodeFeasibility = ConvertTo<THashMap<TString, std::vector<TString>>>(
            TYsonString(TString(std::get<2>(param)), EYsonType::Node));
        auto tabletSlotCount = std::get<3>(param);
        auto cellDistribution = ConvertTo<THashMap<TString, std::vector<int>>>(
            TYsonString(TString(std::get<4>(param)), EYsonType::Node));

        for (auto& pair : peersPerCell) {
            auto* bundle = GetBundle(pair.first);
            bundle->GetOptions()->PeerCount = pair.second;
        }

        for (auto& pair : cellLists) {
            auto* bundle = GetBundle(pair.first);
            auto& list = pair.second;
            for (int index : list) {
                CreateCell(bundle, index);
            }
        }

        for (auto& pair : nodeFeasibility) {
            auto* node = GetNode(pair.first);
            for (auto& bundleName : pair.second) {
                auto* bundle = GetBundle(bundleName, false);
                YT_VERIFY(FeasibilityMap_[node].insert(bundle).second);
            }
        }

        THashSet<const TNode*> seenNodes;
        THashMap<const TTabletCell*, int> peers;

        for (auto& pair : cellDistribution) {
            auto* node = GetNode(pair.first);
            YT_VERIFY(seenNodes.insert(node).second);

            TTabletCellSet cellSet;

            for (int index : pair.second) {
                auto* cell = GetCell(index);
                int peer = peers[cell]++;
                cell->Peers()[peer].Descriptor = TNodeDescriptor(pair.first);
                cellSet.emplace_back(cell, peer);
            }

            NodeHolders_.emplace_back(node, tabletSlotCount, cellSet);
        }

        for (auto& pair : NodeMap_) {
            auto* node = pair.second;
            if (!seenNodes.contains(node)) {
                seenNodes.insert(node);
                NodeHolders_.emplace_back(node, tabletSlotCount, TTabletCellSet{});
            }
        }

        for (auto& pair : TabletCellMap_) {
            auto* cell = pair.second;
            for (int peer = peers[cell]; peer < cell->GetCellBundle()->GetOptions()->PeerCount; ++peer) {
                UnassignedPeers_.emplace_back(cell, peer);
            }
        }

        InitialDistribution_ = GetDistribution();
    }

    const TTabletCellSet& GetUnassignedPeers()
    {
        return UnassignedPeers_;
    }

    void ApplyMoveDescriptors(const std::vector<TTabletCellMoveDescriptor> descriptors)
    {
        THashMap<const NNodeTrackerServer::TNode*, TNodeHolder*> nodeToHolder;
        for (auto& holder : NodeHolders_) {
            nodeToHolder[holder.GetNode()] = &holder;
        }

        for (const auto& descriptor : descriptors) {
            if (descriptor.Source) {
                RevokePeer(nodeToHolder[descriptor.Source], descriptor.Cell, descriptor.PeerId);
            }
            if (descriptor.Target) {
                AssignPeer(nodeToHolder[descriptor.Target], descriptor.Cell, descriptor.PeerId);
            }
        }
    }

    void ValidateAssingment()
    {
        try {
            ValidatePeerAssignment();
            ValidateNodeFeasibility();
            ValidateSmoothness();
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION(ex)
                << TErrorAttribute("initial_distribution", InitialDistribution_)
                << TErrorAttribute("resulting_distribution", GetDistribution());
        }
    }

    TString GetDistribution()
    {
        return BuildYsonStringFluently(EYsonFormat::Text)
            .DoMapFor(NodeHolders_, [&] (TFluentMap fluent, const TNodeHolder& holder) {
                fluent
                    .Item(NodeToName_[holder.GetNode()])
                    .DoListFor(holder.GetSlots(), [&] (TFluentList fluent, const std::pair<const TTabletCell*, int>& slot) {
                        fluent
                            .Item().Value(Format("(%v,%v,%v)",
                                slot.first->GetCellBundle()->GetName(),
                                CellToIndex_[slot.first],
                                slot.second));
                    });
             })
            .GetData();
    }

    virtual std::vector<TNodeHolder> GetNodes() override
    {
        return NodeHolders_;
    }

    virtual const NHydra::TReadOnlyEntityMap<TTabletCellBundle>& TabletCellBundles() override
    {
        return TabletCellBundleMap_;
    }

    virtual bool IsPossibleHost(const NNodeTrackerServer::TNode* node, const TTabletCellBundle* bundle) override
    {
        if (auto it = FeasibilityMap_.find(node)) {
            return it->second.contains(bundle);
        }
        return false;
    }

    virtual bool IsVerboseLoggingEnabled() override
    {
        return true;
    }

    virtual bool IsBalancingRequired() override
    {
        return true;
    }

private:
    TEntityMap<TTabletCellBundle> TabletCellBundleMap_;
    TEntityMap<TTabletCell> TabletCellMap_;
    TEntityMap<TNode> NodeMap_;
    std::vector<TNodeHolder> NodeHolders_;

    THashMap<const TNode*, THashSet<const TTabletCellBundle*>> FeasibilityMap_;

    THashMap<TString, TTabletCellBundle*> NameToBundle_;
    THashMap<TString, const TNode*> NameToNode_;
    THashMap<const TNode*, TString> NodeToName_;
    THashMap<int, TTabletCell*> IndexToCell_;
    THashMap<const TTabletCell*, int> CellToIndex_;

    TTabletCellSet UnassignedPeers_;

    TString InitialDistribution_;

    TTabletCellBundle* GetBundle(const TString& name, bool create = true)
    {
        if (auto it = NameToBundle_.find(name)) {
            return it->second;
        }

        YT_VERIFY(create);

        auto id = GenerateTabletCellBundleId();
        auto bundleHolder = std::make_unique<TTabletCellBundle>(id);
        bundleHolder->SetName(name);
        auto* bundle = TabletCellBundleMap_.Insert(id, std::move(bundleHolder));
        YT_VERIFY(NameToBundle_.insert(std::make_pair(name, bundle)).second);
        bundle->RefObject();
        return bundle;
    }

    void CreateCell(TTabletCellBundle* bundle, int index)
    {
        auto id = GenerateTabletCellId();
        auto cellHolder = std::make_unique<TTabletCell>(id);
        cellHolder->Peers().resize(bundle->GetOptions()->PeerCount);
        cellHolder->SetCellBundle(bundle);
        auto* cell = TabletCellMap_.Insert(id, std::move(cellHolder));
        YT_VERIFY(IndexToCell_.insert(std::make_pair(index, cell)).second);
        YT_VERIFY(CellToIndex_.insert(std::make_pair(cell, index)).second);
        cell->RefObject();
        YT_VERIFY(bundle->TabletCells().insert(cell).second);
    }

    TTabletCell* GetCell(int index)
    {
        auto it = IndexToCell_.find(index);
        YT_VERIFY(it != IndexToCell_.end());
        return it->second;
    }

    const TNode* GetNode(const TString& name, bool create = true)
    {
        if (auto it = NameToNode_.find(name)) {
            return it->second;
        }

        YT_VERIFY(create);

        auto id = GenerateClusterNodeId();
        auto nodeHolder = std::make_unique<TNode>(id);
        auto* node = NodeMap_.Insert(id, std::move(nodeHolder));
        YT_VERIFY(NameToNode_.insert(std::make_pair(name, node)).second);
        YT_VERIFY(NodeToName_.insert(std::make_pair(node, name)).second);
        node->RefObject();
        node->SetNodeAddresses(TNodeAddressMap{std::make_pair(
            EAddressType::InternalRpc,
            TAddressMap{std::make_pair(DefaultNetworkName, name)})});
        return node;
    }

    void RevokePeer(TNodeHolder* holder, const TTabletCell* cell, int peerId)
    {
        auto pair = holder->RemoveCell(cell);
        YT_VERIFY(pair.second == peerId);
    }

    void AssignPeer(TNodeHolder* holder, const TTabletCell* cell, int peerId)
    {
        holder->InsertCell(std::make_pair(cell, peerId));
    }

    void ValidatePeerAssignment()
    {
        for (const auto& holder : NodeHolders_) {
            THashSet<const TTabletCell*> cellSet;
            for (const auto& slot : holder.GetSlots()) {
                if (cellSet.contains(slot.first)) {
                    THROW_ERROR_EXCEPTION("Cell %v has two peers assigned to node %v",
                        CellToIndex_[slot.first],
                        NodeToName_[holder.GetNode()]);
                }
                YT_VERIFY(cellSet.insert(slot.first).second);
            }
        }

        {
            THashMap<std::pair<const TTabletCell*, int>, const TNode*> cellSet;
            for (const auto& holder : NodeHolders_) {
                for (const auto& slot : holder.GetSlots()) {
                    if (cellSet.contains(slot)) {
                        THROW_ERROR_EXCEPTION("Peer %v of cell %v is assigned to nodes %v and %v",
                            slot.second,
                            CellToIndex_[slot.first],
                            NodeToName_[cellSet[slot]],
                            NodeToName_[holder.GetNode()]);
                    }
                    YT_VERIFY(cellSet.insert(std::make_pair(slot, holder.GetNode())).second);
                }
            }

            for (const auto& pair : TabletCellMap_) {
                auto* cell = pair.second;
                for (int peer = 0; peer < cell->GetCellBundle()->GetOptions()->PeerCount; ++peer) {
                    if (!cellSet.contains(std::make_pair(cell, peer))) {
                        THROW_ERROR_EXCEPTION("Peer %v of cell %v is not assigned to any node",
                            peer,
                            CellToIndex_[cell]);
                    }
                }
            }
        }
    }

    void ValidateNodeFeasibility()
    {
        for (const auto& holder : NodeHolders_) {
            THashSet<const TTabletCell*> cellSet;
            for (const auto& slot : holder.GetSlots()) {
                if (!IsPossibleHost(holder.GetNode(), slot.first->GetCellBundle())) {
                    THROW_ERROR_EXCEPTION("Cell %v is assigned to infeasible node %v",
                        CellToIndex_[slot.first],
                        NodeToName_[holder.GetNode()]);
                }
            }
        }   
    }

    void ValidateSmoothness()
    {
        for (const auto& pair : TabletCellBundleMap_) {
            auto* bundle = pair.second;
            THashMap<const TNode*, int> cellsPerNode;
            int feasibleNodes = 0;
            int cells = 0;

            for (const auto& holder : NodeHolders_) {
                auto* node = holder.GetNode();
                if (!IsPossibleHost(node, bundle)) {
                    continue;
                }
                ++feasibleNodes;
                for (const auto& slot : holder.GetSlots()) {
                    if (slot.first->GetCellBundle() == bundle) {
                        ++cells;
                        cellsPerNode[node]++;
                    }
                }
            }

            if (feasibleNodes == 0) {
                continue;
            }

            int lower = cells / feasibleNodes;
            int upper = (cells + feasibleNodes - 1) / feasibleNodes;

            for (const auto& pair : cellsPerNode) {
                if (pair.second < lower || pair.second > upper) {
                    THROW_ERROR_EXCEPTION("Node %v has %v cells of bundle %v which violates smooth interval [%v, %v]",
                        NodeToName_[pair.first],
                        pair.second,
                        bundle->GetName(),
                        lower,
                        upper);
                }
            } 
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

class TTabletCellBalancerTest
    : public ::testing::Test
    , public ::testing::WithParamInterface<TSettingParam>
{ };

TEST_P(TTabletCellBalancerTest, TestBalancer)
{
    auto setting = New<TSetting>(GetParam());
    auto balancer = CreateTabletCellBalancer(setting);

    for (auto& unassigned : setting->GetUnassignedPeers()) {
        balancer->AssignPeer(unassigned.first, unassigned.second);
    }

    auto moveDescriptors = balancer->GetTabletCellMoveDescriptors();
    setting->ApplyMoveDescriptors(moveDescriptors);
    setting->ValidateAssingment();
}

/*
    Test settings description:
        "{bundle_name: peers_per_cell; ...}",
        "{bundle_name: [cell_index; ...]; ...}",
        "{node_name: [feasible_bundle; ...]; ...}",
        tablet_slots_per_node,
        "{node_name: [cell_index; ...]; ...}"
*/
INSTANTIATE_TEST_CASE_P(
    TabletCellBalancer,
    TTabletCellBalancerTest,
    ::testing::Values(
        std::make_tuple(
            "{a=1;}",
            "{a=[1;2;3;4]; b=[5;6;7;8]}",
            "{n1=[a;b]; n2=[a;b]; n3=[a;b]}",
            10,
            "{n1=[1;2]; n2=[3;4]; n3=[5;6]}"),
        std::make_tuple(
            "{a=2;}",
            "{a=[1;2;3;4]; b=[5;6;7;8]}",
            "{n1=[a;b]; n2=[a;b]; n3=[a;b]}",
            10,
            "{n1=[1;2]; n2=[3;4]; n3=[5;6]}"),
        std::make_tuple(
            "{a=2;}",
            "{a=[1;2;3]}",
            "{n1=[a]; n2=[a]; n3=[a]}",
            2,
            "{n1=[]; n2=[]; n3=[]}"),
        std::make_tuple(
            "{a=2;}",
            "{a=[1;2;3;4;5;6;7;8;9;10]}",
            "{n1=[a]; n2=[a]; n3=[a]}",
            10,
            "{n1=[1;2;3;4;5;6;7;8;9;10]; n2=[1;2;3;4]; n3=[5;6;7;8;9;10]}"),
        std::make_tuple(
            "{a=2; b=2; c=2}",
            "{a=[1;2;3;]; b=[4;5;6;]; c=[7;8;9;]}",
            "{n1=[a;b;c]; n2=[a;b;c]; n3=[a;b;c]}",
            6,
            "{n1=[]; n2=[]; n3=[]}"),
        std::make_tuple(
            "{a=2; b=2; c=2}",
            "{a=[1;2;3;]; b=[4;5;6;]; c=[7;8;9;]}",
            "{n1=[a;b;c]; n2=[a;b;c]; n3=[a;b;c]}",
            6,
            "{n1=[1;2;3;4;5;6;]; n2=[]; n3=[1;2;3;4;5;6;]}"),
        std::make_tuple(
            "{a=2; b=2; c=2}",
            "{a=[1;2;3;]; b=[4;5;6;]; c=[7;8;9;]}",
            "{n1=[a;b;c]; n2=[a;b;c]; n3=[a;b;c]}",
            6,
            "{n1=[1;2;3;4;5;6;]; n2=[1;2;7;8;9;]; n3=[3;4;5;6;8;9]}")
    ));


////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NTabletServer

