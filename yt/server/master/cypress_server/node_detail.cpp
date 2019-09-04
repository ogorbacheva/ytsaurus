#include "node_detail.h"
#include "helpers.h"
#include "node_proxy_detail.h"
#include "portal_exit_node.h"
#include "shard.h"
#include "resolve_cache.h"

#include <yt/server/master/cell_master/hydra_facade.h>
#include <yt/server/master/cell_master/config.h>
#include <yt/server/master/cell_master/config_manager.h>

#include <yt/server/master/security_server/account.h>
#include <yt/server/master/security_server/user.h>

#include <yt/client/object_client/helpers.h>

namespace NYT::NCypressServer {

using namespace NYTree;
using namespace NYson;
using namespace NSecurityServer;
using namespace NTransactionServer;
using namespace NCellMaster;
using namespace NObjectClient;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

const EObjectType TCypressScalarTypeTraits<TString>::ObjectType = EObjectType::StringNode;
const ENodeType   TCypressScalarTypeTraits<TString>::NodeType   = ENodeType::String;
const EObjectType TCypressScalarTypeTraits<i64>::ObjectType    = EObjectType::Int64Node;
const ENodeType   TCypressScalarTypeTraits<i64>::NodeType      = ENodeType::Int64;
const EObjectType TCypressScalarTypeTraits<ui64>::ObjectType   = EObjectType::Uint64Node;
const ENodeType   TCypressScalarTypeTraits<ui64>::NodeType     = ENodeType::Uint64;
const EObjectType TCypressScalarTypeTraits<double>::ObjectType = EObjectType::DoubleNode;
const ENodeType   TCypressScalarTypeTraits<double>::NodeType   = ENodeType::Double;
const EObjectType TCypressScalarTypeTraits<bool>::ObjectType   = EObjectType::BooleanNode;
const ENodeType   TCypressScalarTypeTraits<bool>::NodeType     = ENodeType::Boolean;

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

TNontemplateCypressNodeTypeHandlerBase::TNontemplateCypressNodeTypeHandlerBase(
    NCellMaster::TBootstrap* bootstrap)
    : Bootstrap_(bootstrap)
{ }

ETypeFlags TNontemplateCypressNodeTypeHandlerBase::GetFlags() const
{
    return
        ETypeFlags::ReplicateAttributes |
        ETypeFlags::ReplicateDestroy |
        ETypeFlags::Creatable;
}

void TNontemplateCypressNodeTypeHandlerBase::FillAttributes(
    TCypressNode* trunkNode,
    IAttributeDictionary* inheritedAttributes,
    IAttributeDictionary* explicitAttributes)
{
    for (const auto& key : inheritedAttributes->ListKeys()) {
        if (!IsSupportedInheritableAttribute(key)) {
            inheritedAttributes->Remove(key);
        }
    }

    const auto& objectManager = Bootstrap_->GetObjectManager();
    auto combinedAttributes = NYTree::OverlayAttributeDictionaries(explicitAttributes, inheritedAttributes);
    objectManager->FillAttributes(trunkNode, combinedAttributes);
}

bool TNontemplateCypressNodeTypeHandlerBase::IsSupportedInheritableAttribute(const TString&) const
{
    // NB: most node types don't inherit attributes. That would lead to
    // a lot of pseudo-user attributes.
    return false;
}


bool TNontemplateCypressNodeTypeHandlerBase::IsLeader() const
{
    return Bootstrap_->GetHydraFacade()->GetHydraManager()->IsLeader();
}

bool TNontemplateCypressNodeTypeHandlerBase::IsRecovery() const
{
    return Bootstrap_->GetHydraFacade()->GetHydraManager()->IsRecovery();
}

const TDynamicCypressManagerConfigPtr& TNontemplateCypressNodeTypeHandlerBase::GetDynamicCypressManagerConfig() const
{
    return Bootstrap_->GetConfigManager()->GetConfig()->CypressManager;
}

void TNontemplateCypressNodeTypeHandlerBase::DestroyCore(TCypressNode* node)
{
    // Reset parent links from immediate descendants.
    for (auto* descendant : node->ImmediateDescendants()) {
        descendant->ResetParent();
    }
    node->ImmediateDescendants().clear();
    node->SetParent(nullptr);

    if (node->IsTrunk()) {
        // Reset reference to shard.
        const auto& cypressManager = Bootstrap_->GetCypressManager();
        cypressManager->ResetShard(node);

        // Invalidate resolve cache.
        const auto& resolveCache = cypressManager->GetResolveCache();
        resolveCache->InvalidateNode(node);
    }

    // Clear ACD to unregister the node from linked objects.
    node->Acd().Clear();
}

void TNontemplateCypressNodeTypeHandlerBase::BeginCopyCore(
    TCypressNode* node,
    TBeginCopyContext* context)
{
    using NYT::Save;

    Save(*context, node->GetId());
    Save(*context, node->GetType());
    Save(*context, node->GetAccount());
    Save(*context, node->GetTotalResourceUsage());
    Save(*context, node->GetExternalCellTag());
    Save(*context, node->Acd());
    Save(*context, node->GetOpaque());
    Save(*context, node->GetCreationTime());
    Save(*context, node->TryGetExpirationTime());

    // User attributes
    auto keyToAttribute = GetNodeAttributes(
        Bootstrap_->GetCypressManager(),
        node->GetTrunkNode(),
        node->GetTransaction());
    Save(*context, SortHashMapByKeys(keyToAttribute));

    // For externalizable nodes, lock the source to ensure it survives until EndCopy.
    if (node->GetExternalCellTag() != NotReplicatedCellTag) {
        const auto& cypressManager = Bootstrap_->GetCypressManager();
        cypressManager->LockNode(
            node,
            context->GetTransaction(),
            context->GetRemoveSource() ? ELockMode::Exclusive : ELockMode::Snapshot);
    }
}

TCypressNode* TNontemplateCypressNodeTypeHandlerBase::EndCopyCore(
    TEndCopyContext* context,
    ICypressNodeFactory* factory,
    TNodeId sourceNodeId)
{
    // See BeginCopyCore.
    // NB: The type is already deserialized.
    auto* sourceAccount = Load<TAccount*>(*context);
    auto sourceResourceUsage = Load<TClusterResources>(*context);

    auto* clonedAccount = factory->GetClonedNodeAccount(sourceAccount);
    factory->ValidateClonedAccount(
        context->GetMode(),
        sourceAccount,
        sourceResourceUsage,
        clonedAccount);

    auto externalCellTag = Load<TCellTag>(*context);
    if (externalCellTag == Bootstrap_->GetCellTag()) {
        THROW_ERROR_EXCEPTION("Cannot copy node %v to cell %v since the latter is its external cell",
            sourceNodeId,
            externalCellTag);
    }

    const auto& objectManager = Bootstrap_->GetObjectManager();
    auto clonedId = objectManager->GenerateId(GetObjectType(), NullObjectId);
    auto* clonedTrunkNode = factory->InstantiateNode(clonedId, externalCellTag);

    const auto& securityManager = Bootstrap_->GetSecurityManager();
    securityManager->SetAccount(
        clonedTrunkNode,
        /* oldAccount */ nullptr,
        clonedAccount,
        /* transaction */ nullptr);

    // Set owner.
    auto* user = securityManager->GetAuthenticatedUser();
    clonedTrunkNode->Acd().SetOwner(user);

    // Copy ACD, but only in move.
    auto sourceAcd = Load<TAccessControlDescriptor>(*context);
    if (context->GetMode() == ENodeCloneMode::Move) {
        clonedTrunkNode->Acd().SetInherit(sourceAcd.GetInherit());
        for (const auto& ace : sourceAcd.Acl().Entries) {
            clonedTrunkNode->Acd().AddEntry(ace);
        }
    }

    // Copy opaque.
    auto opaque = Load<bool>(*context);
    clonedTrunkNode->SetOpaque(opaque);

    // Copy creation time.
    auto creationTime = Load<TInstant>(*context);
    if (factory->ShouldPreserveCreationTime()) {
        clonedTrunkNode->SetCreationTime(creationTime);
    }

    // Copy expiration time.
    auto expirationTime = Load<std::optional<TInstant>>(*context);
    if (factory->ShouldPreserveExpirationTime() && expirationTime) {
        clonedTrunkNode->SetExpirationTime(*expirationTime);
    }

    // Copy attributes directly to suppress validation.
    auto keyToAttribute = Load<std::vector<std::pair<TString, TYsonString>>>(*context);
    if (!keyToAttribute.empty()) {
        auto* clonedAttributes = clonedTrunkNode->GetMutableAttributes();
        for (const auto& pair : keyToAttribute) {
            YT_VERIFY(clonedAttributes->Attributes().insert(pair).second);
        }
    }

    return clonedTrunkNode;
}

void TNontemplateCypressNodeTypeHandlerBase::BranchCore(
    TCypressNode* originatingNode,
    TCypressNode* branchedNode,
    TTransaction* transaction,
    const TLockRequest& lockRequest)
{
    const auto& objectManager = Bootstrap_->GetObjectManager();

    // Invalidate resolve cache.
    if (lockRequest.Mode != ELockMode::Snapshot) {
        const auto& cypressManager = Bootstrap_->GetCypressManager();
        const auto& resolveCache = cypressManager->GetResolveCache();
        resolveCache->InvalidateNode(originatingNode);
    }

    // Copy basic properties.
    branchedNode->SetParent(originatingNode->GetParent());
    branchedNode->SetCreationTime(originatingNode->GetCreationTime());
    branchedNode->SetModificationTime(originatingNode->GetModificationTime());
    branchedNode->SetAttributesRevision(originatingNode->GetAttributesRevision());
    branchedNode->SetContentRevision(originatingNode->GetContentRevision());
    branchedNode->SetLockMode(lockRequest.Mode);
    branchedNode->SetTrunkNode(originatingNode->GetTrunkNode());
    branchedNode->SetTransaction(transaction);
    branchedNode->SetOriginator(originatingNode);
    branchedNode->SetExternalCellTag(originatingNode->GetExternalCellTag());
    if (originatingNode->IsForeign()) {
        branchedNode->SetForeign();
    }
    branchedNode->SetOpaque(originatingNode->GetOpaque());

    // Copying node's account requires special handling.
    YT_VERIFY(!branchedNode->GetAccount());
    const auto& securityManager = Bootstrap_->GetSecurityManager();
    auto* account = originatingNode->GetAccount();
    securityManager->SetAccount(branchedNode, nullptr, account, transaction);

    // Branch user attributes.
    objectManager->BranchAttributes(originatingNode, branchedNode);
}

void TNontemplateCypressNodeTypeHandlerBase::MergeCore(
    TCypressNode* originatingNode,
    TCypressNode* branchedNode)
{
    const auto& objectManager = Bootstrap_->GetObjectManager();

    // Merge user attributes.
    objectManager->MergeAttributes(originatingNode, branchedNode);

    // Perform cleanup by resetting the parent link of the branched node.
    branchedNode->SetParent(nullptr);

    // Merge modification time.
    const auto* mutationContext = NHydra::GetCurrentMutationContext();
    originatingNode->SetModificationTime(mutationContext->GetTimestamp());
    originatingNode->SetAttributesRevision(mutationContext->GetVersion().ToRevision());
    originatingNode->SetContentRevision(mutationContext->GetVersion().ToRevision());
}

TCypressNode* TNontemplateCypressNodeTypeHandlerBase::CloneCorePrologue(
    ICypressNodeFactory* factory,
    TNodeId hintId,
    TCypressNode* sourceNode,
    TAccount* account)
{
    auto type = GetObjectType();
    const auto& objectManager = Bootstrap_->GetObjectManager();
    auto clonedId = hintId
        ? hintId
        : objectManager->GenerateId(type, NullObjectId);

    auto* clonedTrunkNode = factory->InstantiateNode(clonedId, sourceNode->GetExternalCellTag());

    const auto& securityManager = Bootstrap_->GetSecurityManager();
    securityManager->SetAccount(
        clonedTrunkNode,
        /* oldAccount */ nullptr,
        account,
        /* transaction */ nullptr);

    return clonedTrunkNode;
}

void TNontemplateCypressNodeTypeHandlerBase::CloneCoreEpilogue(
    TCypressNode* sourceNode,
    TCypressNode* clonedTrunkNode,
    ICypressNodeFactory* factory,
    ENodeCloneMode mode)
{
    // Copy attributes directly to suppress validation.
    auto keyToAttribute = GetNodeAttributes(
        Bootstrap_->GetCypressManager(),
        sourceNode->GetTrunkNode(),
        factory->GetTransaction());
    if (!keyToAttribute.empty()) {
        auto* clonedAttributes = clonedTrunkNode->GetMutableAttributes();
        for (const auto& pair : keyToAttribute) {
            YT_VERIFY(clonedAttributes->Attributes().insert(pair).second);
        }
    }

    // Copy ACD, but only in move.
    if (mode == ENodeCloneMode::Move) {
        clonedTrunkNode->Acd().SetInherit(sourceNode->Acd().GetInherit());
        for (const auto& ace : sourceNode->Acd().Acl().Entries) {
            clonedTrunkNode->Acd().AddEntry(ace);
        }
    }

    // Copy builtin attributes.
    clonedTrunkNode->SetOpaque(sourceNode->GetOpaque());
    if (mode == ENodeCloneMode::Move) {
        clonedTrunkNode->SetCreationTime(sourceNode->GetCreationTime());
    }
}

////////////////////////////////////////////////////////////////////////////////

bool TCompositeNodeBase::TAttributes::operator==(const TAttributes& rhs) const
{
    if (this == &rhs) {
        return true;
    }

#define XX(camelCaseName, snakeCaseName) \
    && camelCaseName == rhs.camelCaseName

    return true FOR_EACH_INHERITABLE_ATTRIBUTE(XX);

#undef XX
}

bool TCompositeNodeBase::TAttributes::operator!=(const TAttributes& rhs) const
{
    return !(*this == rhs);
}

void TCompositeNodeBase::TAttributes::Persist(NCellMaster::TPersistenceContext& context)
{
#define XX(camelCaseName, snakeCaseName) \
    Persist(context, camelCaseName);

    using NYT::Persist;
    FOR_EACH_INHERITABLE_ATTRIBUTE(XX);

#undef XX
}

void TCompositeNodeBase::TAttributes::Persist(NCypressServer::TCopyPersistenceContext& context)
{
//#define XX(camelCaseName, snakeCaseName) \
//    Persist(context, camelCaseName);
//
//    using NYT::Persist;
//    FOR_EACH_INHERITABLE_ATTRIBUTE(XX);
//
//#undef XX
}

void TCompositeNodeBase::TAttributes::Save(NCellMaster::TSaveContext& context) const
{
    SaveViaPersist<NCellMaster::TPersistenceContext>(context, *this);
}

void TCompositeNodeBase::TAttributes::Load(NCellMaster::TLoadContext& context)
{
    LoadViaPersist<NCellMaster::TPersistenceContext>(context, *this);
}

void TCompositeNodeBase::TAttributes::Save(NCypressServer::TBeginCopyContext& context) const
{
    SaveViaPersist<NCypressServer::TCopyPersistenceContext>(context, *this);
}

void TCompositeNodeBase::TAttributes::Load(NCypressServer::TEndCopyContext& context)
{
    LoadViaPersist<NCypressServer::TCopyPersistenceContext>(context, *this);
}

bool TCompositeNodeBase::TAttributes::AreFull() const
{
#define XX(camelCaseName, snakeCaseName) \
    && camelCaseName

    return true FOR_EACH_INHERITABLE_ATTRIBUTE(XX);

#undef XX
}

bool TCompositeNodeBase::TAttributes::AreEmpty() const
{
#define XX(camelCaseName, snakeCaseName) \
    && !camelCaseName

    return true FOR_EACH_INHERITABLE_ATTRIBUTE(XX);

#undef XX
}

void TCompositeNodeBase::Save(NCellMaster::TSaveContext& context) const
{
    TCypressNode::Save(context);

    using NYT::Save;
    TUniquePtrSerializer<>::Save(context, Attributes_);
}

void TCompositeNodeBase::Load(NCellMaster::TLoadContext& context)
{
    TCypressNode::Load(context);

    using NYT::Load;
    TUniquePtrSerializer<>::Load(context, Attributes_);
}

bool TCompositeNodeBase::HasInheritableAttributes() const
{
    if (Attributes_) {
        YT_ASSERT(!Attributes_->AreEmpty());
        return true;
    } else {
        return false;
    }
}

const TCompositeNodeBase::TAttributes* TCompositeNodeBase::Attributes() const
{
    return Attributes_.get();
}

void TCompositeNodeBase::SetAttributes(const TCompositeNodeBase::TAttributes* attributes)
{
    if (!attributes || attributes->AreEmpty()) {
        Attributes_.reset();
    } else if (Attributes_) {
        *Attributes_ = *attributes;
    } else {
        Attributes_ = std::make_unique<TAttributes>(*attributes);
    }
}

#define IMPLEMENT_ATTRIBUTE_ACCESSORS(camelCaseName, snakeCaseName) \
auto TCompositeNodeBase::Get##camelCaseName() const -> decltype(TCompositeNodeBase::TAttributes::camelCaseName) \
{ \
    if (Attributes_) { \
        return Attributes_->camelCaseName; \
    } else { \
        return {}; \
    } \
} \
void TCompositeNodeBase::Set##camelCaseName(decltype(TCompositeNodeBase::TAttributes::camelCaseName) value) \
{ \
    if (Attributes_) { \
        Attributes_->camelCaseName = value; \
        if (Attributes_->AreEmpty()) { \
            Attributes_.reset(); \
        } \
    } else if (value) { \
        Attributes_ = std::make_unique<TAttributes>(); \
        Attributes_->camelCaseName = value; \
    } \
}

FOR_EACH_INHERITABLE_ATTRIBUTE(IMPLEMENT_ATTRIBUTE_ACCESSORS)

#undef IMPLEMENT_ATTRIBUTE_ACCESSORS

////////////////////////////////////////////////////////////////////////////////

TMapNodeChildren::~TMapNodeChildren()
{
    YT_VERIFY(KeyToChild.empty());
    YT_VERIFY(ChildToKey.empty());
}

void TMapNodeChildren::Save(NCellMaster::TSaveContext& context) const
{
    using NYT::Save;

    Save(context, KeyToChild);
}

void TMapNodeChildren::Load(NCellMaster::TLoadContext& context)
{
    using NYT::Load;

    Load(context, KeyToChild);

    // Reconstruct ChildToKey map.
    for (const auto& [key, childNode] : KeyToChild) {
        if (childNode) {
            YT_VERIFY(ChildToKey.insert(std::make_pair(childNode, key)).second);
        }
    }
}

/*static*/ void TMapNodeChildren::Destroy(
    TMapNodeChildren* children,
    const TObjectManagerPtr& objectManager)
{
    YT_VERIFY(children->GetRefCount() == 0);
    children->UnrefChildren(objectManager);

    children->KeyToChild.clear();
    children->ChildToKey.clear();

    delete children;
}

/*static*/ TMapNodeChildren* TMapNodeChildren::Copy(
    TMapNodeChildren* srcChildren,
    const TObjectManagerPtr& objectManager)
{
    YT_VERIFY(srcChildren->GetRefCount() != 0);

    auto holder = std::make_unique<TMapNodeChildren>();
    holder->KeyToChild = srcChildren->KeyToChild;
    holder->ChildToKey = srcChildren->ChildToKey;

    holder->RefChildren(objectManager);

    return holder.release();
}

void TMapNodeChildren::RefChildren(const NObjectServer::TObjectManagerPtr& objectManager)
{
    // Make sure we handle children in a stable order.
    for (const auto& [key, childNode] : SortHashMapByKeys(KeyToChild)) {
        if (childNode) {
            objectManager->RefObject(childNode);
        }
    }
}

void TMapNodeChildren::UnrefChildren(const NObjectServer::TObjectManagerPtr& objectManager)
{
    // Make sure we handle children in a stable order.
    for (const auto& [key, childNode] : SortHashMapByKeys(KeyToChild)) {
        if (childNode) {
            objectManager->UnrefObject(childNode);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

TMapNode::TMapNode(const TVersionedNodeId& id)
    : TCompositeNodeBase(id)
{ }

TMapNode::~TMapNode() = default;

const TMapNode::TKeyToChild& TMapNode::KeyToChild() const
{
    return Children_.Get().KeyToChild;
}

const TMapNode::TChildToKey& TMapNode::ChildToKey() const
{
    return Children_.Get().ChildToKey;
}

TMapNode::TKeyToChild& TMapNode::MutableKeyToChild(const TObjectManagerPtr& objectManager)
{
    return Children_.MutableGet(objectManager).KeyToChild;
}

TMapNode::TChildToKey& TMapNode::MutableChildToKey(const TObjectManagerPtr& objectManager)
{
    return Children_.MutableGet(objectManager).ChildToKey;
}

ENodeType TMapNode::GetNodeType() const
{
    return ENodeType::Map;
}

void TMapNode::Save(NCellMaster::TSaveContext& context) const
{
    TCompositeNodeBase::Save(context);

    using NYT::Save;
    Save(context, ChildCountDelta_);
    Save(context, Children_);
}

void TMapNode::Load(NCellMaster::TLoadContext& context)
{
    TCompositeNodeBase::Load(context);

    using NYT::Load;

    Load(context, ChildCountDelta_);

    // COMPAT(shakurov)
    if (context.GetVersion() < EMasterReign::SnapshotLockableMapNodes) {
        Children_.ResetToDefaultConstructed();
        // Passing a nullptr as the object manager is a dirty hack: in this
        // particular case, we're sure there's no CoW sharing, and the object
        // manager won't actually be used.
        auto& keyToChild = MutableKeyToChild(nullptr);
        auto& childToKey = MutableChildToKey(nullptr);
        TMapSerializer<
            TDefaultSerializer,
            TNonversionedObjectRefSerializer
        >::Load(context, keyToChild);

        // Reconstruct ChildToKey map.
        for (const auto& [key, childNode] : keyToChild) {
            if (childNode) {
                YT_VERIFY(childToKey.insert(std::make_pair(childNode, key)).second);
            }
        }
    } else {
        Load(context, Children_);
    }
}

int TMapNode::GetGCWeight() const
{
    return TObject::GetGCWeight() + KeyToChild().size();
}

////////////////////////////////////////////////////////////////////////////////

template <class TImpl>
EObjectType TMapNodeTypeHandlerImpl<TImpl>::GetObjectType() const
{
    return EObjectType::MapNode;
}

template <class TImpl>
ENodeType TMapNodeTypeHandlerImpl<TImpl>::GetNodeType() const
{
    return ENodeType::Map;
}

template <class TImpl>
void TMapNodeTypeHandlerImpl<TImpl>::DoDestroy(TImpl* node)
{
    TBase::DoDestroy(node);

    node->ChildCountDelta_ = 0;
    node->Children_.Reset(this->Bootstrap_->GetObjectManager());
}

template <class TImpl>
void TMapNodeTypeHandlerImpl<TImpl>::DoBranch(
    const TImpl* originatingNode,
    TImpl* branchedNode,
    const TLockRequest& lockRequest)
{
    TBase::DoBranch(originatingNode, branchedNode, lockRequest);

    YT_VERIFY(!branchedNode->Children_);

    if (lockRequest.Mode == ELockMode::Snapshot) {
        const auto& objectManager = this->Bootstrap_->GetObjectManager();

        if (originatingNode->IsTrunk()) {
            branchedNode->ChildCountDelta() = originatingNode->ChildCountDelta();
            branchedNode->Children_.Assign(originatingNode->Children_, objectManager);
        } else {
            const auto& cypressManager = this->Bootstrap_->GetCypressManager();

            THashMap<TString, TCypressNode*> keyToChildStorage;
            const auto& originatingNodeChildren = GetMapNodeChildMap(
                cypressManager,
                originatingNode->GetTrunkNode()->template As<TMapNode>(),
                originatingNode->GetTransaction(),
                &keyToChildStorage);

            branchedNode->ChildCountDelta() = originatingNodeChildren.size();
            branchedNode->MutableKeyToChild(objectManager) = originatingNodeChildren;
            for (const auto& [key, childNode] : SortHashMapByKeys(branchedNode->KeyToChild())) {
                if (childNode) {
                    objectManager->RefObject(childNode);
                }
            }
        }
    }

    // Non-snapshot branches only hold changes, i.e. deltas. Which are empty at first.
}

template <class TImpl>
void TMapNodeTypeHandlerImpl<TImpl>::DoMerge(
    TImpl* originatingNode,
    TImpl* branchedNode)
{
    TBase::DoMerge(originatingNode, branchedNode);

    const auto& objectManager = this->Bootstrap_->GetObjectManager();

    bool isOriginatingNodeBranched = originatingNode->GetTransaction() != nullptr;

    auto& keyToChild = originatingNode->MutableKeyToChild(objectManager);
    auto& childToKey = originatingNode->MutableChildToKey(objectManager);

    for (const auto& pair : SortHashMapByKeys(branchedNode->KeyToChild())) {
        const auto& key = pair.first;
        auto* trunkChildNode = pair.second;

        auto it = keyToChild.find(key);
        if (trunkChildNode) {

            objectManager->RefObject(trunkChildNode);

            if (it == keyToChild.end()) {
                // Originating: missing
                YT_VERIFY(childToKey.insert(std::make_pair(trunkChildNode, key)).second);
                YT_VERIFY(keyToChild.insert(std::make_pair(key, trunkChildNode)).second);
            } else if (it->second) {
                // Originating: present
                objectManager->UnrefObject(it->second);
                YT_VERIFY(childToKey.erase(it->second) == 1);
                YT_VERIFY(childToKey.insert(std::make_pair(trunkChildNode, key)).second);
                it->second = trunkChildNode;
            } else {
                // Originating: tombstone
                it->second = trunkChildNode;
                YT_VERIFY(childToKey.insert(std::make_pair(trunkChildNode, key)).second);
            }
        } else {
            // Branched: tombstone
            if (it == keyToChild.end()) {
                // Originating: missing
                if (isOriginatingNodeBranched) {
                    YT_VERIFY(keyToChild.insert(std::make_pair(key, nullptr)).second);
                }
            } else if (it->second) {
                // Originating: present
                objectManager->UnrefObject(it->second);
                YT_VERIFY(childToKey.erase(it->second) == 1);
                if (isOriginatingNodeBranched) {
                    it->second = nullptr;
                } else {
                    keyToChild.erase(it);
                }
            } else {
                // Originating: tombstone
            }
        }
    }

    originatingNode->ChildCountDelta() += branchedNode->ChildCountDelta();

    branchedNode->Children_.Reset(objectManager);
}

template <class TImpl>
ICypressNodeProxyPtr TMapNodeTypeHandlerImpl<TImpl>::DoGetProxy(
    TImpl* trunkNode,
    TTransaction* transaction)
{
    return New<TMapNodeProxy>(
        this->Bootstrap_,
        &this->Metadata_,
        transaction,
        trunkNode);
}

template <class TImpl>
void TMapNodeTypeHandlerImpl<TImpl>::DoClone(
    TImpl* sourceNode,
    TImpl* clonedTrunkNode,
    ICypressNodeFactory* factory,
    ENodeCloneMode mode,
    TAccount* account)
{
    TBase::DoClone(sourceNode, clonedTrunkNode, factory, mode, account);

    auto* transaction = factory->GetTransaction();

    const auto& cypressManager = this->Bootstrap_->GetCypressManager();

    THashMap<TString, TCypressNode*> keyToChildMapStorage;
    const auto& keyToChildMap = GetMapNodeChildMap(
        cypressManager,
        sourceNode->GetTrunkNode()->template As<TMapNode>(),
        transaction,
        &keyToChildMapStorage);
    auto keyToChildList = SortHashMapByKeys(keyToChildMap);

    const auto& objectManager = this->Bootstrap_->GetObjectManager();
    auto& clonedNodeKeyToChild = clonedTrunkNode->MutableKeyToChild(objectManager);
    auto& clonedNodeChildToKey = clonedTrunkNode->MutableChildToKey(objectManager);

    for (const auto& pair : keyToChildList) {
        const auto& key = pair.first;
        auto* trunkChildNode = pair.second;

        auto* childNode = cypressManager->GetVersionedNode(trunkChildNode, transaction);

        auto* clonedChildNode = factory->CloneNode(childNode, mode);
        auto* clonedTrunkChildNode = clonedChildNode->GetTrunkNode();

        YT_VERIFY(clonedNodeKeyToChild.insert(std::make_pair(key, clonedTrunkChildNode)).second);
        YT_VERIFY(clonedNodeChildToKey.insert(std::make_pair(clonedTrunkChildNode, key)).second);

        AttachChild(objectManager, clonedTrunkNode, clonedChildNode);

        ++clonedTrunkNode->ChildCountDelta();
    }
}

template <class TImpl>
bool TMapNodeTypeHandlerImpl<TImpl>::HasBranchedChangesImpl(TImpl* originatingNode, TImpl* branchedNode)
{
    if (TBase::HasBranchedChangesImpl(originatingNode, branchedNode)) {
        return true;
    }

    if (branchedNode->GetLockMode() == ELockMode::Snapshot) {
        return false;
    }

    return !branchedNode->KeyToChild().empty();
}

template <class TImpl>
void TMapNodeTypeHandlerImpl<TImpl>::DoBeginCopy(
    TImpl* node,
    TBeginCopyContext* context)
{
    TBase::DoBeginCopy(node, context);

    using NYT::Save;

    THashMap<TString, TCypressNode*> keyToChildMapStorage;
    const auto& keyToChildMap = GetMapNodeChildMap(
        this->Bootstrap_->GetCypressManager(),
        node->GetTrunkNode()->template As<TImpl>(),
        node->GetTransaction(),
        &keyToChildMapStorage);

    TSizeSerializer::Save(*context, keyToChildMap.size());
    const auto& cypressManager = this->Bootstrap_->GetCypressManager();
    for (const auto& [key, child] : SortHashMapByKeys(keyToChildMap)) {
        Save(*context, key);
        const auto& typeHandler = cypressManager->GetHandler(child);
        typeHandler->BeginCopy(child, context);
    }
}

template <class TImpl>
void TMapNodeTypeHandlerImpl<TImpl>::DoEndCopy(
    TImpl* trunkNode,
    TEndCopyContext* context,
    ICypressNodeFactory* factory)
{
    TBase::DoEndCopy(trunkNode, context, factory);

    using NYT::Load;

    const auto& objectManager = this->Bootstrap_->GetObjectManager();
    auto& keyToChild = trunkNode->MutableKeyToChild(objectManager);
    auto& childToKey = trunkNode->MutableChildToKey(objectManager);

    size_t size = TSizeSerializer::Load(*context);
    for (size_t index = 0; index < size; ++index) {
        auto key = Load<TString>(*context);

        auto* childNode = factory->EndCopyNode(context);
        auto* trunkChildNode = childNode->GetTrunkNode();

        YT_VERIFY(keyToChild.insert(std::make_pair(key, trunkChildNode)).second);
        YT_VERIFY(childToKey.insert(std::make_pair(trunkChildNode, key)).second);

        AttachChild(objectManager, trunkNode->GetTrunkNode(), childNode);

        ++trunkNode->ChildCountDelta();
    }
}

// Explicit instantiations.
template class TMapNodeTypeHandlerImpl<TMapNode>;
template class TMapNodeTypeHandlerImpl<TPortalExitNode>;

////////////////////////////////////////////////////////////////////////////////

ENodeType TListNode::GetNodeType() const
{
    return ENodeType::List;
}

void TListNode::Save(NCellMaster::TSaveContext& context) const
{
    TCompositeNodeBase::Save(context);

    using NYT::Save;
    TVectorSerializer<
        TNonversionedObjectRefSerializer
    >::Save(context, IndexToChild_);
}

void TListNode::Load(NCellMaster::TLoadContext& context)
{
    TCompositeNodeBase::Load(context);

    using NYT::Load;
    TVectorSerializer<
        TNonversionedObjectRefSerializer
    >::Load(context, IndexToChild_);

    // Reconstruct ChildToIndex.
    for (int index = 0; index < IndexToChild_.size(); ++index) {
        YT_VERIFY(ChildToIndex_.insert(std::make_pair(IndexToChild_[index], index)).second);
    }
}

int TListNode::GetGCWeight() const
{
    return TObject::GetGCWeight() + IndexToChild_.size();
}

////////////////////////////////////////////////////////////////////////////////

EObjectType TListNodeTypeHandler::GetObjectType() const
{
    return EObjectType::ListNode;
}

ENodeType TListNodeTypeHandler::GetNodeType() const
{
    return ENodeType::List;
}

ICypressNodeProxyPtr TListNodeTypeHandler::DoGetProxy(
    TListNode* trunkNode,
    TTransaction* transaction)
{
    return New<TListNodeProxy>(
        Bootstrap_,
        &Metadata_,
        transaction,
        trunkNode);
}

void TListNodeTypeHandler::DoDestroy(TListNode* node)
{
    TBase::DoDestroy(node);

    // Drop references to the children.
    const auto& objectManager = Bootstrap_->GetObjectManager();
    for (auto* child : node->IndexToChild()) {
        objectManager->UnrefObject(child);
    }
}

void TListNodeTypeHandler::DoBranch(
    const TListNode* originatingNode,
    TListNode* branchedNode,
    const TLockRequest& lockRequest)
{
    TBase::DoBranch(originatingNode, branchedNode, lockRequest);

    branchedNode->IndexToChild() = originatingNode->IndexToChild();
    branchedNode->ChildToIndex() = originatingNode->ChildToIndex();

    // Reference all children.
    const auto& objectManager = Bootstrap_->GetObjectManager();
    for (auto* child : originatingNode->IndexToChild()) {
        objectManager->RefObject(child);
    }
}

void TListNodeTypeHandler::DoMerge(
    TListNode* originatingNode,
    TListNode* branchedNode)
{
    TBase::DoMerge(originatingNode, branchedNode);

    // Drop all references held by the originator.
    const auto& objectManager = Bootstrap_->GetObjectManager();
    for (auto* child : originatingNode->IndexToChild()) {
        objectManager->UnrefObject(child);
    }

    // Replace the child list with the branched copy.
    originatingNode->IndexToChild().swap(branchedNode->IndexToChild());
    originatingNode->ChildToIndex().swap(branchedNode->ChildToIndex());
}

void TListNodeTypeHandler::DoClone(
    TListNode* sourceNode,
    TListNode* clonedTrunkNode,
    ICypressNodeFactory* factory,
    ENodeCloneMode mode,
    TAccount* account)
{
    TBase::DoClone(sourceNode, clonedTrunkNode, factory, mode, account);

    const auto& objectManager = Bootstrap_->GetObjectManager();
    const auto& indexToChild = sourceNode->IndexToChild();

    for (int index = 0; index < static_cast<int>(indexToChild.size()); ++index) {
        auto* childNode = indexToChild[index];
        auto* clonedChildNode = factory->CloneNode(childNode, mode);
        auto* clonedChildTrunkNode = clonedChildNode->GetTrunkNode();

        clonedTrunkNode->IndexToChild().push_back(clonedChildTrunkNode);
        YT_VERIFY(clonedTrunkNode->ChildToIndex().insert(std::make_pair(clonedChildTrunkNode, index)).second);

        AttachChild(objectManager, clonedTrunkNode, clonedChildNode);
    }
}

bool TListNodeTypeHandler::HasBranchedChangesImpl(TListNode* originatingNode, TListNode* branchedNode)
{
    if (TBase::HasBranchedChangesImpl(originatingNode, branchedNode)) {
        return true;
    }

    return branchedNode->IndexToChild() != originatingNode->IndexToChild();
}

void TListNodeTypeHandler::DoBeginCopy(
    TListNode* node,
    TBeginCopyContext* context)
{
    TBase::DoBeginCopy(node, context);

    using NYT::Save;

    const auto& cypressManager = Bootstrap_->GetCypressManager();

    const auto& children = node->IndexToChild();
    TSizeSerializer::Save(*context, children.size());
    for (auto* child : children) {
        const auto& typeHandler = cypressManager->GetHandler(child);
        typeHandler->BeginCopy(child, context);
    }
}

void TListNodeTypeHandler::DoEndCopy(
    TListNode* trunkNode,
    TEndCopyContext* context,
    ICypressNodeFactory* factory)
{
    TBase::DoEndCopy(trunkNode, context, factory);

    using NYT::Load;

    const auto& objectManager = this->Bootstrap_->GetObjectManager();
    auto& indexToChild = trunkNode->IndexToChild();
    auto& childToIndex = trunkNode->ChildToIndex();

    size_t size = TSizeSerializer::Load(*context);
    for (size_t index = 0; index < size; ++index) {
        auto* childNode = factory->EndCopyNode(context);
        auto* trunkChildNode = childNode->GetTrunkNode();

        indexToChild.push_back(trunkChildNode);
        YT_VERIFY(childToIndex.insert(std::make_pair(trunkChildNode, index)).second);

        AttachChild(objectManager, trunkNode, childNode);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressServer

