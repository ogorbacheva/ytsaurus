#include "table_node.h"
#include "private.h"

#include <yt/server/tablet_server/tablet.h>
#include <yt/server/tablet_server/tablet_cell_bundle.h>

namespace NYT {
namespace NTableServer {

using namespace NChunkClient::NProto;
using namespace NChunkClient;
using namespace NChunkServer;
using namespace NCypressServer;
using namespace NObjectServer;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTabletServer;
using namespace NTransactionClient;
using namespace NTransactionServer;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

static auto const& Logger = TableServerLogger;

////////////////////////////////////////////////////////////////////////////////

TTableNode::TDynamicTableAttributes::TDynamicTableAttributes()
{ }

void TTableNode::TDynamicTableAttributes::Save(NCellMaster::TSaveContext& context) const
{
    using NYT::Save;
    Save(context, Atomicity);
    Save(context, CommitOrdering);
    Save(context, UpstreamReplicaId);
    Save(context, TabletCellBundle);
    Save(context, LastCommitTimestamp);
    Save(context, TabletCountByState);
    Save(context, Tablets);
    Save(context, EnableTabletBalancer);
    Save(context, MinTabletSize);
    Save(context, MaxTabletSize);
    Save(context, DesiredTabletSize);
    Save(context, InMemoryMode);
    Save(context, DesiredTabletCount);
    Save(context, TabletErrorCount);
}

void TTableNode::TDynamicTableAttributes::Load(NCellMaster::TLoadContext& context)
{
    using NYT::Load;
    Load(context, Atomicity);
    Load(context, CommitOrdering);
    Load(context, UpstreamReplicaId);
    Load(context, TabletCellBundle);
    Load(context, LastCommitTimestamp);
    Load(context, TabletCountByState);
    Load(context, Tablets);
    //COMPAT(savrus)
    if (context.GetVersion() >= 614) {
        Load(context, EnableTabletBalancer);
        Load(context, MinTabletSize);
        Load(context, MaxTabletSize);
        Load(context, DesiredTabletSize);
    }
    //COMPAT(savrus)
    if (context.GetVersion() >= 621) {
        Load(context, InMemoryMode);
    }
    //COMPAT(savrus)
    if (context.GetVersion() >= 622) {
        Load(context, DesiredTabletCount);
    }
    //COMPAT(iskhakovt)
    if (context.GetVersion() >= 628) {
        Load(context, TabletErrorCount);
    }
}

////////////////////////////////////////////////////////////////////////////////

TTableNode::TTableNode(const TVersionedNodeId& id)
    : TChunkOwnerBase(id)
{
    if (IsTrunk()) {
        SetOptimizeFor(EOptimizeFor::Lookup);
    }
}

EObjectType TTableNode::GetObjectType() const
{
    return EObjectType::Table;
}

TTableNode* TTableNode::GetTrunkNode()
{
    return TrunkNode_->As<TTableNode>();
}

const TTableNode* TTableNode::GetTrunkNode() const
{
    return TrunkNode_->As<TTableNode>();
}

void TTableNode::BeginUpload(EUpdateMode mode)
{
    TChunkOwnerBase::BeginUpload(mode);
}

void TTableNode::EndUpload(
    const TDataStatistics* statistics,
    const TTableSchema& schema,
    ETableSchemaMode schemaMode,
    TNullable<NTableClient::EOptimizeFor> optimizeFor,
    const TNullable<TMD5Hasher>& md5Hasher)
{
    SchemaMode_ = schemaMode;
    TableSchema_ = schema;
    if (optimizeFor) {
        OptimizeFor_.Set(*optimizeFor);
    }
    TChunkOwnerBase::EndUpload(statistics, schema, schemaMode, optimizeFor, md5Hasher);
}

bool TTableNode::IsSorted() const
{
    return TableSchema_.IsSorted();
}

bool TTableNode::IsUniqueKeys() const
{
    return TableSchema_.IsUniqueKeys();
}

bool TTableNode::IsReplicated() const
{
    return GetObjectType() == EObjectType::ReplicatedTable;
}

bool TTableNode::IsPhysicallySorted() const
{
    return IsSorted() && !IsReplicated();
}

ETabletState TTableNode::GetTabletState() const
{
    auto* trunkNode = GetTrunkNode();
    if (trunkNode->Tablets().empty()) {
        return ETabletState::None;
    }
    for (auto state : TEnumTraits<ETabletState>::GetDomainValues()) {
        if (trunkNode->TabletCountByState().IsDomainValue(state)) {
            if (trunkNode->Tablets().size() == trunkNode->TabletCountByState()[state]) {
                return state;
            }
        }
    }
    return ETabletState::Mixed;
}

void TTableNode::Save(NCellMaster::TSaveContext& context) const
{
    TChunkOwnerBase::Save(context);

    using NYT::Save;
    Save(context, TableSchema_);
    Save(context, SchemaMode_);
    Save(context, OptimizeFor_);
    Save(context, RetainedTimestamp_);
    Save(context, UnflushedTimestamp_);
    TUniquePtrSerializer<>::Save(context, DynamicTableAttributes_);
}

void TTableNode::Load(NCellMaster::TLoadContext& context)
{
    TChunkOwnerBase::Load(context);

    // COMPAT(savrus)
    if (context.GetVersion() < 609) {
        LoadPre609(context);
        return;
    }

    using NYT::Load;
    Load(context, TableSchema_);
    Load(context, SchemaMode_);
    Load(context, OptimizeFor_);
    Load(context, RetainedTimestamp_);
    Load(context, UnflushedTimestamp_);
    TUniquePtrSerializer<>::Load(context, DynamicTableAttributes_);

    // NB: All COMPAT's after version 609 should be in this function.
    LoadCompatAfter609(context);
}

void TTableNode::LoadPre609(NCellMaster::TLoadContext& context)
{
    auto dynamic = std::make_unique<TDynamicTableAttributes>();

    using NYT::Load;
    Load(context, TableSchema_);
    Load(context, SchemaMode_);
    Load(context, dynamic->Tablets);
    Load(context, dynamic->Atomicity);
    // COMPAT(babenko)
    if (context.GetVersion() >= 400) {
        Load(context, dynamic->CommitOrdering);
        Load(context, dynamic->TabletCellBundle);
        Load(context, dynamic->LastCommitTimestamp);
        Load(context, RetainedTimestamp_);
        Load(context, UnflushedTimestamp_);
    }
    // COMPAT(babenko)
    if (context.GetVersion() >= 600 && context.GetVersion() <= 601) {
        Load<int>(context); // replication mode
    }
    // COMPAT(babenko)
    if (context.GetVersion() >= 602) {
        Load(context, dynamic->UpstreamReplicaId);
    }
    // COMPAT(babenko)
    if (context.GetVersion() >= 601) {
        Load(context, OptimizeFor_);
    } else {
        if (Attributes_) {
            auto& attributes = Attributes_->Attributes();
            {
                static const TString optimizeForAttributeName("optimize_for");
                auto it = attributes.find(optimizeForAttributeName);
                if (it != attributes.end()) {
                    const auto& value = it->second;
                    try {
                        OptimizeFor_.Set(NYTree::ConvertTo<EOptimizeFor>(value));
                    } catch (...) {
                    }
                    attributes.erase(it);
                }
            }
            if (Attributes_->Attributes().empty()) {
                Attributes_.reset();
            }
        }
    }
    // COMPAT(savrus)
    if (context.GetVersion() >= 607) {
        Load(context, dynamic->TabletCountByState);
    }

    // COMPAT(savrus)
    if (!dynamic->Tablets.empty() ||
        dynamic->Atomicity != DefaultDynamicTableAttributes_.Atomicity ||
        dynamic->CommitOrdering != DefaultDynamicTableAttributes_.CommitOrdering ||
        dynamic->UpstreamReplicaId != DefaultDynamicTableAttributes_.UpstreamReplicaId ||
        dynamic->TabletCellBundle != DefaultDynamicTableAttributes_.TabletCellBundle ||
        dynamic->LastCommitTimestamp != DefaultDynamicTableAttributes_.LastCommitTimestamp)
    {
        DynamicTableAttributes_ = std::move(dynamic);
    }

    // NB: All COMPAT's after version 609 should be in this function.
    LoadCompatAfter609(context);
}

void TTableNode::LoadCompatAfter609(NCellMaster::TLoadContext& context)
{
    //COMPAT(savrus)
    if (context.GetVersion() < 622) {
        if (Attributes_) {
            auto& attributes = Attributes_->Attributes();

            auto processAttribute = [&] (
                const TString& attributeName,
                std::function<void(const TYsonString&)> functor)
            {
                auto it = attributes.find(attributeName);
                if (it != attributes.end()) {
                    LOG_DEBUG("Change attribute from custom to builtin (AttributeName: %Qv, AttributeValue: %v, TableId: %v)",
                        attributeName,
                        ConvertToYsonString(it->second, EYsonFormat::Text),
                        Id_);
                    try {
                        functor(it->second);
                    } catch (...) {
                    }
                    attributes.erase(it);
                }
            };
            static const TString disableTabletBalancerAttributeName("disable_tablet_balancer");
            static const TString enableTabletBalancerAttributeName("enable_tablet_balancer");
            static const TString minTabletSizeAttributeName("min_tablet_size");
            static const TString maxTabletSizeAttributeName("max_tablet_size");
            static const TString desiredTabletSizeAttributeName("desired_tablet_size");
            static const TString desiredTabletCountAttributeName("desired_tablet_count");
            static const TString inMemoryModeAttributeName("in_memory_mode");
            processAttribute(disableTabletBalancerAttributeName, [&] (const TYsonString& val) {
                SetEnableTabletBalancer(!ConvertTo<bool>(val));
            });
            processAttribute(enableTabletBalancerAttributeName, [&] (const TYsonString& val) {
                SetEnableTabletBalancer(ConvertTo<bool>(val));
            });
            processAttribute(minTabletSizeAttributeName, [&] (const TYsonString& val) {
                SetMinTabletSize(ConvertTo<i64>(val));
            });
            processAttribute(maxTabletSizeAttributeName, [&] (const TYsonString& val) {
                SetMaxTabletSize(ConvertTo<i64>(val));
            });
            processAttribute(desiredTabletSizeAttributeName, [&] (const TYsonString& val) {
                SetDesiredTabletSize(ConvertTo<i64>(val));
            });
            processAttribute(desiredTabletCountAttributeName, [&] (const TYsonString& val) {
                SetDesiredTabletCount(ConvertTo<int>(val));
            });
            processAttribute(inMemoryModeAttributeName, [&] (const TYsonString& val) {
                SetInMemoryMode(ConvertTo<EInMemoryMode>(val));
            });

            if (attributes.empty()) {
                Attributes_.reset();
            }
        }
    }
}

std::pair<TTableNode::TTabletListIterator, TTableNode::TTabletListIterator> TTableNode::GetIntersectingTablets(
    const TOwningKey& minKey,
    const TOwningKey& maxKey)
{
    auto* trunkNode = GetTrunkNode();

    auto beginIt = std::upper_bound(
        trunkNode->Tablets().cbegin(),
        trunkNode->Tablets().cend(),
        minKey,
        [] (const TOwningKey& key, const TTablet* tablet) {
            return key < tablet->GetPivotKey();
        });

    if (beginIt != trunkNode->Tablets().cbegin()) {
        --beginIt;
    }

    auto endIt = beginIt;
    while (endIt != trunkNode->Tablets().cend() && maxKey >= (*endIt)->GetPivotKey()) {
        ++endIt;
    }

    return std::make_pair(beginIt, endIt);
}

bool TTableNode::IsDynamic() const
{
    return !GetTrunkNode()->Tablets().empty();
}

bool TTableNode::IsEmpty() const
{
    return ComputeTotalStatistics().chunk_count() == 0;
}

TTimestamp TTableNode::GetCurrentUnflushedTimestamp(
    TTimestamp latestTimestamp) const
{
    return UnflushedTimestamp_ != NullTimestamp
        ? UnflushedTimestamp_
        : CalculateUnflushedTimestamp(latestTimestamp);
}

TTimestamp TTableNode::GetCurrentRetainedTimestamp() const
{
    return RetainedTimestamp_ != NullTimestamp
        ? RetainedTimestamp_
        : CalculateRetainedTimestamp();
}

TTimestamp TTableNode::CalculateUnflushedTimestamp(
    TTimestamp latestTimestamp) const
{
    auto* trunkNode = GetTrunkNode();
    if (!trunkNode->IsDynamic()) {
        return NullTimestamp;
    }

    auto result = MaxTimestamp;
    for (const auto* tablet : trunkNode->Tablets()) {
        auto timestamp = tablet->GetState() != ETabletState::Unmounted
            ? static_cast<TTimestamp>(tablet->NodeStatistics().unflushed_timestamp())
            : latestTimestamp;
        result = std::min(result, timestamp);
    }
    return result;
}

TTimestamp TTableNode::CalculateRetainedTimestamp() const
{
    auto* trunkNode = GetTrunkNode();
    if (!trunkNode->IsDynamic()) {
        return NullTimestamp;
    }

    auto result = MinTimestamp;
    for (const auto* tablet : trunkNode->Tablets()) {
        auto timestamp = tablet->GetRetainedTimestamp();
        result = std::max(result, timestamp);
    }
    return result;
}

DEFINE_EXTRA_PROPERTY_HOLDER(TTableNode, TTableNode::TDynamicTableAttributes, DynamicTableAttributes);

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableServer
} // namespace NYT

