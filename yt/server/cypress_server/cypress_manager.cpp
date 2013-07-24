#include "stdafx.h"
#include "cypress_manager.h"
#include "node_detail.h"
#include "node_proxy_detail.h"
#include "config.h"
#include "access_tracker.h"
#include "private.h"

#include <ytlib/misc/singleton.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>
#include <ytlib/cypress_client/cypress_ypath.pb.h>

#include <ytlib/object_client/object_service_proxy.h>

#include <ytlib/ytree/ephemeral_node_factory.h>
#include <ytlib/ytree/ypath_detail.h>

#include <server/cell_master/serialization_context.h>
#include <server/cell_master/bootstrap.h>
#include <server/cell_master/meta_state_facade.h>

#include <server/object_server/type_handler_detail.h>

#include <server/security_server/account.h>
#include <server/security_server/group.h>
#include <server/security_server/user.h>
#include <server/security_server/security_manager.h>

namespace NYT {
namespace NCypressServer {

using namespace NCellMaster;
using namespace NBus;
using namespace NRpc;
using namespace NYTree;
using namespace NTransactionServer;
using namespace NMetaState;
using namespace NObjectClient;
using namespace NObjectServer;
using namespace NSecurityClient;
using namespace NSecurityServer;
using namespace NCypressClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = CypressServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TCypressManager::TNodeTypeHandler
    : public TObjectTypeHandlerBase<TCypressNodeBase>
{
public:
    TNodeTypeHandler(TBootstrap* bootstrap, EObjectType type)
        : TObjectTypeHandlerBase(bootstrap)
        , Type(type)
    { }

    virtual EObjectType GetType() const override
    {
        return Type;
    }

    virtual TObjectBase* FindObject(const TObjectId& id) override
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        return cypressManager->FindNode(TVersionedNodeId(id));
    }

    virtual void Destroy(TObjectBase* object) override
    {
        DoDestroy(static_cast<TCypressNodeBase*>(object));
    }

    virtual TNullable<TTypeCreationOptions> GetCreationOptions() const override
    {
        return TTypeCreationOptions(
            EObjectTransactionMode::Optional,
            EObjectAccountMode::Forbidden,
            false);
    }

    virtual EPermissionSet GetSupportedPermissions() const override
    {
        return EPermissionSet(
            EPermission::Read |
            EPermission::Write |
            EPermission::Administer);
    }

private:
    EObjectType Type;

    void DoDestroy(TCypressNodeBase* node)
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        cypressManager->DestroyNode(node);
    }

    virtual Stroka DoGetName(TCypressNodeBase* node) override
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        auto path = cypressManager->GetNodePath(node->GetTrunkNode(), node->GetTransaction());
        return Sprintf("node %s", ~path);
    }

    virtual IObjectProxyPtr DoGetProxy(
        TCypressNodeBase* node,
        TTransaction* transaction) override
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        return cypressManager->GetVersionedNodeProxy(node, transaction);
    }

    virtual TAccessControlDescriptor* DoFindAcd(TCypressNodeBase* node) override
    {
        return &node->GetTrunkNode()->Acd();
    }

    virtual TObjectBase* DoGetParent(TCypressNodeBase* node) override
    {
        return node->GetParent();
    }

};

////////////////////////////////////////////////////////////////////////////////

class TCypressManager::TYPathResolver
    : public INodeResolver
{
public:
    TYPathResolver(
        TBootstrap* bootstrap,
        TTransaction* transaction)
        : Bootstrap(bootstrap)
        , Transaction(transaction)
    { }

    virtual INodePtr ResolvePath(const TYPath& path) override
    {
        auto objectManager = Bootstrap->GetObjectManager();
        auto* resolver = objectManager->GetObjectResolver();
        auto objectProxy = resolver->ResolvePath(path, Transaction);
        auto* nodeProxy = dynamic_cast<ICypressNodeProxy*>(~objectProxy);
        if (!nodeProxy) {
            THROW_ERROR_EXCEPTION("Path % points to a nonversioned %s object instead of a node",
                ~FormatEnum(TypeFromId(objectProxy->GetId())).Quote());
        }
        return nodeProxy;
    }

    virtual TYPath GetPath(INodePtr node) override
    {
        INodePtr root;
        auto path = GetNodeYPath(node, &root);

        auto* rootProxy = dynamic_cast<ICypressNodeProxy*>(~root);
        YCHECK(rootProxy);

        auto cypressManager = Bootstrap->GetCypressManager();
        auto rootId = cypressManager->GetRootNode()->GetId();
        return rootProxy->GetId() == rootId
            ? "/" + path
            : "?" + path;
    }

private:
    TBootstrap* Bootstrap;
    TTransaction* Transaction;

};

////////////////////////////////////////////////////////////////////////////////

TCypressManager::TNodeMapTraits::TNodeMapTraits(TCypressManager* cypressManager)
    : CypressManager(cypressManager)
{ }

std::unique_ptr<TCypressNodeBase> TCypressManager::TNodeMapTraits::Create(const TVersionedNodeId& id) const
{
    auto type = TypeFromId(id.ObjectId);
    auto handler = CypressManager->GetHandler(type);
    return handler->Instantiate(id);
}

////////////////////////////////////////////////////////////////////////////////

TCypressManager::TCypressManager(
    TCypressManagerConfigPtr config,
    TBootstrap* bootstrap)
    : TMetaStatePart(
        bootstrap->GetMetaStateFacade()->GetManager(),
        bootstrap->GetMetaStateFacade()->GetState())
    , Config(config)
    , Bootstrap(bootstrap)
    , NodeMap(TNodeMapTraits(this))
    , TypeToHandler(MaxObjectType + 1)
    , RootNode(nullptr)
    , AccessTracker(New<TAccessTracker>(config, bootstrap))
{
    YCHECK(config);
    YCHECK(bootstrap);
    VERIFY_INVOKER_AFFINITY(bootstrap->GetMetaStateFacade()->GetInvoker(), StateThread);

    {
        auto cellId = Bootstrap->GetObjectManager()->GetCellId();
        RootNodeId = MakeWellKnownId(EObjectType::MapNode, cellId);
    }

    RegisterHandler(New<TStringNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TIntegerNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TDoubleNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TMapNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TListNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TLinkNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TDocumentNodeTypeHandler>(Bootstrap));

    {
        NCellMaster::TLoadContext context;
        context.SetBootstrap(Bootstrap);

        RegisterLoader(
            "Cypress.Keys",
            SnapshotVersionValidator(),
            BIND(&TCypressManager::LoadKeys, MakeStrong(this)),
            context);
        RegisterLoader(
            "Cypress.Values",
            SnapshotVersionValidator(),
            BIND(&TCypressManager::LoadValues, MakeStrong(this)),
            context);
    }

    {
        NCellMaster::TSaveContext context;

        RegisterSaver(
            ESerializationPriority::Keys,
            "Cypress.Keys",
            GetCurrentSnapshotVersion(),
            BIND(&TCypressManager::SaveKeys, MakeStrong(this)),
            context);
        RegisterSaver(
            ESerializationPriority::Values,
            "Cypress.Values",
            GetCurrentSnapshotVersion(),
            BIND(&TCypressManager::SaveValues, MakeStrong(this)),
            context);
    }

    RegisterMethod(BIND(&TThis::UpdateAccessStatistics, Unretained(this)));
}

void TCypressManager::Initialize()
{
    auto transactionManager = Bootstrap->GetTransactionManager();
    transactionManager->SubscribeTransactionCommitted(BIND(
        &TThis::OnTransactionCommitted,
        MakeStrong(this)));
    transactionManager->SubscribeTransactionAborted(BIND(
        &TThis::OnTransactionAborted,
        MakeStrong(this)));
}

void TCypressManager::RegisterHandler(INodeTypeHandlerPtr handler)
{
    // No thread affinity is given here.
    // This will be called during init-time only.
    YCHECK(handler);

    auto type = handler->GetObjectType();
    int typeValue = static_cast<int>(type);
    YCHECK(typeValue >= 0 && typeValue <= MaxObjectType);
    YCHECK(!TypeToHandler[typeValue]);
    TypeToHandler[typeValue] = handler;

    auto objectManager = Bootstrap->GetObjectManager();
    objectManager->RegisterHandler(New<TNodeTypeHandler>(Bootstrap, type));
}

INodeTypeHandlerPtr TCypressManager::FindHandler(EObjectType type)
{
    VERIFY_THREAD_AFFINITY_ANY();

    int typeValue = static_cast<int>(type);
    if (typeValue < 0 || typeValue > MaxObjectType) {
        return nullptr;
    }

    return TypeToHandler[typeValue];
}

INodeTypeHandlerPtr TCypressManager::GetHandler(EObjectType type)
{
    VERIFY_THREAD_AFFINITY_ANY();

    auto handler = FindHandler(type);
    YCHECK(handler);
    return handler;
}

INodeTypeHandlerPtr TCypressManager::GetHandler(const TCypressNodeBase* node)
{
    return GetHandler(node->GetType());
}

NMetaState::TMutationPtr TCypressManager::CreateUpdateAccessStatisticsMutation(
    const NProto::TMetaReqUpdateAccessStatistics& request)
{
   return Bootstrap
        ->GetMetaStateFacade()
        ->CreateMutation(this, request, &TThis::UpdateAccessStatistics);
}

TCypressNodeBase* TCypressManager::CreateNode(
    INodeTypeHandlerPtr handler,
    ICypressNodeFactoryPtr factory,
    TReqCreate* request,
    TRspCreate* response)
{
    YCHECK(handler);
    YCHECK(factory);

    auto* transaction = factory->GetTransaction();
    auto node = handler->Create(transaction, request, response);
    auto node_ = ~node;

    RegisterNode(std::move(node));

    // Set account.
    auto securityManager = Bootstrap->GetSecurityManager();
    auto* account = factory->GetAccount();
    securityManager->SetAccount(node_, account);

    // Set owner.
    auto* user = securityManager->GetAuthenticatedUser();
    auto* acd = securityManager->GetAcd(node_);
    acd->SetOwner(user);

    if (response) {
        ToProto(response->mutable_node_id(), node_->GetId());
    }

    return node_;
}

TCypressNodeBase* TCypressManager::CloneNode(
    TCypressNodeBase* sourceNode,
    ICypressNodeFactoryPtr factory)
{
    YCHECK(sourceNode);
    YCHECK(factory);

    auto handler = GetHandler(sourceNode);
    auto clonedNode = handler->Clone(sourceNode, factory);

    // Make a rawptr copy and transfer the ownership.
    auto clonedNode_ = ~clonedNode;
    RegisterNode(std::move(clonedNode));

    // Set account.
    auto securityManager = Bootstrap->GetSecurityManager();
    auto* account = factory->GetAccount();
    securityManager->SetAccount(clonedNode_, account);

    // Set owner.
    auto* user = securityManager->GetAuthenticatedUser();
    auto* acd = securityManager->GetAcd(clonedNode_);
    acd->SetOwner(user);

    return clonedNode_;
}

TCypressNodeBase* TCypressManager::GetRootNode() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return RootNode;
}

INodeResolverPtr TCypressManager::CreateResolver(TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    return New<TYPathResolver>(Bootstrap, transaction);
}

TCypressNodeBase* TCypressManager::FindNode(
    TCypressNodeBase* trunkNode,
    NTransactionServer::TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());

    // Fast path -- no transaction.
    if (!transaction) {
        return trunkNode;
    }

    TVersionedNodeId versionedId(trunkNode->GetId(), GetObjectId(transaction));
    return FindNode(versionedId);
}

TCypressNodeBase* TCypressManager::GetVersionedNode(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());

    auto* currentTransaction = transaction;
    while (true) {
        auto* currentNode = FindNode(trunkNode, currentTransaction);
        if (currentNode) {
            return currentNode;
        }
        currentTransaction = currentTransaction->GetParent();
    }
}

ICypressNodeProxyPtr TCypressManager::GetVersionedNodeProxy(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());

    auto handler = GetHandler(trunkNode);
    return handler->GetProxy(trunkNode, transaction);
}

void TCypressManager::ValidateLock(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request,
    bool* isMandatory)
{
    YCHECK(trunkNode->IsTrunk());

    // Snapshot locks can only be taken inside a transaction.
    if (request.Mode == ELockMode::Snapshot && !transaction) {
        THROW_ERROR_EXCEPTION("Cannot take %s lock outside of a transaction",
            ~FormatEnum(request.Mode).Quote());
    }

    // Examine existing locks.
    // A quick check: same transaction, same or weaker lock mode (beware of Snapshot!).
    {
        auto it = trunkNode->Locks().find(transaction);
        if (it != trunkNode->Locks().end()) {
            const auto& existingLock = it->second;
            if (IsRedundantLock(existingLock, request)) {
                *isMandatory = false;
                return;
            }
            if (existingLock.Mode == ELockMode::Snapshot) {
                THROW_ERROR_EXCEPTION(
                    NCypressClient::EErrorCode::SameTransactionLockConflict,
                    "Cannot take %s lock for node %s since %s lock is already taken by the same transaction",
                    ~FormatEnum(request.Mode).Quote(),
                    ~GetNodePath(trunkNode, transaction),
                    ~FormatEnum(existingLock.Mode).Quote());
            }
        }
    }

    FOREACH (const auto& pair, trunkNode->Locks()) {
        auto* existingTransaction = pair.first;
        const auto& existingLock = pair.second;

        // Ignore other Snapshot locks.
        if (existingLock.Mode == ELockMode::Snapshot) {
            continue;
        }

        // When a Snapshot is requested no descendant transaction (including |transaction| itself)
        // may hold a lock other than Snapshot.
        if (request.Mode == ELockMode::Snapshot &&
            IsParentTransaction(existingTransaction, transaction))
        {
            THROW_ERROR_EXCEPTION(
                NCypressClient::EErrorCode::DescendantTransactionLockConflict,
                "Cannot take %s lock for node %s since %s lock is taken by descendant transaction %s",
                ~FormatEnum(request.Mode).Quote(),
                ~GetNodePath(trunkNode, transaction),
                ~FormatEnum(existingLock.Mode).Quote(),
                ~ToString(existingTransaction->GetId()));
        }

        if (!transaction || IsConcurrentTransaction(transaction, existingTransaction)) {
            // For Exclusive locks we check locks held by concurrent transactions.
            if ((request.Mode == ELockMode::Exclusive && existingLock.Mode != ELockMode::Snapshot) ||
                (existingLock.Mode == ELockMode::Exclusive && request.Mode != ELockMode::Snapshot))
            {
                THROW_ERROR_EXCEPTION(
                    NCypressClient::EErrorCode::ConcurrentTransactionLockConflict,
                    "Cannot take %s lock for node %s since %s lock is taken by concurrent transaction %s",
                    ~FormatEnum(request.Mode).Quote(),
                    ~GetNodePath(trunkNode, transaction),
                    ~FormatEnum(existingLock.Mode).Quote(),
                    ~ToString(existingTransaction->GetId()));
            }

            // For Shared locks we check child and attribute keys.
            if (request.Mode == ELockMode::Shared && existingLock.Mode == ELockMode::Shared) {
                if (request.ChildKey &&
                    existingLock.ChildKeys.find(request.ChildKey.Get()) != existingLock.ChildKeys.end())
                {
                    THROW_ERROR_EXCEPTION(
                        NCypressClient::EErrorCode::ConcurrentTransactionLockConflict,
                        "Cannot take %s lock for child %s of node %s since %s lock is taken by concurrent transaction %s",
                        ~FormatEnum(request.Mode).Quote(),
                        ~request.ChildKey.Get().Quote(),
                        ~GetNodePath(trunkNode, transaction),
                        ~FormatEnum(existingLock.Mode).Quote(),
                        ~ToString(existingTransaction->GetId()));
                }
                if (request.AttributeKey &&
                    existingLock.AttributeKeys.find(request.AttributeKey.Get()) != existingLock.AttributeKeys.end())
                {
                    THROW_ERROR_EXCEPTION(
                        NCypressClient::EErrorCode::ConcurrentTransactionLockConflict,
                        "Cannot take %s lock for attribute %s of node %s since %s lock is taken by concurrent transaction %s",
                        ~FormatEnum(request.Mode).Quote(),
                        ~request.AttributeKey.Get().Quote(),
                        ~GetNodePath(trunkNode, transaction),
                        ~FormatEnum(existingLock.Mode).Quote(),
                        ~ToString(existingTransaction->GetId()));
                }
            }
        }
    }

    // If we're outside of a transaction then the lock is not needed.
    *isMandatory = (transaction != nullptr);
}

void TCypressManager::ValidateLock(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request)
{
    bool dummy;
    ValidateLock(trunkNode, transaction, request, &dummy);
}

bool TCypressManager::IsRedundantLock(
    const TLock& existingLock,
    const TLockRequest& request)
{
    if (existingLock.Mode > request.Mode && request.Mode != ELockMode::Snapshot) {
        return true;
    }

    if (existingLock.Mode == request.Mode) {
        if (request.Mode == ELockMode::Shared) {
            if (request.ChildKey &&
                existingLock.ChildKeys.find(request.ChildKey.Get()) == existingLock.ChildKeys.end())
            {
                return false;
            }
            if (request.AttributeKey &&
                existingLock.AttributeKeys.find(request.AttributeKey.Get()) == existingLock.AttributeKeys.end())
            {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool TCypressManager::IsParentTransaction(TTransaction* transaction, TTransaction* parent)
{
    auto currentTransaction = transaction;
    while (currentTransaction) {
        if (currentTransaction == parent) {
            return true;
        }
        currentTransaction = currentTransaction->GetParent();
    }
    return false;
}

bool TCypressManager::IsConcurrentTransaction(TTransaction* transaction1, TTransaction* transaction2)
{
    return
        !IsParentTransaction(transaction1, transaction2) &&
        !IsParentTransaction(transaction2, transaction1);
}

TCypressNodeBase* TCypressManager::AcquireLock(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request)
{
    YCHECK(trunkNode->IsTrunk());
    YCHECK(transaction);

    DoAcquireLock(trunkNode, transaction, request);

    // Upgrade locks held by parent transactions, if needed.
    if (request.Mode != ELockMode::Snapshot) {
        auto* currentTransaction = transaction->GetParent();
        while (currentTransaction) {
            DoAcquireLock(trunkNode, currentTransaction, request);
            currentTransaction = currentTransaction->GetParent();
        }
    }

    // Branch node, if needed.
    auto* branchedNode = FindNode(trunkNode, transaction);
    if (branchedNode) {
        if (branchedNode->GetLockMode() < request.Mode) {
            branchedNode->SetLockMode(request.Mode);
        }
        return branchedNode;
    }

    TCypressNodeBase* originatingNode;
    std::vector<TTransaction*> intermediateTransactions;
    // Walk up to the root, find originatingNode, construct the list of
    // intermediate transactions.
    auto* currentTransaction = transaction;
    while (true) {
        originatingNode = FindNode(trunkNode, currentTransaction);
        if (originatingNode) {
            break;
        }
        if (!currentTransaction) {
            break;
        }
        intermediateTransactions.push_back(currentTransaction);
        currentTransaction = currentTransaction->GetParent();
    }

    YCHECK(originatingNode);
    YCHECK(!intermediateTransactions.empty());

    if (request.Mode == ELockMode::Snapshot) {
        // Branch at requested transaction only.
        return BranchNode(originatingNode, transaction, request.Mode);
    } else {
        // Branch at all intermediate transactions.
        std::reverse(intermediateTransactions.begin(), intermediateTransactions.end());
        auto* currentNode = originatingNode;
        FOREACH (auto* transactionToBranch, intermediateTransactions) {
            currentNode = BranchNode(currentNode, transactionToBranch, request.Mode);
        }
        return currentNode;
    }
}

TLock* TCypressManager::DoAcquireLock(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request)
{
    YCHECK(trunkNode->IsTrunk());

    TVersionedNodeId versionedId(trunkNode->GetId(), transaction->GetId());
    TLock* lock;
    auto it = trunkNode->Locks().find(transaction);
    if (it == trunkNode->Locks().end()) {
        lock = &trunkNode->Locks()[transaction];
        lock->Mode = request.Mode;
        transaction->LockedNodes().push_back(trunkNode);

        LOG_DEBUG_UNLESS(IsRecovery(), "Node locked (NodeId: %s, Mode: %s)",
            ~ToString(versionedId),
            ~request.Mode.ToString());
    } else {
        lock = &it->second;
        if (lock->Mode < request.Mode) {
            lock->Mode = request.Mode;

            LOG_DEBUG_UNLESS(IsRecovery(), "Node lock upgraded (NodeId: %s, Mode: %s)",
                ~ToString(versionedId),
                ~lock->Mode.ToString());
        }
    }

    if (request.ChildKey &&
        lock->ChildKeys.find(request.ChildKey.Get()) == lock->ChildKeys.end())
    {
        YCHECK(lock->ChildKeys.insert(request.ChildKey.Get()).second);
        LOG_DEBUG_UNLESS(IsRecovery(), "Node child locked (NodeId: %s, Key: %s)",
            ~ToString(versionedId),
            ~request.ChildKey.Get());
    }

    if (request.AttributeKey &&
        lock->AttributeKeys.find(request.AttributeKey.Get()) == lock->AttributeKeys.end())
    {
        YCHECK(lock->AttributeKeys.insert(request.AttributeKey.Get()).second);
        LOG_DEBUG_UNLESS(IsRecovery(), "Node attribute locked (NodeId: %s, Key: %s)",
            ~ToString(versionedId),
            ~request.AttributeKey.Get());
    }

    return lock;
}

void TCypressManager::ReleaseLock(TCypressNodeBase* trunkNode, TTransaction* transaction)
{
    YCHECK(trunkNode->IsTrunk());

    YCHECK(trunkNode->Locks().erase(transaction) == 1);

    TVersionedNodeId versionedId(trunkNode->GetId(), transaction->GetId());
    LOG_DEBUG_UNLESS(IsRecovery(), "Node unlocked (NodeId: %s)",
        ~ToString(versionedId));
}

TCypressNodeBase* TCypressManager::LockVersionedNode(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    const TLockRequest& request,
    bool recursive)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());
    YCHECK(request.Mode != ELockMode::None);

    TSubtreeNodes nodesToLock;
    if (recursive) {
        YCHECK(!request.ChildKey);
        YCHECK(!request.AttributeKey);
        ListSubtreeNodes(trunkNode, transaction, true, &nodesToLock);
    } else {
        nodesToLock.push_back(trunkNode);
    }

    // Validate all potentials lock to see if we need to take at least one of them.
    // This throws an exception in case the validation fails.
    bool isMandatory = false;
    FOREACH (auto* child, nodesToLock) {
        bool isChildMandatory;
        ValidateLock(child->GetTrunkNode(), transaction, request, &isChildMandatory);
        isMandatory |= isChildMandatory;
    }

    if (!isMandatory) {
        return GetVersionedNode(trunkNode, transaction);
    }

    if (!transaction) {
        THROW_ERROR_EXCEPTION("The requested operation requires %s lock but no current transaction is given",
            ~FormatEnum(request.Mode).Quote());
    }

    TCypressNodeBase* lockedNode = nullptr;
    FOREACH (auto* child, nodesToLock) {
        auto* lockedChild = AcquireLock(child->GetTrunkNode(), transaction, request);
        if (child == trunkNode) {
            lockedNode = lockedChild;
        }
    }

    YCHECK(lockedNode);
    return lockedNode;
}

void TCypressManager::SetModified(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    AccessTracker->SetModified(trunkNode, transaction);
}

void TCypressManager::SetAccessed(TCypressNodeBase* trunkNode)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    if (IsLeader()) {
        AccessTracker->SetAccessed(trunkNode);
    }
}

TCypressManager::TSubtreeNodes TCypressManager::ListSubtreeNodes(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    bool includeRoot)
{
    TSubtreeNodes result;
    ListSubtreeNodes(trunkNode, transaction, includeRoot, &result);
    return result;
}

bool TCypressManager::IsOrphaned(TCypressNodeBase* trunkNode)
{
    if (!IsObjectAlive(trunkNode)) {
        return true;
    }

    YCHECK(trunkNode->IsTrunk());

    while (true) {
        if (trunkNode == RootNode) {
            return false;
        }
        if (!trunkNode) {
            return true;
        }
        trunkNode = trunkNode->GetParent();
    }
}

TCypressNodeBase* TCypressManager::BranchNode(
    TCypressNodeBase* originatingNode,
    TTransaction* transaction,
    ELockMode mode)
{
    YCHECK(originatingNode);
    YCHECK(transaction);
    VERIFY_THREAD_AFFINITY(StateThread);

    auto objectManager = Bootstrap->GetObjectManager();
    auto securityManager = Bootstrap->GetSecurityManager();

    const auto& id = originatingNode->GetId();

    // Create a branched node and initialize its state.
    auto handler = GetHandler(originatingNode);
    auto branchedNode = handler->Branch(originatingNode, transaction, mode);
    YCHECK(branchedNode->GetLockMode() == mode);
    auto* branchedNode_ = branchedNode.release();

    TVersionedNodeId versionedId(id, transaction->GetId());
    NodeMap.Insert(versionedId, branchedNode_);

    // Register the branched node with the transaction.
    transaction->BranchedNodes().push_back(branchedNode_);

    // The branched node holds an implicit reference to its originator.
    objectManager->RefObject(originatingNode->GetTrunkNode());

    // Update resource usage.
    auto* account = originatingNode->GetAccount();
    securityManager->SetAccount(branchedNode_, account);

    LOG_DEBUG_UNLESS(IsRecovery(), "Node branched (NodeId: %s, TransactionId: %s, Mode: %s)",
        ~ToString(id),
        ~ToString(transaction->GetId()),
        ~mode.ToString());

    return branchedNode_;
}

void TCypressManager::SaveKeys(NCellMaster::TSaveContext& context) const
{
    NodeMap.SaveKeys(context);
}

void TCypressManager::SaveValues(NCellMaster::TSaveContext& context) const
{
    NodeMap.SaveValues(context);
}

void TCypressManager::OnBeforeLoaded()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    DoClear();
}

void TCypressManager::LoadKeys(NCellMaster::TLoadContext& context)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.LoadKeys(context);
}

void TCypressManager::LoadValues(NCellMaster::TLoadContext& context)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.LoadValues(context);
}

void TCypressManager::OnAfterLoaded()
{
    // Reconstruct immediate ancestor sets.
    FOREACH (const auto& pair, NodeMap) {
        auto* node = pair.second;
        auto* parent = node->GetParent();
        if (parent) {
            YCHECK(parent->ImmediateDescendants().insert(node).second);
        }
    }

    InitBuiltin();
}

void TCypressManager::InitBuiltin()
{
    RootNode = FindNode(TVersionedNodeId(RootNodeId));
    if (!RootNode) {
        // Create the root.
        auto securityManager = Bootstrap->GetSecurityManager();
        RootNode = new TMapNode(TVersionedNodeId(RootNodeId));
        RootNode->SetTrunkNode(RootNode);
        RootNode->SetAccount(securityManager->GetSysAccount());
        RootNode->Acd().SetInherit(false);
        RootNode->Acd().AddEntry(TAccessControlEntry(
            ESecurityAction::Allow,
            securityManager->GetEveryoneGroup(),
            EPermission::Read));
        RootNode->Acd().SetOwner(securityManager->GetRootUser());

        NodeMap.Insert(TVersionedNodeId(RootNodeId), RootNode);
        YCHECK(RootNode->RefObject() == 1);
    }
}

void TCypressManager::DoClear()
{
    NodeMap.Clear();
}

void TCypressManager::Clear()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    DoClear();
    InitBuiltin();
}

void TCypressManager::OnRecoveryComplete()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    FOREACH (const auto& pair, NodeMap) {
        auto* node = pair.second;
        node->ResetObjectLocks();
    }
}

void TCypressManager::RegisterNode(std::unique_ptr<TCypressNodeBase> node)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(node->IsTrunk());

    const auto& nodeId = node->GetId();

    auto objectManager = Bootstrap->GetObjectManager();

    auto* mutationContext = Bootstrap
        ->GetMetaStateFacade()
        ->GetManager()
        ->GetMutationContext();

    node->SetCreationTime(mutationContext->GetTimestamp());
    node->SetModificationTime(mutationContext->GetTimestamp());
    node->SetRevision(mutationContext->GetVersion().ToRevision());

    NodeMap.Insert(TVersionedNodeId(nodeId), node.release());

    LOG_DEBUG_UNLESS(IsRecovery(), "Node registered (NodeId: %s, Type: %s)",
        ~ToString(nodeId),
        ~TypeFromId(nodeId).ToString());
}

void TCypressManager::DestroyNode(TCypressNodeBase* trunkNode)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(trunkNode->IsTrunk());

    auto nodeHolder = NodeMap.Release(trunkNode->GetVersionedId());

    auto handler = GetHandler(trunkNode);
    handler->Destroy(trunkNode);
}

void TCypressManager::OnTransactionCommitted(TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    ReleaseLocks(transaction);
    MergeNodes(transaction);
}

void TCypressManager::OnTransactionAborted(TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    ReleaseLocks(transaction);
    RemoveBranchedNodes(transaction);
}

void TCypressManager::ReleaseLocks(TTransaction* transaction)
{
    FOREACH (auto* trunkNode, transaction->LockedNodes()) {
        ReleaseLock(trunkNode, transaction);
    }
    transaction->LockedNodes().clear();
}

void TCypressManager::ListSubtreeNodes(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction,
    bool includeRoot,
    TSubtreeNodes* subtreeNodes)
{
    YCHECK(trunkNode->IsTrunk());

    auto transactionManager = Bootstrap->GetTransactionManager();

    if (includeRoot) {
        subtreeNodes->push_back(trunkNode);
    }

    switch (trunkNode->GetType()) {
        case EObjectType::MapNode: {
            auto transactions = transactionManager->GetTransactionPath(transaction);
            std::reverse(transactions.begin(), transactions.end());

            yhash_map<Stroka, TCypressNodeBase*> children;
            FOREACH (auto* currentTransaction, transactions) {
                TVersionedObjectId versionedId(trunkNode->GetId(), GetObjectId(currentTransaction));
                const auto* node = FindNode(versionedId);
                if (node) {
                    const auto* mapNode = static_cast<const TMapNode*>(node);
                    FOREACH (const auto& pair, mapNode->KeyToChild()) {
                        if (pair.second) {
                            children[pair.first] = pair.second;
                        } else {
                            // NB: erase may fail.
                            children.erase(pair.first);
                        }
                    }
                }
            }

            FOREACH (const auto& pair, children) {
                ListSubtreeNodes(pair.second, transaction, true, subtreeNodes);
            }

            break;
        }

        case EObjectType::ListNode: {
            auto* node = GetVersionedNode(trunkNode, transaction);
            auto* listRoot = static_cast<TListNode*>(node);
            FOREACH (auto* trunkChild, listRoot->IndexToChild()) {
                ListSubtreeNodes(trunkChild, transaction, true, subtreeNodes);
            }
            break;
        }

        default:
            break;
    }
}

void TCypressManager::MergeNode(
    TTransaction* transaction,
    TCypressNodeBase* branchedNode)
{
    auto objectManager = Bootstrap->GetObjectManager();
    auto securityManager = Bootstrap->GetSecurityManager();

    auto handler = GetHandler(branchedNode);

    auto* trunkNode = branchedNode->GetTrunkNode();
    auto branchedId = branchedNode->GetVersionedId();
    auto* parentTransaction = transaction->GetParent();
    auto originatingId = TVersionedNodeId(branchedId.ObjectId, GetObjectId(parentTransaction));

    if (branchedNode->GetLockMode() != ELockMode::Snapshot) {
        auto* originatingNode = NodeMap.Get(originatingId);

        // Merge changes back.
        handler->Merge(originatingNode, branchedNode);

        // The root needs a special handling.
        // When Cypress gets cleared, the root is created and is assigned zero creation time.
        // (We don't have any mutation context at hand to provide a synchronized timestamp.)
        // Later on, Cypress is initialized and filled with nodes.
        // At this point we set the root's creation time.
        if (trunkNode == RootNode && !parentTransaction) {
            originatingNode->SetCreationTime(originatingNode->GetModificationTime());
        }

        // Update resource usage.
        securityManager->UpdateAccountNodeUsage(originatingNode);

        LOG_DEBUG_UNLESS(IsRecovery(), "Node merged (NodeId: %s)", ~ToString(branchedId));
    } else {
        // Destroy the branched copy.
        handler->Destroy(branchedNode);

        LOG_DEBUG_UNLESS(IsRecovery(), "Node snapshot destroyed (NodeId: %s)", ~ToString(branchedId));
    }

    // Drop the implicit reference to the originator.
    objectManager->UnrefObject(trunkNode);

    // Remove the branched copy.
    NodeMap.Remove(branchedId);

    LOG_DEBUG_UNLESS(IsRecovery(), "Branched node removed (NodeId: %s)", ~ToString(branchedId));
}

void TCypressManager::MergeNodes(TTransaction* transaction)
{
    FOREACH (auto* node, transaction->BranchedNodes()) {
        MergeNode(transaction, node);
    }
    transaction->BranchedNodes().clear();
}

void TCypressManager::RemoveBranchedNode(TCypressNodeBase* branchedNode)
{
    auto objectManager = Bootstrap->GetObjectManager();

    auto handler = GetHandler(branchedNode);

    auto* trunkNode = branchedNode->GetTrunkNode();
    auto branchedNodeId = branchedNode->GetVersionedId();

    // Drop the implicit reference to the originator.
    objectManager->UnrefObject(trunkNode);

    // Remove the node.
    handler->Destroy(branchedNode);
    NodeMap.Remove(branchedNodeId);

    LOG_DEBUG_UNLESS(IsRecovery(), "Branched node removed (NodeId: %s)", ~ToString(branchedNodeId));
}

void TCypressManager::RemoveBranchedNodes(TTransaction* transaction)
{
    FOREACH (auto* branchedNode, transaction->BranchedNodes()) {
        RemoveBranchedNode(branchedNode);
    }
    transaction->BranchedNodes().clear();
}

TYPath TCypressManager::GetNodePath(
    TCypressNodeBase* trunkNode,
    TTransaction* transaction)
{
    YCHECK(trunkNode->IsTrunk());

    auto proxy = GetVersionedNodeProxy(trunkNode, transaction);
    return proxy->GetResolver()->GetPath(proxy);
}

void TCypressManager::OnActiveQuorumEstablished()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    AccessTracker->StartFlush();
}

void TCypressManager::OnStopLeading()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    AccessTracker->StopFlush();
}

void TCypressManager::UpdateAccessStatistics(const NProto::TMetaReqUpdateAccessStatistics& request)
{
    FOREACH (const auto& update, request.updates()) {
        auto nodeId = FromProto<TNodeId>(update.node_id());
        auto* node = FindNode(TVersionedNodeId(nodeId));
        if (node) {
            // Update access time.
            auto accessTime = TInstant(update.access_time());
            if (accessTime > node->GetAccessTime()) {
                node->SetAccessTime(accessTime);
            }

            // Update access counter.
            i64 accessCounter = node->GetAccessCounter() + update.access_counter_delta();
            node->SetAccessCounter(accessCounter);
        }
    }
}

DEFINE_METAMAP_ACCESSORS(TCypressManager, Node, TCypressNodeBase, TVersionedNodeId, NodeMap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypressServer
} // namespace NYT
