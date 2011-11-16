#pragma once

#include "common.h"
#include "node.h"
#include "node_proxy.h"
#include "lock.h"
#include "cypress_manager.pb.h"

#include "../misc/thread_affinity.h"
#include "../transaction_server/transaction.h"
#include "../transaction_server/transaction_manager.h"
#include "../ytree/ypath_service.h"
#include "../ytree/tree_builder.h"
#include "../misc/id_generator.h"
#include "../meta_state/meta_state_manager.h"
#include "../meta_state/composite_meta_state.h"
#include "../meta_state/map.h"
#include "../meta_state/meta_change.h"

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

struct ICypressNodeProxy;

class TCypressManager
    : public NMetaState::TMetaStatePart
{
public:
    typedef TCypressManager TThis;
    typedef TIntrusivePtr<TThis> TPtr;

    TCypressManager(
        NMetaState::TMetaStateManager* metaStateManager,
        NMetaState::TCompositeMetaState* metaState,
        NTransaction::TTransactionManager* transactionManager);

    void RegisterNodeType(INodeTypeHandler* handler);

    METAMAP_ACCESSORS_DECL(Node, ICypressNode, TBranchedNodeId);

    const ICypressNode* FindTransactionNode(
        const TNodeId& nodeId,
        const TTransactionId& transactionId);

    const ICypressNode& GetTransactionNode(
        const TNodeId& nodeId,
        const TTransactionId& transactionId);

    ICypressNode* FindTransactionNodeForUpdate(
        const TNodeId& nodeId,
        const TTransactionId& transactionId);

    ICypressNode& GetTransactionNodeForUpdate(
        const TNodeId& nodeId,
        const TTransactionId& transactionId);

    TIntrusivePtr<ICypressNodeProxy> GetNodeProxy(
        const TNodeId& nodeId,
        const TTransactionId& transactionId);

    bool IsTransactionNodeLocked(
        const TNodeId& nodeId,
        const TTransactionId& transactionId);

    TLockId LockTransactionNode(
        const TNodeId& nodeId,
        const TTransactionId& transactionId);

    void RefNode(ICypressNode& node);
    void RefNode(const TNodeId& nodeId);
    void UnrefNode(ICypressNode & node);
    void UnrefNode(const TNodeId& nodeId);

    NYTree::IStringNode::TPtr CreateStringNodeProxy(const TTransactionId& transactionId);
    NYTree::IInt64Node::TPtr  CreateInt64NodeProxy(const TTransactionId& transactionId);
    NYTree::IDoubleNode::TPtr CreateDoubleNodeProxy(const TTransactionId& transactionId);
    NYTree::IMapNode::TPtr    CreateMapNodeProxy(const TTransactionId& transactionId);
    NYTree::IListNode::TPtr   CreateListNodeProxy(const TTransactionId& transactionId);

    TIntrusivePtr<ICypressNodeProxy> CreateDynamicNode(
        const TTransactionId& transactionId,
        NYTree::INode* manifest);

    METAMAP_ACCESSORS_DECL(Lock, TLock, TLockId);

    TLock& CreateLock(const TNodeId& nodeId, const TTransactionId& transactionId);

    ICypressNode& BranchNode(ICypressNode& node, const TTransactionId& transactionId);

    void ExecuteVerb(NYTree::IYPathService* service, NRpc::IServiceContext* context);

private:
    class TNodeMapTraits
    {
    public:
        TNodeMapTraits(TCypressManager* cypressManager);

        TAutoPtr<ICypressNode> Clone(ICypressNode* value) const;
        void Save(ICypressNode* value, TOutputStream* output) const;
        TAutoPtr<ICypressNode> Load(const TBranchedNodeId& id, TInputStream* input) const;

    private:
        TCypressManager::TPtr CypressManager;

    };
    
    const NTransaction::TTransactionManager::TPtr TransactionManager;

    TIdGenerator<TNodeId> NodeIdGenerator;
    NMetaState::TMetaStateMap<TBranchedNodeId, ICypressNode, TNodeMapTraits> NodeMap;

    TIdGenerator<TLockId> LockIdGenerator; 
    NMetaState::TMetaStateMap<TLockId, TLock> LockMap;

    yvector<INodeTypeHandler::TPtr> RuntimeTypeToHandler;
    yhash_map<Stroka, INodeTypeHandler::TPtr> TypeNameToHandler;

    TVoid DoExecuteVerb(const NProto::TMsgExecuteVerb& message);
    TVoid DoExecuteVerbFast(NYTree::IYPathService::TPtr service, NRpc::IServiceContext::TPtr context);

    // TMetaStatePart overrides.
    TFuture<TVoid>::TPtr Save(const NMetaState::TCompositeMetaState::TSaveContext& context);
    void Load(TInputStream* input);
    virtual void Clear();

    void OnTransactionCommitted(const NTransaction::TTransaction& transaction);
    void OnTransactionAborted(const NTransaction::TTransaction& transaction);

    void ReleaseLocks(const NTransaction::TTransaction& transaction);
    void MergeBranchedNodes(const NTransaction::TTransaction& transaction);
    void RemoveBranchedNodes(const NTransaction::TTransaction& transaction);
    void UnrefOriginatingNodes(const NTransaction::TTransaction& transaction);
    void CommitCreatedNodes(const NTransaction::TTransaction& transaction);

    INodeTypeHandler::TPtr GetTypeHandler(const ICypressNode& node);
    INodeTypeHandler::TPtr GetTypeHandler(ERuntimeNodeType type);

    template <class TImpl, class TProxy>
    TIntrusivePtr<TProxy> CreateNode(
        const TTransactionId& transactionId,
        ERuntimeNodeType type);

    class TDeserializationBuilder;
    friend class TDeserializationBuilder;

    DECLARE_THREAD_AFFINITY_SLOT(StateThread);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT
