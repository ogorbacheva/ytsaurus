#include "stdafx.h"
#include "node_tracker.h"
#include "config.h"
#include "node.h"
#include "node_proxy.h"
#include "rack.h"
#include "rack_proxy.h"
#include "private.h"
#include "cypress_integration.h"

#include <core/misc/id_generator.h>
#include <core/misc/address.h>

#include <core/ytree/convert.h>
#include <core/ytree/ypath_client.h>

#include <core/ypath/token.h>

#include <core/concurrency/scheduler.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>
#include <ytlib/cypress_client/rpc_helpers.h>

#include <ytlib/object_client/helpers.h>

#include <ytlib/node_tracker_client/helpers.h>

#include <server/chunk_server/job.h>

#include <server/node_tracker_server/node_tracker.pb.h>

#include <server/cypress_server/cypress_manager.h>
#include <server/cypress_server/node_proxy.h>

#include <server/transaction_server/transaction_manager.h>
#include <server/transaction_server/transaction.h>

#include <server/object_server/object_manager.h>
#include <server/object_server/attribute_set.h>
#include <server/object_server/type_handler_detail.h>

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/hydra_facade.h>
#include <server/cell_master/serialize.h>

#include <deque>

namespace NYT {
namespace NNodeTrackerServer {

using namespace NConcurrency;
using namespace NYTree;
using namespace NYPath;
using namespace NNodeTrackerClient;
using namespace NNodeTrackerClient::NProto;
using namespace NHydra;
using namespace NHive;
using namespace NObjectClient;
using namespace NCypressClient;
using namespace NCypressServer;
using namespace NNodeTrackerServer::NProto;
using namespace NTransactionServer;
using namespace NSecurityServer;
using namespace NObjectServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = NodeTrackerServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TNodeTracker::TClusterNodeTypeHandler
    : public TObjectTypeHandlerWithMapBase<TNode>
{
public:
    explicit TClusterNodeTypeHandler(TImpl* owner);

    virtual EObjectReplicationFlags GetReplicationFlags() const override
    {
        return EObjectReplicationFlags::None;
    }

    virtual EObjectType GetType() const override
    {
        return EObjectType::ClusterNode;
    }

    virtual TNullable<TTypeCreationOptions> GetCreationOptions() const override
    {
        return Null;
    }

private:
    TImpl* const Owner_;

    virtual Stroka DoGetName(TNode* node) override
    {
        return Format("node %v", node->GetDefaultAddress());
    }

    virtual IObjectProxyPtr DoGetProxy(TNode* node, TTransaction* transaction) override;

    virtual void DoZombifyObject(TNode* node) override;

};

////////////////////////////////////////////////////////////////////////////////

class TNodeTracker::TRackTypeHandler
    : public TObjectTypeHandlerWithMapBase<TRack>
{
public:
    explicit TRackTypeHandler(TImpl* owner);

    virtual EObjectReplicationFlags GetReplicationFlags() const override
    {
        return EObjectReplicationFlags::All;
    }

    virtual EObjectType GetType() const override
    {
        return EObjectType::Rack;
    }

    virtual TNullable<TTypeCreationOptions> GetCreationOptions() const override
    {
        return TTypeCreationOptions(
            EObjectTransactionMode::Forbidden,
            EObjectAccountMode::Forbidden);
    }

    virtual TObjectBase* CreateObject(
        const TObjectId& hintId,
        TTransaction* transaction,
        TAccount* account,
        IAttributeDictionary* attributes,
        TReqCreateObject* request,
        TRspCreateObject* response) override;

private:
    TImpl* const Owner_;

    virtual Stroka DoGetName(TRack* rack) override
    {
        return Format("rack %Qv", rack->GetName());
    }

    virtual IObjectProxyPtr DoGetProxy(TRack* rack, TTransaction* transaction) override;

    virtual void DoDestroyObject(TRack* rack) override;

};

////////////////////////////////////////////////////////////////////////////////

class TNodeTracker::TImpl
    : public TMasterAutomatonPart
{
public:
    TImpl(
        TNodeTrackerConfigPtr config,
        TBootstrap* bootstrap)
        : TMasterAutomatonPart(bootstrap)
        , Config_(config)
    {
        RegisterMethod(BIND(&TImpl::HydraRegisterNode, Unretained(this)));
        RegisterMethod(BIND(&TImpl::HydraUnregisterNode, Unretained(this)));
        RegisterMethod(BIND(&TImpl::HydraDisposeNode, Unretained(this)));
        RegisterMethod(BIND(&TImpl::HydraFullHeartbeat, Unretained(this), nullptr));
        RegisterMethod(BIND(&TImpl::HydraIncrementalHeartbeat, Unretained(this), nullptr, nullptr));

        RegisterLoader(
            "NodeTracker.Keys",
            BIND(&TImpl::LoadKeys, Unretained(this)));
        RegisterLoader(
            "NodeTracker.Values",
            BIND(&TImpl::LoadValues, Unretained(this)));

        RegisterSaver(
            ESyncSerializationPriority::Keys,
            "NodeTracker.Keys",
            BIND(&TImpl::SaveKeys, Unretained(this)));
        RegisterSaver(
            ESyncSerializationPriority::Values,
            "NodeTracker.Values",
            BIND(&TImpl::SaveValues, Unretained(this)));
    }

    void Initialize()
    {
        auto transactionManager = Bootstrap_->GetTransactionManager();
        transactionManager->SubscribeTransactionCommitted(BIND(&TImpl::OnTransactionFinished, MakeWeak(this)));
        transactionManager->SubscribeTransactionAborted(BIND(&TImpl::OnTransactionFinished, MakeWeak(this)));

        auto objectManager = Bootstrap_->GetObjectManager();
        objectManager->RegisterHandler(New<TClusterNodeTypeHandler>(this));
        objectManager->RegisterHandler(New<TRackTypeHandler>(this));
    }

    bool TryAcquireNodeRegistrationSemaphore()
    {
        if (PendingRegisterNodeMutationCount_ + RegisteredNodeCount_ >= Config_->MaxConcurrentNodeRegistrations) {
            return false;
        }
        ++PendingRegisterNodeMutationCount_;
        return true;
    }

    TMutationPtr CreateRegisterNodeMutation(
        const TReqRegisterNode& request)
    {
        return CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            request);
    }

    TMutationPtr CreateUnregisterNodeMutation(
        const TReqUnregisterNode& request)
    {
        return CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            request);
    }

    TMutationPtr CreateDisposeNodeMutation(
        const TReqDisposeNode& request)
    {
        return CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            request);
    }

    TMutationPtr CreateFullHeartbeatMutation(
        TCtxFullHeartbeatPtr context)
    {
        return CreateMutation(Bootstrap_->GetHydraFacade()->GetHydraManager())
            ->SetRequestData(context->GetRequestBody(), context->Request().GetTypeName())
            ->SetAction(BIND(
                &TImpl::HydraFullHeartbeat,
                MakeStrong(this),
                context,
                ConstRef(context->Request())));
   }

    TMutationPtr CreateIncrementalHeartbeatMutation(
        TCtxIncrementalHeartbeatPtr context)
    {
        return CreateMutation(Bootstrap_->GetHydraFacade()->GetHydraManager())
            ->SetRequestData(context->GetRequestBody(), context->Request().GetTypeName())
            ->SetAction(BIND(
                &TImpl::HydraIncrementalHeartbeat,
                MakeStrong(this),
                context,
                &context->Response(),
                ConstRef(context->Request())));
    }


    DECLARE_ENTITY_MAP_ACCESSORS(Node, TNode, TObjectId);
    DECLARE_ENTITY_MAP_ACCESSORS(Rack, TRack, TRackId);

    DEFINE_SIGNAL(void(TNode* node), NodeRegistered);
    DEFINE_SIGNAL(void(TNode* node), NodeUnregistered);
    DEFINE_SIGNAL(void(TNode* node), NodeDisposed);
    DEFINE_SIGNAL(void(TNode* node), NodeBanChanged);
    DEFINE_SIGNAL(void(TNode* node), NodeDecommissionChanged);
    DEFINE_SIGNAL(void(TNode* node), NodeRackChanged);
    DEFINE_SIGNAL(void(TNode* node, const TReqFullHeartbeat& request), FullHeartbeat);
    DEFINE_SIGNAL(void(TNode* node, const TReqIncrementalHeartbeat& request, TRspIncrementalHeartbeat* response), IncrementalHeartbeat);
    DEFINE_SIGNAL(void(std::vector<TCellDescriptor>*), PopulateCellDescriptors);


    void DestroyNode(TNode* node)
    {
        auto nodeMapProxy = GetNodeMap();
        auto nodeNodeProxy = nodeMapProxy->FindChild(ToString(node->GetDefaultAddress()));
        auto cypressNodeNodeProxy = dynamic_cast<ICypressNodeProxy*>(nodeNodeProxy.Get());

        auto cypressManager = Bootstrap_->GetCypressManager();
        cypressManager->AbortSubtreeTransactions(cypressNodeNodeProxy->GetTrunkNode(), nullptr);

        nodeMapProxy->RemoveChild(nodeNodeProxy);

        RemoveFromAddressMaps(node);
    }

    TNode* FindNode(TNodeId id)
    {
        return FindNode(ObjectIdFromNodeId(id));
    }

    TNode* GetNode(TNodeId id)
    {
        return GetNode(ObjectIdFromNodeId(id));
    }

    TNode* GetNodeOrThrow(TNodeId id)
    {
        auto* node = FindNode(id);
        if (!node) {
            THROW_ERROR_EXCEPTION(
                NNodeTrackerClient::EErrorCode::NoSuchNode,
                "Invalid or expired node id %v",
                id);
        }
        return node;
    }

    TNode* FindNodeByAddress(const Stroka& address)
    {
        auto it = AddressToNodeMap_.find(address);
        return it == AddressToNodeMap_.end() ? nullptr : it->second;
    }

    TNode* GetNodeByAddress(const Stroka& address)
    {
        auto* node = FindNodeByAddress(address);
        YCHECK(node);
        return node;
    }

    TNode* GetNodeByAddressOrThrow(const Stroka& address)
    {
        auto* node = FindNodeByAddress(address);
        if (!node) {
            THROW_ERROR_EXCEPTION("No such cluster node %v", address);
        }
        return node;
    }

    TNode* FindNodeByHostName(const Stroka& hostName)
    {
        auto it = HostNameToNodeMap_.find(hostName);
        return it == HostNameToNodeMap_.end() ? nullptr : it->second;
    }

    std::vector<TNode*> GetRackNodes(const TRack* rack)
    {
        std::vector<TNode*> result;
        for (const auto& pair : NodeMap_) {
            auto* node = pair.second;
            if (!IsObjectAlive(node))
                continue;
            if (node->GetRack() == rack) {
                result.push_back(node);
            }
        }
        return result;
    }


    void SetNodeBanned(TNode* node, bool value)
    {
        if (node->GetBanned() != value) {
            node->SetBanned(value);
            if (value) {
                LOG_INFO_UNLESS(IsRecovery(), "Node banned (NodeId: %v, Address: %v)",
                    node->GetId(),
                    node->GetDefaultAddress());
                if (node->GetState() == ENodeState::Online || node->GetState() == ENodeState::Registered) {
                    UnregisterNode(node, true);
                }
            } else {
                LOG_INFO_UNLESS(IsRecovery(), "Node is no longer banned (NodeId: %v, Address: %v)",
                    node->GetId(),
                    node->GetDefaultAddress());
            }
            NodeBanChanged_.Fire(node);
        }
    }

    void SetNodeDecommissioned(TNode* node, bool value)
    {
        if (node->GetDecommissioned() != value) {
            node->SetDecommissioned(value);
            if (value) {
                LOG_INFO_UNLESS(IsRecovery(), "Node decommissioned (NodeId: %v, Address: %v)",
                    node->GetId(),
                    node->GetDefaultAddress());
            } else {
                LOG_INFO_UNLESS(IsRecovery(), "Node is no longer decommissioned (NodeId: %v, Address: %v)",
                    node->GetId(),
                    node->GetDefaultAddress());
            }
            NodeDecommissionChanged_.Fire(node);
        }
    }

    void SetNodeRack(TNode* node, TRack* rack)
    {
        if (node->GetRack() != rack) {
            node->SetRack(rack);
            LOG_INFO_UNLESS(IsRecovery(), "Node rack changed (NodeId: %v, Address: %v, Rack: %v)",
                node->GetId(),
                node->GetDefaultAddress(),
                rack ? MakeNullable(rack->GetName()) : Null);
            NodeRackChanged_.Fire(node);
        }
    }


    TRack* CreateRack(const Stroka& name, const TObjectId& hintId)
    {
        if (name.empty()) {
            THROW_ERROR_EXCEPTION("Rack name cannot be empty");
        }

        if (FindRackByName(name)) {
            THROW_ERROR_EXCEPTION(
                NYTree::EErrorCode::AlreadyExists,
                "Rack %Qv already exists",
                name);
        }

        if (RackMap_.GetSize() >= MaxRackCount) {
            THROW_ERROR_EXCEPTION("Rack count limit %v is reached",
                MaxRackCount);
        }

        auto objectManager = Bootstrap_->GetObjectManager();
        auto id = objectManager->GenerateId(EObjectType::Rack, hintId);

        auto rackHolder = std::make_unique<TRack>(id);
        rackHolder->SetName(name);
        rackHolder->SetIndex(AllocateRackIndex());

        auto* rack = RackMap_.Insert(id, std::move(rackHolder));
        YCHECK(NameToRackMap_.insert(std::make_pair(name, rack)).second);

        // Make the fake reference.
        YCHECK(rack->RefObject() == 1);

        return rack;
    }

    void DestroyRack(TRack* rack)
    {
        // Unbind nodes from this rack.
        for (auto* node : GetRackNodes(rack)) {
            SetNodeRack(node, nullptr);
        }

        // Remove rack from maps.
        YCHECK(NameToRackMap_.erase(rack->GetName()) == 1);
        FreeRackIndex(rack->GetIndex());
    }

    void RenameRack(TRack* rack, const Stroka& newName)
    {
        if (rack->GetName() == newName)
            return;

        if (FindRackByName(newName)) {
            THROW_ERROR_EXCEPTION(
                NYTree::EErrorCode::AlreadyExists,
                "Rack %Qv already exists",
                newName);
        }

        // Update name.
        YCHECK(NameToRackMap_.erase(rack->GetName()) == 1);
        YCHECK(NameToRackMap_.insert(std::make_pair(newName, rack)).second);
        rack->SetName(newName);
    }

    TRack* FindRackByName(const Stroka& name)
    {
        auto it = NameToRackMap_.find(name);
        return it == NameToRackMap_.end() ? nullptr : it->second;
    }

    TRack* GetRackByNameOrThrow(const Stroka& name)
    {
        auto* rack = FindRackByName(name);
        if (!rack) {
            THROW_ERROR_EXCEPTION(
                NNodeTrackerClient::EErrorCode::NoSuchRack,
                "No such rack %Qv",
                name);
        }
        return rack;
    }


    TTotalNodeStatistics GetTotalNodeStatistics()
    {
        TTotalNodeStatistics result;
        for (const auto& pair : NodeMap_) {
            const auto* node = pair.second;
            const auto& statistics = node->Statistics();
            result.AvailableSpace += statistics.total_available_space();
            result.UsedSpace += statistics.total_used_space();
            result.ChunkCount += statistics.total_stored_chunk_count();
            result.OnlineNodeCount++;
        }
        return result;
    }

    int GetRegisteredNodeCount()
    {
        return RegisteredNodeCount_;
    }

    int GetOnlineNodeCount()
    {
        return OnlineNodeCount_;
    }


    std::vector<TCellDescriptor> GetCellDescriptors()
    {
        std::vector<TCellDescriptor> result;
        PopulateCellDescriptors_.Fire(&result);
        return result;
    }

private:
    friend class TClusterNodeTypeHandler;
    friend class TRackTypeHandler;

    const TNodeTrackerConfigPtr Config_;

    NProfiling::TProfiler Profiler = NodeTrackerServerProfiler;

    TIdGenerator NodeIdGenerator_;
    NHydra::TEntityMap<TObjectId, TNode> NodeMap_;
    NHydra::TEntityMap<TRackId, TRack> RackMap_;

    int OnlineNodeCount_ = 0;
    int RegisteredNodeCount_ = 0;

    TRackSet UsedRackIndexes_ = 0;

    yhash_map<Stroka, TNode*> AddressToNodeMap_;
    yhash_multimap<Stroka, TNode*> HostNameToNodeMap_;
    yhash_map<TTransaction*, TNode*> TransactionToNodeMap_;
    yhash_map<Stroka, TRack*> NameToRackMap_;


    int PendingRegisterNodeMutationCount_ = 0;

    std::deque<TNode*> NodeDisposalQueue_;
    int PendingDisposeNodeMutationCount_ = 0;


    TNodeId GenerateNodeId()
    {
        TNodeId id;
        while (true) {
            id = NodeIdGenerator_.Next();
            // Beware of sentinels!
            if (id == InvalidNodeId) {
                // Just wait for the next attempt.
            } else if (id > MaxNodeId) {
                NodeIdGenerator_.Reset();
            } else {
                break;
            }
        }
        return id;
    }

    TObjectId ObjectIdFromNodeId(TNodeId nodeId)
    {
        return NNodeTrackerClient::ObjectIdFromNodeId(nodeId, Bootstrap_->GetHydraFacade()->GetPrimaryCellTag());
    }


    static TYPath GetNodePath(const Stroka& address)
    {
        return "//sys/nodes/" + ToYPathLiteral(address);
    }

    static TYPath GetNodePath(TNode* node)
    {
        return GetNodePath(node->GetDefaultAddress());
    }

    IMapNodePtr GetNodeMap()
    {
        auto cypressManager = Bootstrap_->GetCypressManager();
        auto resolver = cypressManager->CreateResolver();
        auto node = resolver->ResolvePath("//sys/nodes");
        return node->AsMap();
    }


    TRspRegisterNode HydraRegisterNode(const TReqRegisterNode& request)
    {
        auto addresses = FromProto<TAddressMap>(request.addresses());
        const auto& address = GetDefaultAddress(addresses);
        const auto& statistics = request.statistics();

        // Kick-out any previous incarnation.
        auto* existingNode = FindNodeByAddress(address);
        if (existingNode) {
            if (existingNode->GetBanned()) {
                THROW_ERROR_EXCEPTION("Node %v is banned", address);
            }
            LOG_INFO_UNLESS(IsRecovery(), "Node kicked out due to address conflict (Address: %v, ExistingNodeId: %v)",
                address,
                existingNode->GetId());
            UnregisterNode(existingNode, false);
            DisposeNode(existingNode);
            RemoveFromAddressMaps(existingNode);
        }

        if (IsLeader()) {
            YCHECK(--PendingRegisterNodeMutationCount_ >= 0);
        }

        auto* newNode = RegisterNode(addresses, statistics);

        if (existingNode) {
            SetNodeBanned(newNode, existingNode->GetBanned());
            SetNodeDecommissioned(newNode, existingNode->GetDecommissioned());
            const auto* newAttributes = newNode->GetAttributes();
            auto* existingAttributes = existingNode->GetMutableAttributes();
            if (newAttributes) {
                existingAttributes->Attributes() = newAttributes->Attributes();
            } else {
                existingAttributes->Attributes().clear();
            }

            NodeMap_.Remove(ObjectIdFromNodeId(existingNode->GetId()));
        }

        TRspRegisterNode response;
        response.set_node_id(newNode->GetId());
        return response;
    }

    void HydraUnregisterNode(const TReqUnregisterNode& request)
    {
        auto nodeId = request.node_id();

        auto* node = FindNode(nodeId);
        if (!node)
            return;
        if (node->GetState() != ENodeState::Registered && node->GetState() != ENodeState::Online)
            return;

        UnregisterNode(node, true);
    }

    void HydraDisposeNode(const TReqDisposeNode& request)
    {
        auto nodeId = request.node_id();

        auto* node = FindNode(nodeId);
        if (!node)
            return;
        if (node->GetState() != ENodeState::Unregistered)
            return;

        if (IsLeader()) {
            YCHECK(--PendingDisposeNodeMutationCount_ >= 0);
        }

        DisposeNode(node);
    }

    void HydraFullHeartbeat(
        TCtxFullHeartbeatPtr context,
        const TReqFullHeartbeat& request)
    {
        auto nodeId = request.node_id();
        const auto& statistics = request.statistics();

        auto* node = FindNode(nodeId);
        if (!node)
            return;
        if (node->GetState() != ENodeState::Registered)
            return;

        PROFILE_TIMING ("/full_heartbeat_time") {
            LOG_DEBUG_UNLESS(IsRecovery(), "Processing full heartbeat (NodeId: %v, Address: %v, State: %v, %v)",
                nodeId,
                node->GetDefaultAddress(),
                node->GetState(),
                statistics);

            UpdateNodeCounters(node, -1);
            node->SetState(ENodeState::Online);
            UpdateNodeCounters(node, +1);

            node->Statistics() = statistics;

            RenewNodeLease(node);

            LOG_INFO_UNLESS(IsRecovery(), "Node online (NodeId: %v, Address: %v)",
                nodeId,
                node->GetDefaultAddress());

            FullHeartbeat_.Fire(node, request);
        }
    }

    void HydraIncrementalHeartbeat(
        TCtxIncrementalHeartbeatPtr context,
        TRspIncrementalHeartbeat* response,
        const TReqIncrementalHeartbeat& request)
    {
        auto nodeId = request.node_id();
        const auto& statistics = request.statistics();

        auto* node = FindNode(nodeId);
        if (!node)
            return;
        if (node->GetState() != ENodeState::Online)
            return;

        PROFILE_TIMING ("/incremental_heartbeat_time") {
            LOG_DEBUG_UNLESS(IsRecovery(), "Processing incremental heartbeat (NodeId: %v, Address: %v, State: %v, %v)",
                nodeId,
                node->GetDefaultAddress(),
                node->GetState(),
                statistics);

            node->Statistics() = statistics;
            node->Alerts() = FromProto<TError>(request.alerts());

            RenewNodeLease(node);

            if (response && node->GetRack()) {
                response->set_rack(node->GetRack()->GetName());
            }
            
            IncrementalHeartbeat_.Fire(node, request, response);
        }
    }


    void SaveKeys(NCellMaster::TSaveContext& context) const
    {
        NodeMap_.SaveKeys(context);
        RackMap_.SaveKeys(context);
    }

    void SaveValues(NCellMaster::TSaveContext& context) const
    {
        Save(context, NodeIdGenerator_);
        NodeMap_.SaveValues(context);
        RackMap_.SaveValues(context);
    }

    void LoadKeys(NCellMaster::TLoadContext& context)
    {
        NodeMap_.LoadKeys(context);
        // COMPAT(babenko)
        if (context.GetVersion() >= 103) {
            RackMap_.LoadKeys(context);
        }
    }

    void LoadValues(NCellMaster::TLoadContext& context)
    {
        Load(context, NodeIdGenerator_);
        NodeMap_.LoadValues(context);
        // COMPAT(babenko)
        if (context.GetVersion() >= 103) {
            RackMap_.LoadValues(context);
        }
    }

    virtual void Clear() override
    {
        TMasterAutomatonPart::Clear();

        NodeIdGenerator_.Reset();
        NodeMap_.Clear();
        RackMap_.Clear();

        AddressToNodeMap_.clear();
        HostNameToNodeMap_.clear();
        TransactionToNodeMap_.clear();

        NameToRackMap_.clear();

        OnlineNodeCount_ = 0;
        RegisteredNodeCount_ = 0;
    }

    virtual void OnAfterSnapshotLoaded() override
    {
        TMasterAutomatonPart::OnAfterSnapshotLoaded();

        AddressToNodeMap_.clear();
        HostNameToNodeMap_.clear();
        TransactionToNodeMap_.clear();

        OnlineNodeCount_ = 0;
        RegisteredNodeCount_ = 0;

        for (const auto& pair : NodeMap_) {
            auto* node = pair.second;

            InsertToAddressMaps(node);
            UpdateNodeCounters(node, +1);

            if (node->GetTransaction()) {
                RegisterLeaseTransaction(node);
            } else {
                UnregisterNode(node, true);
            }
        }

        UsedRackIndexes_ = 0;
        for (const auto& pair : RackMap_) {
            auto* rack = pair.second;

            YCHECK(NameToRackMap_.insert(std::make_pair(rack->GetName(), rack)).second);

            auto rackIndexMask = rack->GetIndexMask();
            YCHECK(!(UsedRackIndexes_ & rackIndexMask));
            UsedRackIndexes_ |= rackIndexMask;
        }
    }

    virtual void OnRecoveryStarted() override
    {
        TMasterAutomatonPart::OnRecoveryStarted();

        Profiler.SetEnabled(false);

        // Reset runtime info.
        for (const auto& pair : NodeMap_) {
            auto* node = pair.second;
            node->ResetSessionHints();
            node->ClearChunkRemovalQueue();
            node->ClearChunkReplicationQueues();
            node->ClearChunkSealQueue();
        }
    }

    virtual void OnRecoveryComplete() override
    {
        TMasterAutomatonPart::OnRecoveryComplete();

        Profiler.SetEnabled(true);
    }

    virtual void OnLeaderActive() override
    {
        TMasterAutomatonPart::OnLeaderActive();

        PendingRegisterNodeMutationCount_ = 0;

        NodeDisposalQueue_.clear();
        PendingDisposeNodeMutationCount_ = 0;

        for (const auto& pair : NodeMap_) {
            auto* node = pair.second;
            if (node->GetState() == ENodeState::Unregistered) {
                NodeDisposalQueue_.push_back(node);
            }
        }

        MaybePostDisposeNodeMutations();
    }


    void UpdateNodeCounters(TNode* node, int delta)
    {
        switch (node->GetState()) {
            case ENodeState::Registered:
                RegisteredNodeCount_ += delta;
                break;
            case ENodeState::Online:
                OnlineNodeCount_ += delta;
                break;
            default:
                break;
        }
    }


    void RegisterLeaseTransaction(TNode* node)
    {
        auto* transaction = node->GetTransaction();
        YCHECK(transaction);
        YCHECK(TransactionToNodeMap_.insert(std::make_pair(transaction, node)).second);
    }

    TTransaction* UnregisterLeaseTransaction(TNode* node)
    {
        auto* transaction = node->GetTransaction();
        if (transaction) {
            YCHECK(TransactionToNodeMap_.erase(transaction) == 1);
        }
        node->SetTransaction(nullptr);
        return transaction;
    }

    void RenewNodeLease(TNode* node)
    {
        auto* transaction = node->GetTransaction();
        if (!transaction)
            return;

        auto timeout = GetNodeLeaseTimeout(node);
        transaction->SetTimeout(timeout);

        try {
            auto objectManager = Bootstrap_->GetObjectManager();
            auto rootService = objectManager->GetRootService();
            auto nodePath = GetNodePath(node);
            const auto* mutationContext = GetCurrentMutationContext();
            auto mutationTimestamp = mutationContext->GetTimestamp();
            SyncYPathSet(rootService, nodePath + "/@last_seen_time", ConvertToYsonString(mutationTimestamp));
        } catch (const std::exception& ex) {
            LOG_ERROR_UNLESS(IsRecovery(), ex, "Error updating node properties in Cypress");
        }

        if (IsLeader()) {
            auto transactionManager = Bootstrap_->GetTransactionManager();
            transactionManager->PingTransaction(transaction);
        }
    }

    TDuration GetNodeLeaseTimeout(TNode* node)
    {
        switch (node->GetState()) {
            case ENodeState::Registered:
                return Config_->RegisteredNodeTimeout;
            case ENodeState::Online:
                return Config_->OnlineNodeTimeout;
            default:
                YUNREACHABLE();
        }
    }

    void OnTransactionFinished(TTransaction* transaction)
    {
        auto it = TransactionToNodeMap_.find(transaction);
        if (it == TransactionToNodeMap_.end())
            return;

        auto* node = it->second;
        LOG_INFO_UNLESS(IsRecovery(), "Node lease expired (NodeId: %v, Address: %v)",
            node->GetId(),
            node->GetDefaultAddress());

        UnregisterNode(node, true);
    }


    TNode* RegisterNode(
        const TAddressMap& addresses,
        const TNodeStatistics& statistics)
    {
        PROFILE_TIMING ("/node_register_time") {
            const auto& address = GetDefaultAddress(addresses);
            auto objectId = ObjectIdFromNodeId(GenerateNodeId());

            const auto* mutationContext = GetCurrentMutationContext();

            auto nodeHolder = std::make_unique<TNode>(
                objectId,
                addresses,
                mutationContext->GetTimestamp());

            auto* node = NodeMap_.Insert(objectId, std::move(nodeHolder));

            // Make the fake reference.
            YCHECK(node->RefObject() == 1);

            node->SetState(ENodeState::Registered);
            node->Statistics() = statistics;

            InsertToAddressMaps(node);
            UpdateNodeCounters(node, +1);

            auto transactionManager = Bootstrap_->GetTransactionManager();
            auto objectManager = Bootstrap_->GetObjectManager();
            auto rootService = objectManager->GetRootService();
            auto nodePath = GetNodePath(node);

            // Create lease transaction.
            TTransaction* transaction;
            {
                auto timeout = GetNodeLeaseTimeout(node);
                transaction = transactionManager->StartTransaction(nullptr, timeout);
                node->SetTransaction(transaction);
                RegisterLeaseTransaction(node);
            }

            try {
                // Set attributes.
                {
                    auto attributes = CreateEphemeralAttributes();
                    attributes->Set("title", Format("Lease for node %v", node->GetDefaultAddress()));
                    objectManager->FillAttributes(transaction, *attributes);
                }

                // Create Cypress node.
                {
                    auto req = TCypressYPathProxy::Create(nodePath);
                    req->set_type(static_cast<int>(EObjectType::ClusterNodeNode));
                    req->set_ignore_existing(true);

                    SyncExecuteVerb(rootService, req);
                }

                // Create "orchid" child.
                {
                    auto req = TCypressYPathProxy::Create(nodePath + "/orchid");
                    req->set_type(static_cast<int>(EObjectType::Orchid));
                    req->set_ignore_existing(true);

                    auto attributes = CreateEphemeralAttributes();
                    attributes->Set("remote_address", GetInterconnectAddress(addresses));
                    ToProto(req->mutable_node_attributes(), *attributes);

                    SyncExecuteVerb(rootService, req);
                }
            } catch (const std::exception& ex) {
                LOG_ERROR_UNLESS(IsRecovery(), ex, "Error registering node in Cypress");
            }

            // Make the initial lease renewal (and also set "last_seen_time" attribute).
            RenewNodeLease(node);

            LOG_INFO_UNLESS(IsRecovery(), "Node registered (NodeId: %v, Address: %v, %v)",
                node->GetId(),
                address,
                statistics);

            NodeRegistered_.Fire(node);

            return node;
        }
    }

    void UnregisterNode(TNode* node, bool scheduleDisposal)
    {
        PROFILE_TIMING ("/node_unregister_time") {
            auto* transaction = UnregisterLeaseTransaction(node);
            if (transaction && transaction->GetPersistentState() == ETransactionState::Active) {
                auto transactionManager = Bootstrap_->GetTransactionManager();
                // NB: This will trigger OnTransactionFinished, however we've already evicted the
                // lease so the latter call is no-op.
                transactionManager->AbortTransaction(transaction, false);
            }

            UpdateNodeCounters(node, -1);
            node->SetState(ENodeState::Unregistered);
            NodeUnregistered_.Fire(node);

            if (scheduleDisposal && IsLeader()) {
                NodeDisposalQueue_.push_back(node);
                MaybePostDisposeNodeMutations();
            }

            LOG_INFO_UNLESS(IsRecovery(), "Node unregistered (NodeId: %v, Address: %v)",
                node->GetId(),
                node->GetDefaultAddress());
        }
    }

    void DisposeNode(TNode* node)
    {
        PROFILE_TIMING ("/node_dispose_time") {
            node->SetState(ENodeState::Offline);
            NodeDisposed_.Fire(node);

            LOG_INFO_UNLESS(IsRecovery(), "Node offline (NodeId: %v, Address: %v)",
                node->GetId(),
                node->GetDefaultAddress());

            if (IsLeader()) {
                MaybePostDisposeNodeMutations();
            }
        }
    }


    void InsertToAddressMaps(TNode* node)
    {
        const auto& address = node->GetDefaultAddress();
        AddressToNodeMap_.insert(std::make_pair(address, node));
        HostNameToNodeMap_.insert(std::make_pair(Stroka(GetServiceHostName(address)), node));
    }

    void RemoveFromAddressMaps(TNode* node)
    {
        const auto& address = node->GetDefaultAddress();
        YCHECK(AddressToNodeMap_.erase(address) == 1);
        {
            auto hostNameRange = HostNameToNodeMap_.equal_range(Stroka(GetServiceHostName(address)));
            for (auto it = hostNameRange.first; it != hostNameRange.second; ++it) {
                if (it->second == node) {
                    HostNameToNodeMap_.erase(it);
                    break;
                }
            }
        }

    }


    void PostUnregisterNodeMutation(TNode* node)
    {
        TReqUnregisterNode request;
        request.set_node_id(node->GetId());

        auto mutation = CreateUnregisterNodeMutation(request);
        BIND(&TMutation::Commit, mutation)
            .AsyncVia(Bootstrap_->GetHydraFacade()->GetEpochAutomatonInvoker())
            .Run()
            .Subscribe(BIND([] (const TErrorOr<TMutationResponse>& error) {
                if (!error.IsOK()) {
                    LOG_ERROR(error, "Error committing node unregistration mutation");
                }
            }));
    }

    void MaybePostDisposeNodeMutations()
    {
        while (
            !NodeDisposalQueue_.empty() &&
            PendingDisposeNodeMutationCount_ < Config_->MaxConcurrentNodeUnregistrations)
        {
            const auto* node = NodeDisposalQueue_.front();
            NodeDisposalQueue_.pop_front();

            TReqDisposeNode request;
            request.set_node_id(node->GetId());

            ++PendingDisposeNodeMutationCount_;

            auto mutation = CreateDisposeNodeMutation(request);
            BIND(&TMutation::Commit, mutation)
                .AsyncVia(Bootstrap_->GetHydraFacade()->GetEpochAutomatonInvoker())
                .Run()
                .Subscribe(BIND([] (const TErrorOr<TMutationResponse>& error) {
                    if (!error.IsOK()) {
                        LOG_ERROR(error, "Error committing node disposal mutation");
                    }
                }));
        }
    }


    int AllocateRackIndex()
    {
        for (int index = 0; index < MaxRackCount; ++index) {
            if (index == NullRackIndex)
                continue;
            auto mask = 1ULL << index;
            if (!(UsedRackIndexes_ & mask)) {
                UsedRackIndexes_ |= mask;
                return index;
            }
        }
        YUNREACHABLE();
    }

    void FreeRackIndex(int index)
    {
        auto mask = 1ULL << index;
        YCHECK(UsedRackIndexes_ & mask);
        UsedRackIndexes_ &= ~mask;
    }

};

DEFINE_ENTITY_MAP_ACCESSORS(TNodeTracker::TImpl, Node, TNode, TObjectId, NodeMap_)
DEFINE_ENTITY_MAP_ACCESSORS(TNodeTracker::TImpl, Rack, TRack, TRackId, RackMap_)

///////////////////////////////////////////////////////////////////////////////

TNodeTracker::TNodeTracker(
    TNodeTrackerConfigPtr config,
    TBootstrap* bootstrap)
    : Impl_(New<TImpl>(config, bootstrap))
{ }

void TNodeTracker::Initialize()
{
    Impl_->Initialize();
}

TNodeTracker::~TNodeTracker()
{ }

TNode* TNodeTracker::FindNode(TNodeId id)
{
    return Impl_->FindNode(id);
}

TNode* TNodeTracker::GetNode(TNodeId id)
{
    return Impl_->GetNode(id);
}

TNode* TNodeTracker::GetNodeOrThrow(TNodeId id)
{
    return Impl_->GetNodeOrThrow(id);
}

TNode* TNodeTracker::FindNodeByAddress(const Stroka& address)
{
    return Impl_->FindNodeByAddress(address);
}

TNode* TNodeTracker::GetNodeByAddress(const Stroka& address)
{
    return Impl_->GetNodeByAddress(address);
}

TNode* TNodeTracker::GetNodeByAddressOrThrow(const Stroka& address)
{
    return Impl_->GetNodeByAddressOrThrow(address);
}

TNode* TNodeTracker::FindNodeByHostName(const Stroka& hostName)
{
    return Impl_->FindNodeByHostName(hostName);
}

std::vector<TNode*> TNodeTracker::GetRackNodes(const TRack* rack)
{
    return Impl_->GetRackNodes(rack);
}

void TNodeTracker::SetNodeBanned(TNode* node, bool value)
{
    Impl_->SetNodeBanned(node, value);
}

void TNodeTracker::SetNodeDecommissioned(TNode* node, bool value)
{
    Impl_->SetNodeDecommissioned(node, value);
}

void TNodeTracker::SetNodeRack(TNode* node, TRack* rack)
{
    Impl_->SetNodeRack(node, rack);
}

TRack* TNodeTracker::CreateRack(const Stroka& name)
{
    return Impl_->CreateRack(name, NullObjectId);
}

void TNodeTracker::DestroyRack(TRack* rack)
{
    Impl_->DestroyRack(rack);
}

void TNodeTracker::RenameRack(TRack* rack, const Stroka& newName)
{
    Impl_->RenameRack(rack, newName);
}

TRack* TNodeTracker::FindRackByName(const Stroka& name)
{
    return Impl_->FindRackByName(name);
}

TRack* TNodeTracker::GetRackByNameOrThrow(const Stroka& name)
{
    return Impl_->GetRackByNameOrThrow(name);
}

bool TNodeTracker::TryAcquireNodeRegistrationSemaphore()
{
    return Impl_->TryAcquireNodeRegistrationSemaphore();
}

TMutationPtr TNodeTracker::CreateRegisterNodeMutation(
    const TReqRegisterNode& request)
{
    return Impl_->CreateRegisterNodeMutation(request);
}

TMutationPtr TNodeTracker::CreateUnregisterNodeMutation(
    const TReqUnregisterNode& request)
{
    return Impl_->CreateUnregisterNodeMutation(request);
}

TMutationPtr TNodeTracker::CreateFullHeartbeatMutation(
    TCtxFullHeartbeatPtr context)
{
    return Impl_->CreateFullHeartbeatMutation(context);
}

TMutationPtr TNodeTracker::CreateIncrementalHeartbeatMutation(
    TCtxIncrementalHeartbeatPtr context)
{
    return Impl_->CreateIncrementalHeartbeatMutation(context);
}

TTotalNodeStatistics TNodeTracker::GetTotalNodeStatistics()
{
    return Impl_->GetTotalNodeStatistics();
}

int TNodeTracker::GetRegisteredNodeCount()
{
    return Impl_->GetRegisteredNodeCount();
}

int TNodeTracker::GetOnlineNodeCount()
{
    return Impl_->GetOnlineNodeCount();
}

std::vector<NHive::TCellDescriptor> TNodeTracker::GetCellDescriptors()
{
    return Impl_->GetCellDescriptors();
}

DELEGATE_ENTITY_MAP_ACCESSORS(TNodeTracker, Node, TNode, TObjectId, *Impl_)
DELEGATE_ENTITY_MAP_ACCESSORS(TNodeTracker, Rack, TRack, TRackId, *Impl_)

DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeRegistered, *Impl_);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeUnregistered, *Impl_);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeDisposed, *Impl_);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeBanChanged, *Impl_);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeDecommissionChanged, *Impl_);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeRackChanged, *Impl_);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*, const TReqFullHeartbeat&), FullHeartbeat, *Impl_);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*, const TReqIncrementalHeartbeat&, TRspIncrementalHeartbeat*), IncrementalHeartbeat, *Impl_);
DELEGATE_SIGNAL(TNodeTracker, void(std::vector<TCellDescriptor>*), PopulateCellDescriptors, *Impl_);

///////////////////////////////////////////////////////////////////////////////

TNodeTracker::TClusterNodeTypeHandler::TClusterNodeTypeHandler(TImpl* owner)
    : TObjectTypeHandlerWithMapBase(owner->Bootstrap_, &owner->NodeMap_)
    , Owner_(owner)
{ }

IObjectProxyPtr TNodeTracker::TClusterNodeTypeHandler::DoGetProxy(
    TNode* node,
    TTransaction* /*transaction*/)
{
    return CreateClusterNodeProxy(Owner_->Bootstrap_, node);
}

void TNodeTracker::TClusterNodeTypeHandler::DoZombifyObject(TNode* node)
{
    TObjectTypeHandlerWithMapBase::DoZombifyObject(node);
    // NB: Destroy the cell right away and do not wait for GC to prevent
    // dangling links from occuring in //sys/tablet_cells.
    Owner_->DestroyNode(node);
}

///////////////////////////////////////////////////////////////////////////////

TNodeTracker::TRackTypeHandler::TRackTypeHandler(TImpl* owner)
    : TObjectTypeHandlerWithMapBase(owner->Bootstrap_, &owner->RackMap_)
    , Owner_(owner)
{ }

TObjectBase* TNodeTracker::TRackTypeHandler::CreateObject(
    const TObjectId& hintId,
    TTransaction* /*transaction*/,
    TAccount* /*account*/,
    IAttributeDictionary* attributes,
    TReqCreateObject* /*request*/,
    TRspCreateObject* /*response*/)
{
    auto name = attributes->Get<Stroka>("name");
    attributes->Remove("name");

    return Owner_->CreateRack(name, hintId);
}

IObjectProxyPtr TNodeTracker::TRackTypeHandler::DoGetProxy(
    TRack* rack,
    TTransaction* /*transaction*/)
{
    return CreateRackProxy(Owner_->Bootstrap_, rack);
}

void TNodeTracker::TRackTypeHandler::DoDestroyObject(TRack* rack)
{
    TObjectTypeHandlerWithMapBase::DoDestroyObject(rack);
    Owner_->DestroyRack(rack);
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NNodeTrackerServer
} // namespace NYT
