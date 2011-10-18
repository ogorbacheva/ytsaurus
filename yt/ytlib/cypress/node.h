#pragma once

#include "common.h"

#include "../misc/property.h"
#include "../ytree/node_detail.h"
#include "../transaction_manager/common.h"

namespace NYT {
namespace NCypress {

using NTransaction::TTransactionId;
using NTransaction::NullTransactionId;

////////////////////////////////////////////////////////////////////////////////

//! Identifies a node possibly branched by a transaction.
struct TBranchedNodeId
{
    //! Id of the node itself.
    TNodeId NodeId;

    //! Id of the transaction that had branched the node.
    TTransactionId TransactionId;

    //! Initializes a instance by given node and transaction ids.
    TBranchedNodeId(const TNodeId& nodeId, const TTransactionId& transactionId);

    //! Checks that the id is branched, i.e. #TransactionId it not #NullTransactionId.
    bool IsBranched() const;

    //! Formats the id to string (for debugging and logging purposes mainly).
    Stroka ToString() const;
};

bool operator==(const TBranchedNodeId& lhs, const TBranchedNodeId& rhs);
inline bool operator!=(const TBranchedNodeId& lhs, const TBranchedNodeId& rhs);

} // namespace NCypress
} // namespace NYT

//! A hasher for TBranchedNodeId.
template <>
struct hash<NYT::NCypress::TBranchedNodeId>
{
    i32 operator()(const NYT::NCypress::TBranchedNodeId& id) const;
};

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

struct ICypressNodeProxy;
class TCypressManager;

//! Describes the state of the persisted node.
DECLARE_ENUM(ENodeState,
    // The node is present in the HEAD version.
    (Committed)
    // The node is a branched copy of another committed node.
    (Branched)
    // The node is created by the transaction and is thus new.
    (Uncommitted)
);

//! Provides a common interface for all persistent nodes.
struct ICypressNode
{
    virtual ~ICypressNode()
    { }

    //! Returns the id of the node (which is the key in the respective meta-map).
    virtual TBranchedNodeId GetId() const = 0;

    //! Gets node state.
    virtual ENodeState GetState() const = 0;
    //! Sets node state.
    virtual void SetState(const ENodeState& value) = 0;

    //! Gets parent node id.
    virtual TNodeId GetParentId() const = 0;
    //! Sets parent node id.
    virtual void SetParentId(const TNodeId& value) = 0;

    //! Gets attributes node id.
    virtual TNodeId GetAttributesId() const = 0;
    //! Sets attributes node id.
    virtual void SetAttributesId(const TNodeId& value) = 0;

    //! Gets an immutable reference to the node's locks.
    virtual const yhash_set<TLockId>& LockIds() const = 0;
    //! Gets an mutable reference to the node's locks.
    virtual yhash_set<TLockId>& LockIds() = 0;

    //! Increments the reference counter, returns the incremented value.
    virtual int Ref() = 0;
    //! Decrements the reference counter, returns the decremented value.
    virtual int Unref() = 0;

    // TODO: this shouldn't be a part of public interface
    virtual TAutoPtr<ICypressNode> Clone() const = 0;

    //! Constructs a proxy.
    /*!
     *  \param cypressManager A cypress manager.
     *  \param transactionId The id of the transaction for which the proxy
     *  is being created, may be #NullTransactionId.
     *  \return The constructed proxy.
     */
    virtual TIntrusivePtr<ICypressNodeProxy> GetProxy(
        TIntrusivePtr<TCypressManager> cypressManager,
        const TTransactionId& transactionId) const = 0;

    //! Branches a committed node into a given transaction.
    /*!
     *  \param transactionId The id of the transaction that is about to
     *  modify the node.
     *  \return A branched node.
     */
    virtual TAutoPtr<ICypressNode> Branch(const TTransactionId& transactionId) const = 0;
    
    //! Merges the changes made in the branched node back into the committed one.
    /*!
     *  \param cypressManager A cypress manager.
     *  \param branchedNode A branched node.
     *
     *  \note 
     *  #branchedNode is non-const for performance reasons (i.e. to swap the data instead of copying).
     */
    virtual void Merge(
        TIntrusivePtr<TCypressManager> cypressManager,
        ICypressNode& branchedNode) = 0;


    //! Performs cleanup on node destruction.
    /*!
     *  This is called prior to the actual removal of the node from the meta-map.
     *  A typical implementation will release the resources held by the node,
     *  decrement the ref-counters of the children etc.
     *  
     *  \param cypressManager A cypress manager.
     *  
     *  \note This method is only called for committed and uncommitted nodes.
     *  It is not called for branched ones.
     */
    virtual void Destroy(TIntrusivePtr<TCypressManager> cypressManager) = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TCypressNodeBase
    : public ICypressNode
{
    // This also overrides appropriate methods from ICypressNode.
    DECLARE_BYREF_RW_PROPERTY(LockIds, yhash_set<TLockId>);
    DECLARE_BYVAL_RW_PROPERTY(ParentId, TNodeId);
    DECLARE_BYVAL_RW_PROPERTY(AttributesId, TNodeId);
    DECLARE_BYVAL_RW_PROPERTY(State, ENodeState);

public:
    TCypressNodeBase(const TBranchedNodeId& id);

    virtual TBranchedNodeId GetId() const;

    virtual int Ref();
    virtual int Unref();

    virtual void Destroy(TIntrusivePtr<TCypressManager> cypressManager);

protected:
    TCypressNodeBase(const TBranchedNodeId& id, const TCypressNodeBase& other);

    virtual void Merge(
        TIntrusivePtr<TCypressManager> cypressManager,
        ICypressNode& branchedNode);

    TBranchedNodeId Id;
    int RefCounter;

};

//////////////////////////////////////////////////////////////////////////////// 

// TODO: move impl to inl
template <class TValue>
class TScalarNode
    : public TCypressNodeBase
{
    DECLARE_BYREF_RW_PROPERTY(Value, TValue)

private:
    typedef TScalarNode<TValue> TThis;

    TScalarNode(const TBranchedNodeId& id, const TThis& other)
        : TCypressNodeBase(id, other)
        , Value_(other.Value_)
    { }

public:
    TScalarNode(const TBranchedNodeId& id)
        : TCypressNodeBase(id)
    { }

    virtual TAutoPtr<ICypressNode> Branch(const TTransactionId& transactionId) const
    {
        YASSERT(!Id.IsBranched());
        return new TThis(
            TBranchedNodeId(Id.NodeId, transactionId),
            *this);
    }

    virtual void Merge(
        TIntrusivePtr<TCypressManager> cypressManager,
        ICypressNode& branchedNode)
    {
        TCypressNodeBase::Merge(cypressManager, branchedNode);

        const auto& typedBranchedNode = dynamic_cast<const TThis&>(branchedNode);
        Value() = typedBranchedNode.Value();
    }

    virtual TAutoPtr<ICypressNode> Clone() const
    {
        return new TThis(Id, *this);
    }

    virtual TIntrusivePtr<ICypressNodeProxy> GetProxy(
        TIntrusivePtr<TCypressManager> state,
        const TTransactionId& transactionId) const;
};

typedef TScalarNode<Stroka> TStringNode;
typedef TScalarNode<i64>    TInt64Node;
typedef TScalarNode<double> TDoubleNode;

//////////////////////////////////////////////////////////////////////////////// 

class TMapNode
    : public TCypressNodeBase
{
    typedef yhash_map<Stroka, TNodeId> TNameToChild;
    typedef yhash_map<TNodeId, Stroka> TChildToName;

    DECLARE_BYREF_RW_PROPERTY(NameToChild, TNameToChild);
    DECLARE_BYREF_RW_PROPERTY(ChildToName, TChildToName);

private:
    typedef TMapNode TThis;

    TMapNode(const TBranchedNodeId& id, const TMapNode& other);

public:
    TMapNode(const TBranchedNodeId& id);

    virtual TAutoPtr<ICypressNode> Branch(const TTransactionId& transactionId) const;
    virtual void Merge(
        TIntrusivePtr<TCypressManager> cypressManager,
        ICypressNode& branchedNode);

    virtual TAutoPtr<ICypressNode> Clone() const;

    virtual TIntrusivePtr<ICypressNodeProxy> GetProxy(
        TIntrusivePtr<TCypressManager> state,
        const TTransactionId& transactionId) const;

    virtual void Destroy(TIntrusivePtr<TCypressManager> cypressManager);

};

//////////////////////////////////////////////////////////////////////////////// 

class TListNode
    : public TCypressNodeBase
{
    typedef yvector<TNodeId> TIndexToChild;
    typedef yhash_map<TNodeId, int> TChildToIndex;

    DECLARE_BYREF_RW_PROPERTY(IndexToChild, TIndexToChild);
    DECLARE_BYREF_RW_PROPERTY(ChildToIndex, TChildToIndex);

private:
    typedef TListNode TThis;

    TListNode(const TBranchedNodeId& id, const TListNode& other);

public:
    TListNode(const TBranchedNodeId& id);

    virtual TAutoPtr<ICypressNode> Branch(const TTransactionId& transactionId) const;
    virtual void Merge(
        TIntrusivePtr<TCypressManager> cypressManager,
        ICypressNode& branchedNode);

    virtual TAutoPtr<ICypressNode> Clone() const;

    virtual TIntrusivePtr<ICypressNodeProxy> GetProxy(
        TIntrusivePtr<TCypressManager> state,
        const TTransactionId& transactionId) const;

    virtual void Destroy(TIntrusivePtr<TCypressManager> cypressManager);

};

//////////////////////////////////////////////////////////////////////////////// 

} // namespace NCypress
} // namespace NYT
