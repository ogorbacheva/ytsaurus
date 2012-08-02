#include "stdafx.h"
#include "cypress_manager.h"
#include "node_detail.h"
#include "node_proxy_detail.h"

#include <ytlib/misc/singleton.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>
#include <ytlib/cypress_client/cypress_ypath.pb.h>

#include <ytlib/cell_master/load_context.h>
#include <ytlib/cell_master/bootstrap.h>
#include <ytlib/cell_master/meta_state_facade.h>

#include <ytlib/ytree/ephemeral.h>
#include <ytlib/ytree/ypath_detail.h>

#include <ytlib/object_server/type_handler_detail.h>
#include <ytlib/object_server/object_service_proxy.h>

namespace NYT {
namespace NCypressServer {

using namespace NCellMaster;
using namespace NBus;
using namespace NRpc;
using namespace NYTree;
using namespace NTransactionServer;
using namespace NMetaState;
using namespace NCypressClient::NProto;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger Logger("Cypress");

////////////////////////////////////////////////////////////////////////////////

class TCypressManager::TNodeTypeHandler
    : public IObjectTypeHandler
{
public:
    TNodeTypeHandler(
        TCypressManager* cypressManager,
        EObjectType type)
        : CypressManager(cypressManager)
        , Type(type)
    { }

    virtual EObjectType GetType() override
    {
        return Type;
    }

    virtual bool Exists(const TObjectId& id) override
    {
        return CypressManager->FindNode(id) != NULL;
    }

    virtual i32 RefObject(const TObjectId& id) override
    {
        return CypressManager->RefNode(id);
    }

    virtual i32 UnrefObject(const TObjectId& id) override
    {
        return CypressManager->UnrefNode(id);
    }

    virtual i32 GetObjectRefCounter(const TObjectId& id) override
    {
        return CypressManager->GetNodeRefCounter(id);
    }

    virtual void Destroy(const TObjectId& id) override
    {
        CypressManager->DestroyNode(id);
    }

    virtual IObjectProxyPtr GetProxy(
        const TObjectId& id,
        TTransaction* transaction) override
    {
        return CypressManager->GetVersionedNodeProxy(id, transaction);
    }

    virtual TObjectId Create(
        TTransaction* transaction,
        TReqCreateObject* request,
        TRspCreateObject* response) override
    {
        UNUSED(transaction);
        UNUSED(request);
        UNUSED(response);

        ythrow yexception() << Sprintf("Cannot create an instance of %s outside Cypress",
            ~FormatEnum(GetType()));
    }

    virtual bool IsTransactionRequired() const override
    {
        return false;
    }

private:
    TCypressManager* CypressManager;
    EObjectType Type;

};

////////////////////////////////////////////////////////////////////////////////

class TCypressManager::TYPathResolver
    : public IYPathResolver
{
public:
    explicit TYPathResolver(
        TBootstrap* bootstrap,
        TTransaction* transaction)
        : Bootstrap(bootstrap)
        , Transaction(transaction)
    { }

    virtual INodePtr ResolvePath(const TYPath& path) override
    {
        if (path.empty()) {
            ythrow yexception() << "YPath cannot be empty";
        }

        TTokenizer tokenizer(path);
        tokenizer.ParseNext();

        if (tokenizer.GetCurrentType() != RootToken) {
            ythrow yexception() << "YPath must start with \"/\"";
        }

        auto cypressManager = Bootstrap->GetCypressManager();
        auto root = cypressManager->FindVersionedNodeProxy(
            cypressManager->GetRootNodeId(),
            Transaction);

        return GetNodeByYPath(root, TYPath(tokenizer.GetCurrentSuffix()));
    }

    virtual TYPath GetPath(INodePtr node) override
    {
        auto path = GetNodeYPath(node);
        return Stroka(TokenTypeToChar(RootToken)) + path;
    }

private:
    TBootstrap* Bootstrap;
    TTransaction* Transaction;

};

////////////////////////////////////////////////////////////////////////////////

TCypressManager::TNodeMapTraits::TNodeMapTraits(TCypressManager* cypressManager)
    : CypressManager(cypressManager)
{ }

TAutoPtr<ICypressNode> TCypressManager::TNodeMapTraits::Create(const TVersionedNodeId& id) const
{
    auto type = TypeFromId(id.ObjectId);
    return CypressManager->GetHandler(type)->Instantiate(id);
}

////////////////////////////////////////////////////////////////////////////////

TCypressManager::TCypressManager(TBootstrap* bootstrap)
    : TMetaStatePart(
        bootstrap->GetMetaStateFacade()->GetManager(),
        bootstrap->GetMetaStateFacade()->GetState())
    , Bootstrap(bootstrap)
    , NodeMap(TNodeMapTraits(this))
    , TypeToHandler(MaxObjectType)
{
    YCHECK(bootstrap);
    VERIFY_INVOKER_AFFINITY(bootstrap->GetMetaStateFacade()->GetInvoker(), StateThread);

    auto transactionManager = bootstrap->GetTransactionManager();
    transactionManager->SubscribeTransactionCommitted(BIND(
        &TThis::OnTransactionCommitted,
        MakeStrong(this)));
    transactionManager->SubscribeTransactionAborted(BIND(
        &TThis::OnTransactionAborted,
        MakeStrong(this)));

    RegisterHandler(New<TStringNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TIntegerNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TDoubleNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TMapNodeTypeHandler>(Bootstrap));
    RegisterHandler(New<TListNodeTypeHandler>(Bootstrap));

    auto metaState = bootstrap->GetMetaStateFacade()->GetState();
    TLoadContext context(bootstrap);
    metaState->RegisterLoader(
        "Cypress.Keys.1",
        BIND(&TCypressManager::LoadKeys, MakeStrong(this)));
    metaState->RegisterLoader(
        "Cypress.Values.1",
        BIND(&TCypressManager::LoadValues, MakeStrong(this), context));
    metaState->RegisterSaver(
        "Cypress.Keys.1",
        BIND(&TCypressManager::SaveKeys, MakeStrong(this)),
        ESavePhase::Keys);
    metaState->RegisterSaver(
        "Cypress.Values.1",
        BIND(&TCypressManager::SaveValues, MakeStrong(this)),
        ESavePhase::Values);

    metaState->RegisterPart(this);
}

void TCypressManager::RegisterHandler(INodeTypeHandlerPtr handler)
{
    // No thread affinity is given here.
    // This will be called during init-time only.
    YCHECK(handler);

    auto type = handler->GetObjectType();
    int typeValue = type.ToValue();
    YCHECK(typeValue >= 0 && typeValue < MaxObjectType);
    YCHECK(!TypeToHandler[typeValue]);
    TypeToHandler[typeValue] = handler;

    Bootstrap->GetObjectManager()->RegisterHandler(New<TNodeTypeHandler>(this, type));
}

INodeTypeHandlerPtr TCypressManager::FindHandler(EObjectType type)
{
    VERIFY_THREAD_AFFINITY_ANY();

    int typeValue = type.ToValue();
    if (typeValue < 0 || typeValue >= MaxObjectType) {
        return NULL;
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

INodeTypeHandlerPtr TCypressManager::GetHandler(const ICypressNode* node)
{
    return GetHandler(node->GetObjectType());
}

ICypressNode* TCypressManager::CreateNode(
    INodeTypeHandlerPtr handler,
    NTransactionServer::TTransaction* transaction,
    TReqCreate* request,
    TRspCreate* response,
    IAttributeDictionary* attributes)
{
    YASSERT(handler);
    YASSERT(request);
    YASSERT(response);

    auto node = handler->Create(
        transaction,
        request,
        response);
    
    // Make a rawptr copy, next call will transfer the ownership.
    auto node_ = ~node;
    RegisterNode(transaction, node, attributes);

    auto nodeId = node_->GetId().ObjectId;
    *response->mutable_object_id() = nodeId.ToProto();

    return node_;
}

void TCypressManager::CreateNodeBehavior(const TNodeId& id)
{
    auto handler = GetHandler(TypeFromId(id));
    auto behavior = handler->CreateBehavior(id);
    if (!behavior)
        return;

    YCHECK(NodeBehaviors.insert(MakePair(id, behavior)).second);

    LOG_DEBUG("Node behavior created (NodeId: %s)",  ~id.ToString());
}

void TCypressManager::DestroyNodeBehavior(const TNodeId& id)
{
    auto it = NodeBehaviors.find(id);
    if (it == NodeBehaviors.end())
        return;

    it->second->Destroy();
    NodeBehaviors.erase(it);

    LOG_DEBUG("Node behavior destroyed (NodeId: %s)", ~id.ToString());
}

TNodeId TCypressManager::GetRootNodeId()
{
    VERIFY_THREAD_AFFINITY_ANY();

    return CreateId(
        EObjectType::MapNode,
        Bootstrap->GetObjectManager()->GetCellId(),
        0xffffffffffffffff);
}

namespace {

class TNotALeaderRootService
    : public TYPathServiceBase
{
public:
    virtual TResolveResult Resolve(const TYPath& path, const Stroka& verb) override
    {
        UNUSED(path);
        UNUSED(verb);
        ythrow NRpc::TServiceException(TError(NRpc::EErrorCode::Unavailable, "Not an active leader"));
    }
};

class TLeaderRootService
    : public TYPathServiceBase
{
public:
    TLeaderRootService(TBootstrap* bootstrap)
        : Bootstrap(bootstrap)
    { }

    virtual TResolveResult Resolve(const TYPath& path, const Stroka& verb) override
    {
        UNUSED(verb);

        // Make a rigorous check at the right thread.
        if (Bootstrap->GetMetaStateFacade()->GetManager()->GetStateStatus() != EPeerStatus::Leading) {
            ythrow yexception() << "Not a leader";
        }

        auto cypressManager = Bootstrap->GetCypressManager();
        auto service = cypressManager->GetVersionedNodeProxy(
            cypressManager->GetRootNodeId());
        return TResolveResult::There(service, path);
    }

private:
    TBootstrap* Bootstrap;

};

} // namespace

TYPathServiceProducer TCypressManager::GetRootServiceProducer()
{
    auto stateInvoker = Bootstrap->GetMetaStateFacade()->GetInvoker();
    auto this_ = MakeStrong(this);
    return BIND([=] () -> IYPathServicePtr
        {
            // Make a coarse check at this (wrong) thread first.
            auto status = this_->MetaStateManager->GetStateStatusAsync();
            if (status == EPeerStatus::Leading) {
                return New<TLeaderRootService>(Bootstrap)->Via(stateInvoker);
            } else {
                return RefCountedSingleton<TNotALeaderRootService>();
            }
        });

}

IYPathResolverPtr TCypressManager::CreateResolver(TTransaction* transaction)
{
    return New<TYPathResolver>(Bootstrap, transaction);
}

ICypressNode* TCypressManager::FindVersionedNode(
    const TNodeId& nodeId,
    const TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    auto* currentTransaction = transaction;
    while (true) {
        auto* currentNode = FindNode(TVersionedNodeId(nodeId, GetObjectId(currentTransaction)));
        if (currentNode) {
            return currentNode;
        }

        if (!currentTransaction) {
            // Looks like there's no such node at all.
            return NULL;
        }

        // Move to the parent transaction.
        currentTransaction = currentTransaction->GetParent();
    }
}

ICypressNode* TCypressManager::GetVersionedNode(
    const TNodeId& nodeId,
    const TTransaction* transaction)
{
    auto* node = FindVersionedNode(nodeId, transaction);
    YCHECK(node);
    return node;
}

ICypressNodeProxyPtr TCypressManager::FindVersionedNodeProxy(
    const TNodeId& id,
    TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    const auto* node = FindVersionedNode(id, transaction);
    if (!node) {
        return NULL;
    }

    return GetHandler(node)->GetProxy(id, transaction);
}

ICypressNodeProxyPtr TCypressManager::GetVersionedNodeProxy(
    const TNodeId& nodeId,
    TTransaction* transaction)
{
    auto proxy = FindVersionedNodeProxy(nodeId, transaction);
    YCHECK(proxy);
    return proxy;
}

ICypressNodeProxyPtr TCypressManager::GetVersionedNodeProxy(
    const TVersionedNodeId& versionedId)
{
    auto transactionManager = Bootstrap->GetTransactionManager();
    auto* transaction =
        versionedId.TransactionId == NullTransactionId
        ?  NULL
        : transactionManager->GetTransaction(versionedId.TransactionId);
    return GetVersionedNodeProxy(versionedId.ObjectId, transaction);
}

void TCypressManager::ValidateLock(
    ICypressNode* trunkNode,
    TTransaction* transaction,
    ELockMode requestedMode,
    bool* isMandatory)
{
    YASSERT(requestedMode != ELockMode::None);
    YASSERT(isMandatory);

    // Check if the node supports this particular mode at all.
    auto nodeId = trunkNode->GetId().ObjectId;
    auto handler = GetHandler(trunkNode);
    if (!handler->IsLockModeSupported(requestedMode)) {
        ythrow yexception() << Sprintf("Node %s does not support %s locks",
            ~GetNodePath(nodeId, transaction).Quote(),
            ~FormatEnum(requestedMode).Quote());
    }

    // Snapshot locks can only be taken inside a transaction.
    if (requestedMode == ELockMode::Snapshot && !transaction) {
        ythrow yexception() << Sprintf("Cannot take %s lock outside of a transaction",
            ~FormatEnum(requestedMode).Quote());
    }

    // Examine existing locks.
    // A quick check: same transaction, same or weaker lock mode (beware of Snapshot!).
    {
        auto it = trunkNode->Locks().find(transaction);
        if (it != trunkNode->Locks().end()) {
            const auto& existingLock = it->second;
            if ((existingLock.Mode == requestedMode) ||
                (existingLock.Mode > requestedMode && requestedMode != ELockMode::Snapshot))
            {
                *isMandatory = false;
                return;
            }
            if (existingLock.Mode == ELockMode::Snapshot) {
                ythrow yexception() << Sprintf("Cannot take %s lock for node %s since %s lock is already taken by the same transaction",
                    ~FormatEnum(requestedMode).Quote(),
                    ~GetNodePath(nodeId, transaction).Quote(),
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
        if (requestedMode == ELockMode::Snapshot &&
            IsParentTransaction(existingTransaction, transaction))
        {
            ythrow yexception() << Sprintf("Cannot take %s lock for node %s since %s lock is taken by descendant transaction %s",
                ~FormatEnum(requestedMode).Quote(),
                ~GetNodePath(nodeId, transaction).Quote(),
                ~FormatEnum(existingLock.Mode).Quote(),
                ~existingTransaction->GetId().ToString());
        }

        // For Exclusive and Shared locks we check the locks held by concurrent transactions.
        if (IsConcurrentTransaction(transaction, existingTransaction) &&
            (requestedMode == ELockMode::Exclusive || existingLock.Mode == ELockMode::Exclusive))
        {
            ythrow yexception() << Sprintf("Cannot take %s lock for node %s since %s lock is taken by concurrent transaction %s",
                ~FormatEnum(requestedMode).Quote(),
                ~GetNodePath(nodeId, transaction).Quote(),
                ~FormatEnum(existingLock.Mode).Quote(),
                ~existingTransaction->GetId().ToString());
        }
    }

    // If we're outside of a transaction then the lock is not needed.
    *isMandatory = (transaction != NULL);
}

void TCypressManager::ValidateLock(
    ICypressNode* trunkNode,
    TTransaction* transaction,
    ELockMode requestedMode)
{
    bool dummy;
    ValidateLock(trunkNode, transaction, requestedMode, &dummy);
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

ICypressNode* TCypressManager::AcquireLock(
    ICypressNode* trunkNode,
    TTransaction* transaction,
    ELockMode mode)
{
    YCHECK(transaction);

    auto* lock = CreateLock(trunkNode, transaction, mode);

    // Upgrade locks held by parent transactions, if needed.
    if (mode != ELockMode::Snapshot) {
        auto* currentTransaction = transaction->GetParent();
        while (currentTransaction) {
            CreateLock(trunkNode, currentTransaction, mode);
            currentTransaction = currentTransaction->GetParent();
        }
    }

    // Branch node, if needed.
    auto nodeId = trunkNode->GetId().ObjectId;
    auto* branchedNode = FindNode(TVersionedNodeId(nodeId, transaction->GetId()));
    if (branchedNode) {
        if (branchedNode->GetLockMode() < mode) {
            branchedNode->SetLockMode(mode);
        }
        return branchedNode;
    }

    ICypressNode* originatingNode;
    std::vector<TTransaction*> intermediateTransactions;
    // Walk up to the root, find originatingNode, construct the list of
    // intermediate transactions.
    auto* currentTransaction = transaction;
    while (true) {
        originatingNode = FindNode(TVersionedNodeId(nodeId, GetObjectId(currentTransaction)));
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

    if (mode == ELockMode::Snapshot) {
        // Branch at requested transaction only.
        return BranchNode(originatingNode, transaction, mode);
    } else {
        // Branch at all intermediate transactions.
        std::reverse(intermediateTransactions.begin(), intermediateTransactions.end());
        auto* currentNode = originatingNode;
        FOREACH (auto* transactionToBranch, intermediateTransactions) {
            currentNode = BranchNode(currentNode, transactionToBranch, mode);
        }
        return currentNode;
    }
}

TLock* TCypressManager::CreateLock(
    ICypressNode* trunkNode,
    TTransaction* transaction,
    ELockMode mode)
{
    TVersionedNodeId versionedId(trunkNode->GetId().ObjectId, transaction->GetId());

    auto it = trunkNode->Locks().find(transaction);
    
    if (it == trunkNode->Locks().end()) {
        auto& lock = trunkNode->Locks()[transaction];
        lock.Mode = mode;

        transaction->LockedNodes().push_back(trunkNode);

        LOG_INFO_UNLESS(IsRecovery(), "Node locked (NodeId: %s, Mode: %s)",
            ~versionedId.ToString(),
            ~mode.ToString());

        return &lock;
    } else {
        auto& lock = it->second;
        if (lock.Mode < mode) {
            lock.Mode = mode;

            LOG_INFO_UNLESS(IsRecovery(), "Node lock upgraded (NodeId: %s, Mode: %s)",
                ~versionedId.ToString(),
                ~mode.ToString());
        }

        return &lock;
    }
}

void TCypressManager::ReleaseLock(ICypressNode* trunkNode, TTransaction* transaction)
{
    YCHECK(trunkNode->Locks().erase(transaction) == 1);

    LOG_INFO_UNLESS(IsRecovery(), "Node unlocked (NodeId: %s, TransactionId: %s)",
        ~trunkNode->GetId().ToString(),
        ~transaction->GetId().ToString());
}

ICypressNode* TCypressManager::LockVersionedNode(
    const TNodeId& nodeId,
    TTransaction* transaction,
    ELockMode requestedMode,
    bool recursive)
{
    auto* trunkNode = GetNode(nodeId);
    return LockVersionedNode(trunkNode, transaction, requestedMode, recursive);
}

ICypressNode* TCypressManager::LockVersionedNode(
    ICypressNode* node,
    TTransaction* transaction,
    ELockMode requestedMode,
    bool recursive)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YCHECK(requestedMode != ELockMode::None);

    TSubtreeNodes nodesToLock;
    if (recursive) {
        ListSubtreeNodeIds(node, transaction, &nodesToLock);
    } else {
        nodesToLock.push_back(node);
    }

    // Validate all potentials lock to see if we need to take at least one of them.
    // This throws an exception in case the validation fails.
    bool isMandatory = false;
    FOREACH (auto* child, nodesToLock) {
        bool isChildMandatory;
        ValidateLock(child->GetTrunkNode(), transaction, requestedMode, &isChildMandatory);
        isMandatory |= isChildMandatory;
    }

    if (!isMandatory) {
        return GetVersionedNode(node->GetId().ObjectId, transaction);
    }

    if (!transaction) {
        ythrow yexception() << Sprintf("The requested operation requires %s lock but no current transaction is given",
            ~FormatEnum(requestedMode).Quote());
    }

    ICypressNode* lockedNode = NULL;
    FOREACH (auto* child, nodesToLock) {
        auto* lockedChild = AcquireLock(child->GetTrunkNode(), transaction, requestedMode);
        if (child == node) {
            lockedNode = lockedChild;
        }
    }

    YCHECK(lockedNode);
    return lockedNode;
}

void TCypressManager::RegisterNode(
    TTransaction* transaction,
    TAutoPtr<ICypressNode> node,
    IAttributeDictionary* attributes)
{
    auto nodeId = node->GetId().ObjectId;
    YASSERT(node->GetId().TransactionId == NullTransactionId);
    
    auto objectManager = Bootstrap->GetObjectManager();
    auto* mutationContext = Bootstrap
        ->GetMetaStateFacade()
        ->GetManager()
        ->GetMutationContext();

    node->SetCreationTime(mutationContext->GetTimestamp());

    auto node_ = node.Get();
    NodeMap.Insert(nodeId, node.Release());

    // TODO(babenko): setting attributes here, in RegisterNode
    // is somewhat weird. Moving this logic to some other place, however,
    // complicates the code since we need to worry about possible
    // exceptions thrown from custom attribute validators.
    if (attributes) {
        auto proxy = GetVersionedNodeProxy(nodeId, transaction);
        try {
            proxy->Attributes().MergeFrom(*attributes);
        } catch (...) {
            GetHandler(node_)->Destroy(node_);
            NodeMap.Remove(nodeId);
            throw;
        }
    }

    if (transaction) {
        transaction->CreatedNodes().push_back(node_);
        objectManager->RefObject(node_);
    }

    LOG_INFO_UNLESS(IsRecovery(), "Node registered (NodeId: %s, Type: %s)",
        ~node_->GetId().ToString(),
        ~TypeFromId(nodeId).ToString());

    if (IsLeader()) {
        CreateNodeBehavior(nodeId);
    }
}

ICypressNode* TCypressManager::BranchNode(
    ICypressNode* node,
    TTransaction* transaction,
    ELockMode mode)
{
    YASSERT(transaction);

    VERIFY_THREAD_AFFINITY(StateThread);

    auto id = node->GetId();

    // Create a branched node and initialize its state.
    auto branchedNode = GetHandler(node)->Branch(node, transaction, mode);
    YASSERT(branchedNode->GetLockMode() == mode);
    auto* branchedNode_ = branchedNode.Release();
    NodeMap.Insert(TVersionedNodeId(id.ObjectId, transaction->GetId()), branchedNode_);

    // Register the branched node with the transaction.
    transaction->BranchedNodes().push_back(branchedNode_);

    // The branched node holds an implicit reference to its originator.
    Bootstrap->GetObjectManager()->RefObject(branchedNode_);
    
    LOG_INFO_UNLESS(IsRecovery(), "Node branched (NodeId: %s, Mode: %s)",
        ~id.ToString(),
        ~mode.ToString());

    return branchedNode_;
}

void TCypressManager::SaveKeys(TOutputStream* output) const
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.SaveKeys(output);
}

void TCypressManager::SaveValues(TOutputStream* output) const
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.SaveValues(output);
}

void TCypressManager::LoadKeys(TInputStream* input)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.LoadKeys(input);
}

void TCypressManager::LoadValues(const TLoadContext& context, TInputStream* input)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.LoadValues(context, input);
}

void TCypressManager::Clear()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    NodeMap.Clear();

    // Create the root.
    auto* root = new TMapNode(GetRootNodeId());
    root->SetTrunkNode(root);
    NodeMap.Insert(root->GetId(), root);
    Bootstrap->GetObjectManager()->RefObject(root);}

void TCypressManager::OnLeaderRecoveryComplete()
{
    YCHECK(NodeBehaviors.empty());
    FOREACH (const auto& pair, NodeMap) {
        if (!pair.first.IsBranched()) {
            CreateNodeBehavior(pair.first.ObjectId);
        }
    }
}

void TCypressManager::OnStopLeading()
{
    FOREACH (const auto& pair, NodeBehaviors) {
        pair.second->Destroy();
    }
    NodeBehaviors.clear();
}

i32 TCypressManager::RefNode(const TNodeId& nodeId)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    auto* node = NodeMap.Get(nodeId);
    return node->RefObject();
}

i32 TCypressManager::UnrefNode(const TNodeId& nodeId)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    auto* node = NodeMap.Get(nodeId);
    return node->UnrefObject();
}

void TCypressManager::DestroyNode(const TNodeId& nodeId)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    DestroyNodeBehavior(nodeId);
    
    TAutoPtr<ICypressNode> node(NodeMap.Release(nodeId));
    GetHandler(~node)->Destroy(~node);
}

i32 TCypressManager::GetNodeRefCounter(const TNodeId& nodeId)
{
    const auto* node = NodeMap.Get(nodeId);
    return node->GetObjectRefCounter();
}

void TCypressManager::OnTransactionCommitted(TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    ReleaseLocks(transaction);
    MergeNodes(transaction);
    ReleaseCreatedNodes(transaction);
}

void TCypressManager::OnTransactionAborted(TTransaction* transaction)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    ReleaseLocks(transaction);
    RemoveBranchedNodes(transaction);
    ReleaseCreatedNodes(transaction);
}

void TCypressManager::ReleaseLocks(TTransaction* transaction)
{
    FOREACH (auto* trunkNode, transaction->LockedNodes()) {
        ReleaseLock(trunkNode, transaction);
    }
    transaction->LockedNodes().clear();
}

void TCypressManager::ListSubtreeNodeIds(
    ICypressNode* root,
    NTransactionServer::TTransaction* transaction,
    TSubtreeNodes* subtreeNodes)
{
    auto transactionManager = Bootstrap->GetTransactionManager();
    
    auto rootId = root->GetId().ObjectId;
    subtreeNodes->push_back(root);  
    switch (TypeFromId(rootId)) {
        case EObjectType::MapNode: {
            auto transactions = transactionManager->GetTransactionPath(transaction);
            std::reverse(transactions.begin(), transactions.end());

            yhash_map<Stroka, ICypressNode*> children;
            FOREACH (const auto* transaction, transactions) {
                const auto* node = GetVersionedNode(rootId, transaction);
                const auto* mapNode = static_cast<const TMapNode*>(node);
                FOREACH (const auto& pair, mapNode->KeyToChild()) {
                    if (pair.second == NullObjectId) {
                        YCHECK(children.erase(pair.first) == 1);
                    } else {
                        auto* child = GetVersionedNode(pair.second, transaction);
                        YCHECK(children.insert(std::make_pair(pair.first, child)).second);
                    }
                }
            }

            FOREACH (const auto& pair, children) {
                ListSubtreeNodeIds(pair.second, transaction, subtreeNodes);
            }
            break;
        }

        case EObjectType::ListNode: {
            auto* listRoot = static_cast<TListNode*>(root);
            FOREACH (const auto& childId, listRoot->IndexToChild()) {
                auto* child = GetVersionedNode(childId, transaction);
                ListSubtreeNodeIds(child, transaction, subtreeNodes);
            }
            break;
        }
    }
}

void TCypressManager::MergeNode(TTransaction* transaction, ICypressNode* branchedNode)
{
    auto objectManager = Bootstrap->GetObjectManager();
    auto handler = GetHandler(branchedNode);
    
    auto branchedId = branchedNode->GetId();
    auto* parentTransaction = transaction->GetParent();
    auto originatingId = TVersionedNodeId(branchedId.ObjectId, GetObjectId(parentTransaction));

    if (branchedNode->GetLockMode() != ELockMode::Snapshot) {
        // Merge changes back.
        auto* originatingNode = NodeMap.Get(originatingId);
        handler->Merge(originatingNode, branchedNode);
        LOG_INFO_UNLESS(IsRecovery(), "Node merged (NodeId: %s)", ~branchedId.ToString());
    } else {
        handler->Destroy(branchedNode);
        LOG_INFO_UNLESS(IsRecovery(), "Node snapshot destroyed (NodeId: %s)", ~branchedId.ToString());
    }

    // Remove the branched copy.
    NodeMap.Remove(branchedId);

    // Drop the implicit reference to the originator.
    objectManager->UnrefObject(originatingId);

    LOG_INFO_UNLESS(IsRecovery(), "Branched node removed (NodeId: %s)", ~branchedId.ToString());
}

void TCypressManager::MergeNodes(TTransaction* transaction)
{
    FOREACH (auto* node, transaction->BranchedNodes()) {
        MergeNode(transaction, node);
    }
    transaction->BranchedNodes().clear();
}

void TCypressManager::ReleaseCreatedNodes(TTransaction* transaction)
{
    auto objectManager = Bootstrap->GetObjectManager();
    FOREACH (auto* node, transaction->CreatedNodes()) {
        objectManager->UnrefObject(node);
    }
    transaction->CreatedNodes().clear();
}

void TCypressManager::RemoveBranchedNodes(TTransaction* transaction)
{
    auto objectManager = Bootstrap->GetObjectManager();
    FOREACH (auto* branchedNode, transaction->BranchedNodes()) {
        // Remove the node.
        auto branchedNodeId = branchedNode->GetId();
        GetHandler(branchedNode)->Destroy(branchedNode);
        NodeMap.Remove(branchedNodeId);

        // Drop the implicit reference to the originator.
        objectManager->UnrefObject(branchedNodeId);

        LOG_INFO_UNLESS(IsRecovery(), "Branched node removed (NodeId: %s)", ~branchedNodeId.ToString());
    }
    transaction->BranchedNodes().clear();
}

TYPath TCypressManager::GetNodePath(
    const TNodeId& nodeId,
    TTransaction* transaction)
{
    auto proxy = GetVersionedNodeProxy(nodeId, transaction);
    return proxy->GetResolver()->GetPath(proxy);
}

DEFINE_METAMAP_ACCESSORS(TCypressManager, Node, ICypressNode, TVersionedNodeId, NodeMap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypressServer
} // namespace NYT
