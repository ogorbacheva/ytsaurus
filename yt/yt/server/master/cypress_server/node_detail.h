#pragma once

#include "cypress_manager.h"
#include "helpers.h"
#include "node.h"
#include "private.h"

#include <yt/yt/server/master/cell_master/bootstrap.h>
#include <yt/yt/server/master/cell_master/serialize.h>

#include <yt/yt/server/master/chunk_server/chunk_requisition.h>

#include <yt/yt/server/master/object_server/attribute_set.h>
#include <yt/yt/server/master/object_server/object_detail.h>
#include <yt/yt/server/master/object_server/object_part_cow_ptr.h>
#include <yt/yt/server/master/object_server/type_handler_detail.h>

#include <yt/yt/server/master/security_server/account.h>
#include <yt/yt/server/master/security_server/detailed_master_memory.h>
#include <yt/yt/server/master/security_server/security_manager.h>

#include <yt/yt/server/master/tablet_server/tablet_cell_bundle.h>

#include <yt/yt/server/lib/tablet_node/public.h>

#include <yt/yt/ytlib/table_client/public.h>

#include <yt/yt/core/misc/serialize.h>

#include <yt/yt/core/ytree/ephemeral_node_factory.h>
#include <yt/yt/core/ytree/fluent.h>
#include <yt/yt/core/ytree/node_detail.h>
#include <yt/yt/core/ytree/overlaid_attribute_dictionaries.h>
#include <yt/yt/core/ytree/tree_builder.h>
#include <yt/yt_proto/yt/core/ytree/proto/ypath.pb.h>

namespace NYT::NCypressServer {

////////////////////////////////////////////////////////////////////////////////

class TNontemplateCypressNodeTypeHandlerBase
    : public INodeTypeHandler
{
public:
    explicit TNontemplateCypressNodeTypeHandlerBase(NCellMaster::TBootstrap* bootstrap);

    NObjectServer::ETypeFlags GetFlags() const override;

    void FillAttributes(
        TCypressNode* trunkNode,
        NYTree::IAttributeDictionary* inheritedAttributes,
        NYTree::IAttributeDictionary* explicitAttributes) override;

    virtual bool IsSupportedInheritableAttribute(const TString& /*key*/) const;

    NObjectServer::TAcdList ListAcds(TCypressNode* trunkNode) const override;

protected:
    NCellMaster::TBootstrap* const Bootstrap_;

    NObjectServer::TObjectTypeMetadata Metadata_;


    bool IsLeader() const;
    bool IsRecovery() const;
    bool IsMutationLoggingEnabled() const;
    const TDynamicCypressManagerConfigPtr& GetDynamicCypressManagerConfig() const;

    void DestroyCorePrologue(TCypressNode* node);

    bool BeginCopyCore(
        TCypressNode* node,
        TBeginCopyContext* context);
    TCypressNode* EndCopyCore(
        TEndCopyContext* context,
        ICypressNodeFactory* factory,
        TNodeId sourceNodeId,
        bool* needCustomEndCopy);
    void EndCopyInplaceCore(
        TCypressNode* trunkNode,
        TEndCopyContext* context,
        ICypressNodeFactory* factory,
        TNodeId sourceNodeId);
    bool LoadInplace(
        TCypressNode* trunkNode,
        TEndCopyContext* context,
        ICypressNodeFactory* factory);

    void BranchCorePrologue(
        TCypressNode* originatingNode,
        TCypressNode* branchedNode,
        NTransactionServer::TTransaction* transaction,
        const TLockRequest& lockRequest);
    void BranchCoreEpilogue(
        TCypressNode* branchedNode);

    void MergeCorePrologue(
        TCypressNode* originatingNode,
        TCypressNode* branchedNode);
    void MergeCoreEpilogue(
        TCypressNode* originatingNode,
        TCypressNode* branchedNode);

    TCypressNode* CloneCorePrologue(
        ICypressNodeFactory* factory,
        TNodeId hintId,
        TCypressNode* sourceNode,
        NSecurityServer::TAccount* account);
    void CloneCoreEpilogue(
        TCypressNode* sourceNode,
        TCypressNode* clonedTrunkNode,
        ICypressNodeFactory* factory,
        ENodeCloneMode mode);
};

////////////////////////////////////////////////////////////////////////////////

template <class TImpl>
class TCypressNodeTypeHandlerBase
    : public TNontemplateCypressNodeTypeHandlerBase
{
public:
    explicit TCypressNodeTypeHandlerBase(NCellMaster::TBootstrap* bootstrap)
        : TNontemplateCypressNodeTypeHandlerBase(bootstrap)
    { }

    ICypressNodeProxyPtr GetProxy(
        TCypressNode* trunkNode,
        NTransactionServer::TTransaction* transaction) override
    {
        return DoGetProxy(trunkNode->As<TImpl>(), transaction);
    }

    i64 GetStaticMasterMemoryUsage() const override
    {
        return sizeof(TImpl);
    }

    std::unique_ptr<TCypressNode> Instantiate(
        TVersionedNodeId id,
        NObjectClient::TCellTag externalCellTag) override
    {
        auto nodeHolder = std::unique_ptr<TCypressNode>(TPoolAllocator::New<TImpl>(id));
        nodeHolder->SetExternalCellTag(externalCellTag);
        nodeHolder->SetTrunkNode(nodeHolder.get());

        const auto& multicellManager = Bootstrap_->GetMulticellManager();
        if (nodeHolder->GetNativeCellTag() != multicellManager->GetCellTag()) {
            nodeHolder->SetForeign();
        }

        return nodeHolder;
    }

    std::unique_ptr<TCypressNode> Create(
        TNodeId hintId,
        const TCreateNodeContext& context) override
    {
        const auto& objectManager = Bootstrap_->GetObjectManager();
        auto id = objectManager->GenerateId(GetObjectType(), hintId);
        return DoCreate(TVersionedNodeId(id), context);
    }

    void Destroy(TCypressNode* node) override
    {
        // Run core stuff.
        DestroyCorePrologue(node);

        // Run custom stuff.
        auto* typedNode = node->As<TImpl>();
        DoDestroy(typedNode);
    }

    void RecreateAsGhost(TCypressNode* node) override
    {
        auto* typedNode = node->As<TImpl>();
        NObjectServer::TObject::RecreateAsGhost(typedNode);
    }

    void BeginCopy(
        TCypressNode* node,
        TBeginCopyContext* context) override
    {
        if (BeginCopyCore(node, context)) {
            DoBeginCopy(node->As<TImpl>(), context);
        }
    }

    TCypressNode* EndCopy(
        TEndCopyContext* context,
        ICypressNodeFactory* factory,
        TNodeId sourceNodeId) override
    {
        bool needCustomEndCopy;
        auto* trunkNode = EndCopyCore(context, factory, sourceNodeId, &needCustomEndCopy);

        if (needCustomEndCopy) {
            DoEndCopy(trunkNode->template As<TImpl>(), context, factory);
        }

        return trunkNode;
    }

    void EndCopyInplace(
        TCypressNode* trunkNode,
        TEndCopyContext* context,
        ICypressNodeFactory* factory,
        TNodeId sourceNodeId) override
    {
        EndCopyInplaceCore(trunkNode, context, factory, sourceNodeId);
        DoEndCopy(trunkNode->template As<TImpl>(), context, factory);
    }

    std::unique_ptr<TCypressNode> Branch(
        TCypressNode* originatingNode,
        NTransactionServer::TTransaction* transaction,
        const TLockRequest& lockRequest) override
    {
        // Instantiate a branched copy.
        auto originatingId = originatingNode->GetVersionedId();
        auto branchedId = TVersionedNodeId(originatingId.ObjectId, GetObjectId(transaction));
        auto branchedNodeHolder = TPoolAllocator::New<TImpl>(branchedId);
        auto* typedBranchedNode = branchedNodeHolder.get();

        // Run core stuff.
        auto* typedOriginatingNode = originatingNode->As<TImpl>();
        BranchCorePrologue(typedOriginatingNode, typedBranchedNode, transaction, lockRequest);

        // Run custom stuff.
        DoBranch(typedOriginatingNode, typedBranchedNode, lockRequest);
        DoLogBranch(typedOriginatingNode, typedBranchedNode, lockRequest);

        // Run core stuff.
        BranchCoreEpilogue(typedBranchedNode);

        return std::move(branchedNodeHolder);
    }

    void Unbranch(
        TCypressNode* originatingNode,
        TCypressNode* branchedNode) override
    {
        // Run custom stuff.
        auto* typedOriginatingNode = originatingNode->As<TImpl>();
        auto* typedBranchedNode = branchedNode->As<TImpl>();
        DoUnbranch(typedOriginatingNode, typedBranchedNode);
        DoLogUnbranch(typedOriginatingNode, typedBranchedNode);
    }

    void Merge(
        TCypressNode* originatingNode,
        TCypressNode* branchedNode) override
    {
        // Run core stuff.
        auto* typedOriginatingNode = originatingNode->As<TImpl>();
        auto* typedBranchedNode = branchedNode->As<TImpl>();
        MergeCorePrologue(typedOriginatingNode, typedBranchedNode);

        // Run custom stuff.
        DoMerge(typedOriginatingNode, typedBranchedNode);
        DoLogMerge(typedOriginatingNode, typedBranchedNode);

        // Run core stuff.
        MergeCoreEpilogue(typedOriginatingNode, typedBranchedNode);
    }

    TCypressNode* Clone(
        TCypressNode* sourceNode,
        ICypressNodeFactory* factory,
        TNodeId hintId,
        ENodeCloneMode mode,
        NSecurityServer::TAccount* account) override
    {
        // Run core prologue stuff.
        auto* clonedTrunkNode = CloneCorePrologue(
            factory,
            hintId,
            sourceNode,
            account);

        // Run custom stuff.
        auto* typedSourceNode = sourceNode->template As<TImpl>();
        auto* typedClonedTrunkNode = clonedTrunkNode->template As<TImpl>();
        DoClone(typedSourceNode, typedClonedTrunkNode, factory, mode, account);

        // Run core epilogue stuff.
        CloneCoreEpilogue(
            sourceNode,
            clonedTrunkNode,
            factory,
            mode);

        return clonedTrunkNode;
    }

    bool HasBranchedChanges(
        TCypressNode* originatingNode,
        TCypressNode* branchedNode) override
    {
        return HasBranchedChangesImpl(
            originatingNode->template As<TImpl>(),
            branchedNode->template As<TImpl>());
    }

    std::optional<std::vector<TString>> ListColumns(TCypressNode* node) const override
    {
        return DoListColumns(node->template As<TImpl>());
    }

protected:
    virtual ICypressNodeProxyPtr DoGetProxy(
        TImpl* trunkNode,
        NTransactionServer::TTransaction* transaction) = 0;

    virtual std::unique_ptr<TImpl> DoCreate(
        NCypressServer::TVersionedNodeId id,
        const TCreateNodeContext& context)
    {
        auto nodeHolder = TPoolAllocator::New<TImpl>(id);
        nodeHolder->SetExternalCellTag(context.ExternalCellTag);
        nodeHolder->SetTrunkNode(nodeHolder.get());

        const auto& multicellManager = Bootstrap_->GetMulticellManager();
        if (nodeHolder->GetNativeCellTag() != multicellManager->GetCellTag()) {
            nodeHolder->SetForeign();
            nodeHolder->SetNativeContentRevision(context.NativeContentRevision);
        }

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* user = securityManager->GetAuthenticatedUser();
        securityManager->ValidatePermission(context.Account, user, NSecurityServer::EPermission::Use);
        // Null is passed as transaction because DoCreate() always creates trunk nodes.
        securityManager->SetAccount(
            nodeHolder.get(),
            context.Account,
            nullptr /* transaction */);

        return nodeHolder;
    }

    virtual void DoDestroy(TImpl* /*node*/)
    { }

    virtual void DoBeginCopy(
        TImpl* /*node*/,
        TBeginCopyContext* /*context*/)
    { }

    virtual void DoEndCopy(
        TImpl* /*trunkNode*/,
        TEndCopyContext* /*context*/,
        ICypressNodeFactory* /*factory*/)
    { }

    virtual void DoBranch(
        const TImpl* /*originatingNode*/,
        TImpl* /*branchedNode*/,
        const TLockRequest& /*lockRequest*/)
    { }

    virtual void DoLogBranch(
        const TImpl* originatingNode,
        TImpl* branchedNode,
        const TLockRequest& lockRequest)
    {
        const auto& Logger = CypressServerLogger;
        YT_LOG_DEBUG_IF(
            IsMutationLoggingEnabled(),
            "Node branched (OriginatingNodeId: %v, BranchedNodeId: %v, Mode: %v, LockTimestamp: %llx)",
            originatingNode->GetVersionedId(),
            branchedNode->GetVersionedId(),
            lockRequest.Mode,
            lockRequest.Timestamp);
    }

    virtual void DoMerge(TImpl* /*originatingNode*/, TImpl* /*branchedNode*/)
    {
        // NB: some subclasses (namely, the journal type handler) don't
        // chain-call base class method. So it's probably not a good idea to put
        // any code here. (Hint: put it in MergeCore{Pro,Epi}logue instead.)
    }

    virtual void DoLogMerge(
        TImpl* originatingNode,
        TImpl* branchedNode)
    {
        const auto& Logger = CypressServerLogger;
        YT_LOG_DEBUG_IF(
            IsMutationLoggingEnabled(),
            "Node merged (OriginatingNodeId: %v, BranchedNodeId: %v)",
            originatingNode->GetVersionedId(),
            branchedNode->GetVersionedId());
    }

    virtual void DoUnbranch(
        TImpl* /*originatingNode*/,
        TImpl* /*branchedNode*/)
    { }

    virtual void DoLogUnbranch(
        TImpl* /*originatingNode*/,
        TImpl* /*branchedNode*/)
    { }

    virtual void DoClone(
        TImpl* /*sourceNode*/,
        TImpl* /*clonedTrunkNode*/,
        ICypressNodeFactory* /*factory*/,
        ENodeCloneMode /*mode*/,
        NSecurityServer::TAccount* /*account*/)
    { }

    virtual bool HasBranchedChangesImpl(
        TImpl* /*originatingNode*/,
        TImpl* /*branchedNode*/)
    {
        return false;
    }

    virtual std::optional<std::vector<TString>> DoListColumns(TImpl* /*node*/) const
    {
        return std::nullopt;
    }
};

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

template <class TValue>
struct TCypressScalarTypeTraits
{ };

template <>
struct TCypressScalarTypeTraits<TString>
    : public NYTree::NDetail::TScalarTypeTraits<TString>
{
    static const NObjectClient::EObjectType ObjectType;
    static const NYTree::ENodeType NodeType;
};

template <>
struct TCypressScalarTypeTraits<i64>
    : public NYTree::NDetail::TScalarTypeTraits<i64>
{
    static const NObjectClient::EObjectType ObjectType;
    static const NYTree::ENodeType NodeType;
};

template <>
struct TCypressScalarTypeTraits<ui64>
    : public NYTree::NDetail::TScalarTypeTraits<ui64>
{
    static const NObjectClient::EObjectType ObjectType;
    static const NYTree::ENodeType NodeType;
};

template <>
struct TCypressScalarTypeTraits<double>
    : public NYTree::NDetail::TScalarTypeTraits<double>
{
    static const NObjectClient::EObjectType ObjectType;
    static const NYTree::ENodeType NodeType;
};

template <>
struct TCypressScalarTypeTraits<bool>
    : public NYTree::NDetail::TScalarTypeTraits<bool>
{
    static const NObjectClient::EObjectType ObjectType;
    static const NYTree::ENodeType NodeType;
};

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

template <class TValue>
class TScalarNode
    : public TCypressNode
{
public:
    DEFINE_BYREF_RW_PROPERTY(TValue, Value)

public:
    using TCypressNode::TCypressNode;

    explicit TScalarNode(const TVersionedNodeId& id)
        : TCypressNode(id)
        , Value_()
    { }

    NYTree::ENodeType GetNodeType() const override
    {
        return NDetail::TCypressScalarTypeTraits<TValue>::NodeType;
    }

    void Save(NCellMaster::TSaveContext& context) const override
    {
        TCypressNode::Save(context);

        using NYT::Save;
        Save(context, Value_);
    }

    void Load(NCellMaster::TLoadContext& context) override
    {
        TCypressNode::Load(context);

        using NYT::Load;
        Load(context, Value_);
    }
};

////////////////////////////////////////////////////////////////////////////////

template <class TValue>
class TScalarNodeTypeHandler
    : public TCypressNodeTypeHandlerBase<TScalarNode<TValue>>
{
public:
    explicit TScalarNodeTypeHandler(NCellMaster::TBootstrap* bootstrap)
        : TBase(bootstrap)
    { }

    NObjectClient::EObjectType GetObjectType() const override
    {
        return NDetail::TCypressScalarTypeTraits<TValue>::ObjectType;
    }

    NYTree::ENodeType GetNodeType() const override
    {
        return NDetail::TCypressScalarTypeTraits<TValue>::NodeType;
    }

protected:
    using TBase = TCypressNodeTypeHandlerBase<TScalarNode<TValue>>;

    ICypressNodeProxyPtr DoGetProxy(
        TScalarNode<TValue>* trunkNode,
        NTransactionServer::TTransaction* transaction) override;

    void DoBranch(
        const TScalarNode<TValue>* originatingNode,
        TScalarNode<TValue>* branchedNode,
        const TLockRequest& lockRequest) override
    {
        TBase::DoBranch(originatingNode, branchedNode, lockRequest);

        branchedNode->Value() = originatingNode->Value();
    }

    void DoMerge(
        TScalarNode<TValue>* originatingNode,
        TScalarNode<TValue>* branchedNode) override
    {
        TBase::DoMerge(originatingNode, branchedNode);

        originatingNode->Value() = branchedNode->Value();
    }

    void DoClone(
        TScalarNode<TValue>* sourceNode,
        TScalarNode<TValue>* clonedTrunkNode,
        ICypressNodeFactory* factory,
        ENodeCloneMode mode,
        NSecurityServer::TAccount* account) override
    {
        TBase::DoClone(sourceNode, clonedTrunkNode, factory, mode, account);

        clonedTrunkNode->Value() = sourceNode->Value();
    }

    void DoBeginCopy(
        TScalarNode<TValue>* node,
        TBeginCopyContext* context) override
    {
        TBase::DoBeginCopy(node, context);

        using NYT::Save;
        Save(*context, node->Value());
    }

    void DoEndCopy(
        TScalarNode<TValue>* trunkNode,
        TEndCopyContext* context,
        ICypressNodeFactory* factory) override
    {
        TBase::DoEndCopy(trunkNode, context, factory);

        using NYT::Load;
        Load(*context, trunkNode->Value());
    }
};

////////////////////////////////////////////////////////////////////////////////

// NB: the list of inheritable attributes doesn't include the "account"
// attribute because that's already present on every Cypress node.

// NB: although both Vital and ReplicationFactor can be deduced from Media, it's
// important to be able to specify just the ReplicationFactor (or the Vital
// flag) while leaving Media null.

#define FOR_EACH_SIMPLE_INHERITABLE_ATTRIBUTE(process) \
    process(CompressionCodec, compression_codec) \
    process(ErasureCodec, erasure_codec) \
    process(EnableStripedErasure, enable_striped_erasure) \
    process(HunkErasureCodec, hunk_erasure_codec) \
    process(ReplicationFactor, replication_factor) \
    process(Vital, vital) \
    process(Atomicity, atomicity) \
    process(CommitOrdering, commit_ordering) \
    process(InMemoryMode, in_memory_mode) \
    process(OptimizeFor, optimize_for) \
    process(ProfilingMode, profiling_mode) \
    process(ProfilingTag, profiling_tag) \
    process(ChunkMergerMode, chunk_merger_mode)

#define FOR_EACH_INHERITABLE_ATTRIBUTE(process) \
    FOR_EACH_SIMPLE_INHERITABLE_ATTRIBUTE(process) \
    process(TabletCellBundle, tablet_cell_bundle) \
    process(ChaosCellBundle, chaos_cell_bundle) \
    process(PrimaryMediumIndex, primary_medium) \
    process(Media, media)

class TCompositeNodeBase
    : public TCypressNode
{
public:
    using TCypressNode::TCypressNode;

    void Save(NCellMaster::TSaveContext& context) const override;
    void Load(NCellMaster::TLoadContext& context) override;

    bool HasInheritableAttributes() const;

    // TODO(kvk1920): Rename to TAttributes.
    template <bool Transient>
    struct TGenericAttributes
    {
        template <class T>
        using TPtr = std::conditional_t<Transient, T*, NObjectServer::TStrongObjectPtr<T>>;

        TVersionedBuiltinAttribute<NCompression::ECodec> CompressionCodec;
        TVersionedBuiltinAttribute<NErasure::ECodec> ErasureCodec;
        TVersionedBuiltinAttribute<NErasure::ECodec> HunkErasureCodec;
        TVersionedBuiltinAttribute<bool> EnableStripedErasure;
        TVersionedBuiltinAttribute<int> PrimaryMediumIndex;
        TVersionedBuiltinAttribute<NChunkServer::TChunkReplication> Media;
        TVersionedBuiltinAttribute<int> ReplicationFactor;
        TVersionedBuiltinAttribute<bool> Vital;
        TVersionedBuiltinAttribute<TPtr<NTabletServer::TTabletCellBundle>> TabletCellBundle;
        TVersionedBuiltinAttribute<TPtr<NChaosServer::TChaosCellBundle>> ChaosCellBundle;
        TVersionedBuiltinAttribute<NTransactionClient::EAtomicity> Atomicity;
        TVersionedBuiltinAttribute<NTransactionClient::ECommitOrdering> CommitOrdering;
        TVersionedBuiltinAttribute<NTabletClient::EInMemoryMode> InMemoryMode;
        TVersionedBuiltinAttribute<NTableClient::EOptimizeFor> OptimizeFor;
        TVersionedBuiltinAttribute<NTabletNode::EDynamicTableProfilingMode> ProfilingMode;
        TVersionedBuiltinAttribute<TString> ProfilingTag;
        TVersionedBuiltinAttribute<bool> EnableChunkMerger;
        TVersionedBuiltinAttribute<NChunkClient::EChunkMergerMode> ChunkMergerMode;

        void Persist(const NCellMaster::TPersistenceContext& context);
        void Persist(const NCypressServer::TCopyPersistenceContext& context);

        // Are all attributes not null?
        bool AreFull() const;

        // Are all attributes null?
        bool AreEmpty() const;

        TGenericAttributes<false> ToPersistent() const requires Transient;
    };

public:
    using TTransientAttributes = TGenericAttributes</*Transient*/ true>;
    
    // TODO(kvk1920): Rename to TPersistentAttributes.
    using TAttributes = TGenericAttributes</*Transient*/ false>;

    virtual void FillTransientInheritableAttributes(TTransientAttributes* attributes) const;

    // COMPAT(kvk1920)
    virtual void FillInheritableAttributes(TAttributes* attributes) const;

#define XX(camelCaseName, snakeCaseName) \
public: \
    using T##camelCaseName = decltype(std::declval<TAttributes>().camelCaseName)::TValue; \
    std::optional<TRawVersionedBuiltinAttributeType<T##camelCaseName>> TryGet##camelCaseName() const; \
    bool Has##camelCaseName() const; \
    void Remove##camelCaseName(); \
    void Set##camelCaseName(T##camelCaseName value); \
\
private: \
    const decltype(std::declval<TAttributes>().camelCaseName)* DoTryGet##camelCaseName() const;

    FOR_EACH_INHERITABLE_ATTRIBUTE(XX)
#undef XX

    template <class U>
    friend class TCompositeNodeTypeHandler;
public:
    // TODO(kvk1920): Consider accessing Attributes_ via type handler.
    const TAttributes* FindAttributes() const;
private:
    void SetAttributes(const TAttributes* attributes);
    void CloneAttributesFrom(const TCompositeNodeBase* sourceNode);
    void MergeAttributesFrom(const TCompositeNodeBase* branchedNode);

    std::unique_ptr<TAttributes> Attributes_;
};

////////////////////////////////////////////////////////////////////////////////

// TODO(kvk1920): Rename to GatherInheritableAttributes.
// Traverse all ancestors and collect inheritable attributes.
void GatherTransientInheritableAttributes(TCypressNode* node, TCompositeNodeBase::TTransientAttributes* attributes);
// COMPAT(kvk1920): Replace with GatherTransientInheritableAttributes.
void GatherInheritableAttributes(TCypressNode* node, TCompositeNodeBase::TAttributes* attributes);

////////////////////////////////////////////////////////////////////////////////

template <class TImpl>
class TCompositeNodeTypeHandler
    : public TCypressNodeTypeHandlerBase<TImpl>
{
private:
    using TBase = TCypressNodeTypeHandlerBase<TImpl>;

public:
    using TBase::TBase;

protected:
    void DoClone(
        TImpl* sourceNode,
        TImpl* clonedTrunkNode,
        ICypressNodeFactory* factory,
        ENodeCloneMode mode,
        NSecurityServer::TAccount* account) override;

    void DoBranch(
        const TImpl* originatingNode,
        TImpl* branchedNode,
        const TLockRequest& lockRequest) override;
    void DoMerge(
        TImpl* originatingNode,
        TImpl* branchedNode) override;
    bool HasBranchedChangesImpl(
        TImpl* originatingNode,
        TImpl* branchedNode) override;

    void DoBeginCopy(
        TImpl* node,
        TBeginCopyContext* context) override;
    void DoEndCopy(
        TImpl* trunkNode,
        TEndCopyContext* context,
        ICypressNodeFactory* factory) override;
};

////////////////////////////////////////////////////////////////////////////////

//! The core of a map node. May be shared between multiple map nodes for CoW optimization.
//! Designed to be wrapped into TObjectPartCoWPtr.
class TMapNodeChildren
{
public:
    using TKeyToChild = THashMap<TString, TCypressNode*>;
    using TChildToKey = THashMap<TCypressNode*, TString>;

    TMapNodeChildren() = default;
    ~TMapNodeChildren();

    // Refcounted classes never are - and shouldn't be - copyable.
    TMapNodeChildren(const TMapNodeChildren&) = delete;
    TMapNodeChildren& operator=(const TMapNodeChildren&) = delete;

    void Save(NCellMaster::TSaveContext& context) const;
    void Load(NCellMaster::TLoadContext& context);

    void RecomputeMasterMemoryUsage();

    void Set(const NObjectServer::IObjectManagerPtr& objectManager, const TString& key, TCypressNode* child);
    void Insert(const NObjectServer::IObjectManagerPtr& objectManager, const TString& key, TCypressNode* child);
    void Remove(const NObjectServer::IObjectManagerPtr& objectManager, const TString& key, TCypressNode* child);
    bool Contains(const TString& key) const;

    const TKeyToChild& KeyToChild() const;
    const TChildToKey& ChildToKey() const;

    int GetRefCount() const noexcept;
    void Ref() noexcept;
    void Unref() noexcept;

    static void Destroy(TMapNodeChildren* children, const NObjectServer::IObjectManagerPtr& objectManager);
    static void Clear(TMapNodeChildren* children);
    static TMapNodeChildren* Copy(TMapNodeChildren* srcChildren, const NObjectServer::IObjectManagerPtr& objectManager);

    DEFINE_BYVAL_RO_PROPERTY(i64, MasterMemoryUsage);

private:
    void RefChildren(const NObjectServer::IObjectManagerPtr& objectManager);
    void UnrefChildren(const NObjectServer::IObjectManagerPtr& objectManager);

    TKeyToChild KeyToChild_;
    TChildToKey ChildToKey_;
    int RefCount_ = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TMapNode
    : public TCompositeNodeBase
{
public:
    using TKeyToChild = TMapNodeChildren::TKeyToChild;
    using TChildToKey = TMapNodeChildren::TChildToKey;

    DEFINE_BYREF_RW_PROPERTY(int, ChildCountDelta);

public:
    using TCompositeNodeBase::TCompositeNodeBase;
    ~TMapNode();

    explicit TMapNode(const TMapNode&) = delete;
    TMapNode& operator=(const TMapNode&) = delete;

    const TKeyToChild& KeyToChild() const;
    const TChildToKey& ChildToKey() const;

    // Potentially does the 'copy' part of CoW.
    TMapNodeChildren& MutableChildren(const NObjectServer::IObjectManagerPtr& objectManager);

    NYTree::ENodeType GetNodeType() const override;

    void Save(NCellMaster::TSaveContext& context) const override;
    void Load(NCellMaster::TLoadContext& context) override;

    int GetGCWeight() const override;

    NSecurityServer::TDetailedMasterMemory GetDetailedMasterMemoryUsage() const override;

    void AssignChildren(
        const NObjectServer::TObjectPartCoWPtr<TMapNodeChildren>& children,
        const NObjectServer::IObjectManagerPtr& objectManager);

private:
    NObjectServer::TObjectPartCoWPtr<TMapNodeChildren> Children_;

    template <class TImpl>
    friend class TMapNodeTypeHandlerImpl;
};

////////////////////////////////////////////////////////////////////////////////

// NB: The implementation of this template class can be found in _cpp_file,
// together with all relevant explicit instantiations.
template <class TImpl = TMapNode>
class TMapNodeTypeHandlerImpl
    : public TCompositeNodeTypeHandler<TImpl>
{
public:
    using TBase = TCompositeNodeTypeHandler<TImpl>;

    using TBase::TBase;

    NObjectClient::EObjectType GetObjectType() const override;
    NYTree::ENodeType GetNodeType() const override;

protected:
    ICypressNodeProxyPtr DoGetProxy(
        TImpl* trunkNode,
        NTransactionServer::TTransaction* transaction) override;

    void DoDestroy(TImpl* node) override;

    void DoBranch(
        const TImpl* originatingNode,
        TImpl* branchedNode,
        const TLockRequest& lockRequest) override;

    void DoMerge(
        TImpl* originatingNode,
        TImpl* branchedNode) override;

    void DoClone(
        TImpl* sourceNode,
        TImpl* clonedTrunkNode,
        ICypressNodeFactory* factory,
        ENodeCloneMode mode,
        NSecurityServer::TAccount* account) override;

    bool HasBranchedChangesImpl(
        TImpl* originatingNode,
        TImpl* branchedNode) override;

    void DoBeginCopy(
        TImpl* node,
        TBeginCopyContext* context) override;
    void DoEndCopy(
        TImpl* trunkNode,
        TEndCopyContext* context,
        ICypressNodeFactory* factory) override;
};

using TMapNodeTypeHandler = TMapNodeTypeHandlerImpl<TMapNode>;

////////////////////////////////////////////////////////////////////////////////

class TListNode
    : public TCompositeNodeBase
{
private:
    using TBase = TCompositeNodeBase;

public:
    using TIndexToChild = std::vector<TCypressNode*>;
    using TChildToIndex = THashMap<TCypressNode*, int>;

    DEFINE_BYREF_RW_PROPERTY(TIndexToChild, IndexToChild);
    DEFINE_BYREF_RW_PROPERTY(TChildToIndex, ChildToIndex);

public:
    using TBase::TBase;

    NYTree::ENodeType GetNodeType() const override;

    void Save(NCellMaster::TSaveContext& context) const override;
    void Load(NCellMaster::TLoadContext& context) override;

    int GetGCWeight() const override;
};

////////////////////////////////////////////////////////////////////////////////

class TListNodeTypeHandler
    : public TCompositeNodeTypeHandler<TListNode>
{
private:
    using TBase = TCompositeNodeTypeHandler<TListNode>;

public:
    using TBase::TBase;

    NObjectClient::EObjectType GetObjectType() const override;
    NYTree::ENodeType GetNodeType() const override;

private:
    ICypressNodeProxyPtr DoGetProxy(
        TListNode* trunkNode,
        NTransactionServer::TTransaction* transaction) override;

    void DoDestroy(TListNode* node) override;

    void DoBranch(
        const TListNode* originatingNode,
        TListNode* branchedNode,
        const TLockRequest& lockRequest) override;
    void DoMerge(
        TListNode* originatingNode,
        TListNode* branchedNode) override;

    void DoClone(
        TListNode* sourceNode,
        TListNode* clonedTrunkNode,
        ICypressNodeFactory* factory,
        ENodeCloneMode mode,
        NSecurityServer::TAccount* account) override;

    bool HasBranchedChangesImpl(
        TListNode* originatingNode,
        TListNode* branchedNode) override;

    void DoBeginCopy(
        TListNode* node,
        TBeginCopyContext* context) override;
    void DoEndCopy(
        TListNode* trunkNode,
        TEndCopyContext* context,
        ICypressNodeFactory* factory) override;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressServer

#define NODE_DETAIL_INL_H_
#include "node_detail-inl.h"
#undef NODE_DETAIL_INL_H_
