#include "helpers.h"

#include <yt/core/test_framework/framework.h>

#include <yt/server/master/node_tracker_server/public.h>
#include <yt/server/master/node_tracker_server/node.h>

#include <yt/server/master/tablet_server/tablet_cell_balancer.h>
#include <yt/server/master/tablet_server/tablet_cell_bundle.h>
#include <yt/server/master/tablet_server/tablet_cell.h>

#include <yt/ytlib/tablet_client/config.h>

#include <util/random/random.h>

namespace NYT::NTabletServer {
namespace {

using namespace NYTree;
using namespace NNodeTrackerServer;
using namespace NNodeTrackerClient;
using namespace NYson;
using namespace NHydra;

////////////////////////////////////////////////////////////////////////////////

using TSettingParam = std::tuple<const char*, const char*, const char*, int, const char*>;
using TStressSettingParam = std::tuple<int, int, int, int, int>;
using TCompleteSettingParam = std::tuple<
    THashMap<TString, int>,
    THashMap<TString, std::vector<int>>,
    THashMap<TString, std::vector<TString>>,
    int,
    THashMap<TString, std::vector<int>>>;

class TSetting
    : public ITabletCellBalancerProvider
{
public:
    TSetting(
        const THashMap<TString, int>& peersPerCell,
        const THashMap<TString, std::vector<int>>& cellLists,
        const THashMap<TString, std::vector<TString>>& nodeFeasibility,
        int tabletSlotCount,
        const THashMap<TString, std::vector<int>>& cellDistribution)
    {
        Initialize(peersPerCell, cellLists, nodeFeasibility, tabletSlotCount, cellDistribution);
    }

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

        Initialize(peersPerCell, cellLists, nodeFeasibility, tabletSlotCount, cellDistribution);
    }

    void Initialize(
        const THashMap<TString, int>& peersPerCell,
        const THashMap<TString, std::vector<int>>& cellLists,
        const THashMap<TString, std::vector<TString>>& nodeFeasibility,
        int tabletSlotCount,
        const THashMap<TString, std::vector<int>>& cellDistribution)
    {
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

        PeersPerCell_ = ConvertToYsonString(peersPerCell, EYsonFormat::Text).GetData();
        CellLists_ = ConvertToYsonString(cellLists, EYsonFormat::Text).GetData();
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

    void ValidateAssingment(const std::vector<TTabletCellMoveDescriptor>& moveDescriptors)
    {
        ApplyMoveDescriptors(moveDescriptors);

        try {
            ValidatePeerAssignment();
            ValidateNodeFeasibility();
            ValidateSmoothness();
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION(ex)
                << TErrorAttribute("peers_per_cell", PeersPerCell_)
                << TErrorAttribute("cell_lists", CellLists_)
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

    TString PeersPerCell_;
    TString CellLists_;
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

class TTabletCellBalancerRevokeTest
    : public ::testing::Test
    , public ::testing::WithParamInterface<TSettingParam>
{ };

class TTabletCellBalancerStressTest
    : public ::testing::Test
    , public ::testing::WithParamInterface<TStressSettingParam>
{
public:
    virtual void SetUp() override {
        std::tie(NodesNum_, TabletSlotCount_, PeersNum_, BundlesNum_, CellsNum_) = GetParam();

        YT_VERIFY(NodesNum_ * TabletSlotCount_ == PeersNum_ * BundlesNum_ * CellsNum_);
        YT_VERIFY(NodesNum_ >= PeersNum_);

        Nodes_.resize(NodesNum_);
        std::vector<TString> bundles(BundlesNum_);
        for (int i = 0; i < NodesNum_; ++i) {
            Nodes_[i] = Format("n%v", i);
        }
        for (int i = 0; i < BundlesNum_; ++i) {
            bundles[i] = Format("b%v", i);
        }

        for (int i = 0; i < NodesNum_; ++i) {
            NodeFeasibility_[Nodes_[i]] = bundles;
        }

        Cells_.resize(BundlesNum_, std::vector<int>(CellsNum_));
        CellsFlattened_.resize(BundlesNum_ * CellsNum_);
        int cellIdx = 0;
        for (auto& cell : Cells_) {
            std::iota(cell.begin(), cell.end(), cellIdx);
            cellIdx += cell.size();
        }
        std::iota(CellsFlattened_.begin(), CellsFlattened_.end(), 0);

        for (const auto& bundle : bundles) {
            PeersPerCell_[bundle] = PeersNum_;
        }
        for (int i = 0; i < BundlesNum_; ++i) {
            CellLists_[bundles[i]] = Cells_[i];
        }
    }

    virtual void TearDown() override {
        auto setting = New<TSetting>(PeersPerCell_, CellLists_, NodeFeasibility_, TabletSlotCount_, CellDistribution_);
        auto balancer = CreateTabletCellBalancer(setting);
        for (auto& unassigned : setting->GetUnassignedPeers()) {
            balancer->AssignPeer(unassigned.first, unassigned.second);
        }

        setting->ValidateAssingment(balancer->GetTabletCellMoveDescriptors());
    }

protected:
    int NodesNum_;
    int PeersNum_;
    int BundlesNum_;
    int CellsNum_;

    std::vector<TString> Nodes_;
    std::vector<std::vector<int>> Cells_;
    std::vector<int> CellsFlattened_;

    THashMap<TString, int> PeersPerCell_;
    THashMap<TString, std::vector<int>> CellLists_;
    THashMap<TString, std::vector<TString>> NodeFeasibility_;
    int TabletSlotCount_;
    THashMap<TString, std::vector<int>> CellDistribution_;
};

TEST_P(TTabletCellBalancerStressTest, TestBalancerEmptyDistribution)
{
    CellDistribution_.clear();
    for (int i = 0; i < NodesNum_; ++i) {
        CellDistribution_[Nodes_[i]] = {};
    }
}

// Emplace full bundles (first bundles first) while possible.
TEST_P(TTabletCellBalancerStressTest, TestBalancerGeneratedDistribution1)
{
    int initialBundleIdx = 0;
    int initialNodeIdx = 0;
    const int takenBundles = TabletSlotCount_ / CellsNum_;
    while (initialNodeIdx + PeersNum_ < NodesNum_) {
        for (int nodeIdx = initialNodeIdx; nodeIdx < initialNodeIdx + PeersNum_; ++nodeIdx) {
            auto& distribution = CellDistribution_[Nodes_[nodeIdx]];
            for (int bundleIdx = initialBundleIdx; bundleIdx < initialBundleIdx + takenBundles; ++bundleIdx) {
                for (int cellIdx = 0; cellIdx < CellsNum_; ++cellIdx) {
                    distribution.emplace_back(Cells_[bundleIdx][cellIdx]);
                }
            }
            YT_ASSERT(distribution.size() <= TabletSlotCount_);
            YT_ASSERT(distribution.size() == takenBundles * CellsNum_);
        }
        initialNodeIdx += PeersNum_;
        initialBundleIdx += takenBundles;
    }
    // State when we have to do some cell exchanges
    YT_ASSERT(initialBundleIdx - takenBundles < BundlesNum_);
}

// Fill all nodes except last 2 with all cells.
TEST_P(TTabletCellBalancerStressTest, TestBalancerGeneratedDistribution2)
{
    int node = 0;
    int cell = 0;
    int replicaCount = 0;
    std::vector<int> allEmplaces(CellsFlattened_.size(), 0);
    while (node < NodesNum_ - 2 && replicaCount < PeersNum_) {
        for (int slotIdx = 0; slotIdx < TabletSlotCount_; ++slotIdx) {
            CellDistribution_[Nodes_[node]].emplace_back(CellsFlattened_[cell]);
            ++allEmplaces[cell];

            ++cell;
            if (cell == CellsFlattened_.size()) {
                cell = 0;
                ++replicaCount;
                if (replicaCount == PeersNum_) {
                    break;
                }
            }
        }

        ++node;
    }
}

TEST_P(TTabletCellBalancerStressTest, TestBalancerRandomDistribution)
{
    std::vector<THashSet<int>> filledNodes(NodesNum_);
    auto checkEmplace = [&] (int cell, int nodeIdx) -> bool {
        if (CellDistribution_[Nodes_[nodeIdx]].size() == TabletSlotCount_) {
            return false;
        }

        return !filledNodes[nodeIdx].contains(cell);
    };

    SetRandomSeed(TInstant::Now().MilliSeconds());
    bool failed = false;
    for (int peer = 0; peer < PeersNum_ / 2; ++peer) {
        for (int bundleIdx = 0; bundleIdx < BundlesNum_ - 1; ++bundleIdx) {
            for (int cellIdx = 0; cellIdx < CellsNum_; ++cellIdx) {
                int startNodeIdx = RandomNumber<ui32>(NodesNum_);
                YT_ASSERT(startNodeIdx < NodesNum_);
                int nodeIdx = startNodeIdx;
                int cell = Cells_[bundleIdx][cellIdx];
                while (!failed && !checkEmplace(cell, nodeIdx)) {
                    ++nodeIdx;
                    if (nodeIdx == NodesNum_) {
                        nodeIdx = 0;
                    }
                    if (nodeIdx == startNodeIdx) {
                        failed = true;
                    }
                }
                if (failed) {
                    break;
                }

                YT_ASSERT(checkEmplace(cell, nodeIdx));
                CellDistribution_[Nodes_[nodeIdx]].emplace_back(Cells_[bundleIdx][cellIdx]);
                filledNodes[nodeIdx].insert(cell);
            }
        }
    }
}

TEST_P(TTabletCellBalancerRevokeTest, TestBalancer)
{
    auto setting = New<TSetting>(GetParam());
    auto balancer = CreateTabletCellBalancer(setting);

    for (auto& unassigned : setting->GetUnassignedPeers()) {
        balancer->AssignPeer(unassigned.first, unassigned.second);
    }

    setting->ValidateAssingment(balancer->GetTabletCellMoveDescriptors());

    for (auto& assigned : setting->GetUnassignedPeers()) {
        balancer->RevokePeer(assigned.first, assigned.second);
    }

    setting->ApplyMoveDescriptors(balancer->GetTabletCellMoveDescriptors());

    for (auto& unassigned : setting->GetUnassignedPeers()) {
        balancer->AssignPeer(unassigned.first, unassigned.second);
    }

    setting->ValidateAssingment(balancer->GetTabletCellMoveDescriptors());
}

TEST_P(TTabletCellBalancerTest, TestBalancer)
{
    auto setting = New<TSetting>(GetParam());
    auto balancer = CreateTabletCellBalancer(setting);

    for (auto& unassigned : setting->GetUnassignedPeers()) {
        balancer->AssignPeer(unassigned.first, unassigned.second);
    }

    setting->ValidateAssingment(balancer->GetTabletCellMoveDescriptors());
}

/*
 * Tuple of 5 values:
 * number of nodes,
 * number of slots per node,
 * number of peers per cell,
 * number of bundles,
 * number of cells per bundle
 */
INSTANTIATE_TEST_CASE_P(
    TabletCellBalancer,
    TTabletCellBalancerStressTest,
    ::testing::Values(
        std::make_tuple(4, 20, 2, 5, 8),
        std::make_tuple(6, 30, 4, 9, 5),
        std::make_tuple(10, 50, 4, 5, 25)
    ));

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
    TTabletCellBalancerRevokeTest,
    ::testing::Values(
        std::make_tuple(
            "{a=1;}",
            "{a=[1;2;];}",
            "{n1=[a;]; n2=[a;];}",
            1,
            "{n1=[]; n2=[];}")
    ));

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

