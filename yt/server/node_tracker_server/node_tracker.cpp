#include "stdafx.h"
#include "node_tracker.h"
#include "config.h"
#include "node.h"
#include "private.h"

#include <core/misc/id_generator.h>
#include <core/misc/address.h>

#include <core/ytree/convert.h>

#include <core/ypath/token.h>

#include <core/concurrency/fiber.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>

#include <ytlib/object_client/public.h>

#include <server/chunk_server/job.h>

#include <server/cypress_server/cypress_manager.h>

#include <server/transaction_server/transaction_manager.h>
#include <server/transaction_server/transaction.h>

#include <server/object_server/object_manager.h>
#include <server/object_server/attribute_set.h>

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/meta_state_facade.h>
#include <server/cell_master/serialization_context.h>

namespace NYT {
namespace NNodeTrackerServer {

using namespace NConcurrency;
using namespace NYTree;
using namespace NYPath;
using namespace NNodeTrackerClient;
using namespace NNodeTrackerClient::NProto;
using namespace NHydra;
using namespace NCellMaster;
using namespace NObjectClient;
using namespace NCypressClient;
using namespace NNodeTrackerServer::NProto;
using namespace NTransactionServer;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = NodeTrackerServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TNodeTracker::TImpl
    : public TMasterAutomatonPart
{
public:
    TImpl(
        TNodeTrackerConfigPtr config,
        TBootstrap* bootstrap)
        : TMasterAutomatonPart(bootstrap)
        , Config(config)
        , OnlineNodeCount(0)
        , RegisteredNodeCount(0)
        , Profiler(NodeTrackerServerProfiler)
    {
        RegisterMethod(BIND(&TImpl::RegisterNode, Unretained(this)));
        RegisterMethod(BIND(&TImpl::UnregisterNode, Unretained(this)));
        RegisterMethod(BIND(&TImpl::HydraFullHeartbeat, Unretained(this)));
        RegisterMethod(BIND(&TImpl::HydraIncrementalHeartbeat, Unretained(this)));


        RegisterLoader(
            "NodeTracker.Keys",
            BIND(&TImpl::LoadKeys, Unretained(this)));
        RegisterLoader(
            "NodeTracker.Values",
            BIND(&TImpl::LoadValues, Unretained(this)));

        RegisterSaver(
            ESerializationPriority::Keys,
            "NodeTracker.Keys",
            BIND(&TImpl::SaveKeys, Unretained(this)));
        RegisterSaver(
            ESerializationPriority::Values,
            "NodeTracker.Values",
            BIND(&TImpl::SaveValues, Unretained(this)));

        SubscribeNodeConfigUpdated(BIND(&TImpl::OnNodeConfigUpdated, Unretained(this)));
    }

    void Initialize()
    {
        auto transactionManager = Bootstrap->GetTransactionManager();
        transactionManager->SubscribeTransactionCommitted(BIND(&TImpl::OnTransactionFinished, MakeWeak(this)));
        transactionManager->SubscribeTransactionAborted(BIND(&TImpl::OnTransactionFinished, MakeWeak(this)));
    }


    TMutationPtr CreateRegisterNodeMutation(
        const TReqRegisterNode& request)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(this, request, &TThis::RegisterNode);
    }

    TMutationPtr CreateUnregisterNodeMutation(
        const TReqUnregisterNode& request)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(this, request, &TThis::UnregisterNode);
    }

    TMutationPtr CreateFullHeartbeatMutation(
        TCtxFullHeartbeatPtr context)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(EAutomatonThreadQueue::Heartbeat)
            ->SetRequestData(context->GetRequestBody())
            ->SetType(context->Request().GetTypeName())
            ->SetAction(BIND(&TThis::RpcFullHeartbeat, MakeStrong(this), context));
    }

    TMutationPtr CreateIncrementalHeartbeatMutation(
        TCtxIncrementalHeartbeatPtr context)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation()
            ->SetRequestData(context->GetRequestBody())
            ->SetType(context->Request().GetTypeName())
            ->SetAction(BIND(&TThis::RpcIncrementalHeartbeat, MakeStrong(this), context));
    }


    void RefreshNodeConfig(TNode* node)
    {
        auto attributes = DoFindNodeConfig(node->GetAddress());
        if (!attributes)
            return;

        if (!ReconfigureYsonSerializable(node->GetConfig(), attributes))
            return;

        LOG_INFO_UNLESS(IsRecovery(), "Node configuration updated (Address: %s)", ~node->GetAddress());

        NodeConfigUpdated_.Fire(node);
    }


    DECLARE_ENTITY_MAP_ACCESSORS(Node, TNode, TNodeId);

    DEFINE_SIGNAL(void(TNode* node), NodeRegistered);
    DEFINE_SIGNAL(void(TNode* node), NodeUnregistered);
    DEFINE_SIGNAL(void(TNode* node), NodeConfigUpdated);
    DEFINE_SIGNAL(void(TNode* node, const TReqFullHeartbeat& request), FullHeartbeat);
    DEFINE_SIGNAL(void(TNode* node, const TReqIncrementalHeartbeat& request, TRspIncrementalHeartbeat* response), IncrementalHeartbeat);


    TNode* FindNodeByAddress(const Stroka& address)
    {
        auto it = AddressToNodeMap.find(address);
        return it == AddressToNodeMap.end() ? nullptr : it->second;
    }

    TNode* GetNodeByAddress(const Stroka& address)
    {
        auto* node = FindNodeByAddress(address);
        YCHECK(node);
        return node;
    }

    TNode* FindNodeByHostName(const Stroka& hostName)
    {
        auto it = HostNameToNodeMap.find(hostName);
        return it == AddressToNodeMap.end() ? nullptr : it->second;
    }

    TNode* GetNodeOrThrow(TNodeId id)
    {
        auto* node = FindNode(id);
        if (!node) {
            THROW_ERROR_EXCEPTION(
                NNodeTrackerClient::EErrorCode::NoSuchNode,
                "Invalid or expired node id %d",
                id);
        }
        return node;
    }


    TNodeConfigPtr FindNodeConfigByAddress(const Stroka& address)
    {
        auto attributes = DoFindNodeConfig(address);
        if (!attributes) {
            return nullptr;
        }

        try {
            return ConvertTo<TNodeConfigPtr>(attributes);
        } catch (const std::exception& ex) {
            LOG_WARNING(ex, "Error parsing configuration of node %s, defaults will be used", ~address);
            return nullptr;
        }
    }

    TNodeConfigPtr GetNodeConfigByAddress(const Stroka& address)
    {
        auto config = FindNodeConfigByAddress(address);
        return config ? config : New<TNodeConfig>();
    }

    
    TTotalNodeStatistics GetTotalNodeStatistics()
    {
        TTotalNodeStatistics result;
        for (const auto& pair : NodeMap) {
            const auto* node = pair.second;
            const auto& statistics = node->Statistics();
            result.AvailableSpace += statistics.total_available_space();
            result.UsedSpace += statistics.total_used_space();
            result.ChunkCount += statistics.total_chunk_count();
            result.OnlineNodeCount++;
        }
        return result;
    }

    int GetRegisteredNodeCount()
    {
        return RegisteredNodeCount;
    }

    int GetOnlineNodeCount()
    {
        return OnlineNodeCount;
    }

private:
    typedef TImpl TThis;

    TNodeTrackerConfigPtr Config;

    int OnlineNodeCount;
    int RegisteredNodeCount;

    NProfiling::TProfiler& Profiler;

    TIdGenerator NodeIdGenerator;

    NHydra::TEntityMap<TNodeId, TNode> NodeMap;
    yhash_map<Stroka, TNode*> AddressToNodeMap;
    yhash_multimap<Stroka, TNode*> HostNameToNodeMap;
    yhash_map<TTransaction*, TNode*> TransactionToNodeMap;


    TNodeId GenerateNodeId()
    {
        TNodeId id;
        while (true) {
            id = NodeIdGenerator.Next();
            // Beware of sentinels!
            if (id == InvalidNodeId) {
                // Just wait for the next attempt.
            } else if (id > MaxNodeId) {
                NodeIdGenerator.Reset();
            } else {
                break;
            }
        }
        return id;
    }

    IMapNodePtr DoFindNodeConfig(const Stroka& address)
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        auto resolver = cypressManager->CreateResolver();

        auto nodesNode = resolver->ResolvePath("//sys/nodes");
        YCHECK(nodesNode);

        auto nodesMap = nodesNode->AsMap();
        auto nodeNode = nodesMap->FindChild(address);
        if (!nodeNode) {
            return nullptr;
        }

        return nodeNode->Attributes().ToMap();
    }


    TRspRegisterNode RegisterNode(const TReqRegisterNode& request)
    {
        auto descriptor = FromProto<NNodeTrackerClient::TNodeDescriptor>(request.node_descriptor());
        const auto& statistics = request.statistics();
        const auto& address = descriptor.Address;

        // Kick-out any previous incarnation.
        {
            auto* existingNode = FindNodeByAddress(descriptor.Address);
            if (existingNode) {
                LOG_INFO_UNLESS(IsRecovery(), "Node kicked out due to address conflict (Address: %s, ExistingId: %d)",
                    ~address,
                    existingNode->GetId());
                DoUnregisterNode(existingNode);
            }
        }

        auto* node = DoRegisterNode(descriptor, statistics);

        TRspRegisterNode response;
        response.set_node_id(node->GetId());
        return response;
    }

    void UnregisterNode(const TReqUnregisterNode& request)
    {
        auto nodeId = request.node_id();

        // Allow nodeId to be invalid, just ignore such obsolete requests.
        auto* node = FindNode(nodeId);
        if (!node)
            return;

        DoUnregisterNode(node);
    }


    void RpcFullHeartbeat(TCtxFullHeartbeatPtr context)
    {
        return HydraFullHeartbeat(context->Request());
    }

    void HydraFullHeartbeat(const TReqFullHeartbeat& request)
    {
        PROFILE_TIMING ("/full_heartbeat_time") {
            auto nodeId = request.node_id();
            const auto& statistics = request.statistics();

            auto* node = GetNode(nodeId);

            LOG_DEBUG_UNLESS(IsRecovery(), "Processing full heartbeat (NodeId: %d, Address: %s, State: %s, %s)",
                nodeId,
                ~node->GetAddress(),
                ~node->GetState().ToString(),
                ~ToString(statistics));

            YCHECK(node->GetState() == ENodeState::Registered);
            UpdateNodeCounters(node, -1);
            node->SetState(ENodeState::Online);
            UpdateNodeCounters(node, +1);

            node->Statistics() = statistics;

            RenewNodeLease(node);

            LOG_INFO_UNLESS(IsRecovery(), "Node online (NodeId: %d, Address: %s)",
                nodeId,
                ~node->GetAddress());

            FullHeartbeat_.Fire(node, request);
        }
    }


    void RpcIncrementalHeartbeat(TCtxIncrementalHeartbeatPtr context)
    {
        DoIncrementalHeartbeat(context->Request(), &context->Response());
    }

    void HydraIncrementalHeartbeat(const TReqIncrementalHeartbeat& request)
    {
        DoIncrementalHeartbeat(request, nullptr);
    }

    void DoIncrementalHeartbeat(const TReqIncrementalHeartbeat& request, TRspIncrementalHeartbeat* response)
    {
        PROFILE_TIMING ("/incremental_heartbeat_time") {
            auto nodeId = request.node_id();
            const auto& statistics = request.statistics();

            auto* node = FindNode(nodeId);
            if (!node)
                return;

            LOG_DEBUG_UNLESS(IsRecovery(), "Processing incremental heartbeat (NodeId: %d, Address: %s, State: %s, %s)",
                nodeId,
                ~node->GetAddress(),
                ~node->GetState().ToString(),
                ~ToString(statistics));

            YCHECK(node->GetState() == ENodeState::Online);

            node->Statistics() = statistics;
            node->Alerts() = FromProto<Stroka>(request.alerts());

            RenewNodeLease(node);
            
            IncrementalHeartbeat_.Fire(node, request, response);
        }
    }


    void SaveKeys(NCellMaster::TSaveContext& context) const
    {
        NodeMap.SaveKeys(context);
    }

    void SaveValues(NCellMaster::TSaveContext& context) const
    {
        Save(context, NodeIdGenerator);
        NodeMap.SaveValues(context);
    }

    void LoadKeys(NCellMaster::TLoadContext& context)
    {
        NodeMap.LoadKeys(context);
    }

    void LoadValues(NCellMaster::TLoadContext& context)
    {
        Load(context, NodeIdGenerator);
        NodeMap.LoadValues(context);
    }

    virtual void Clear() override
    {
        NodeIdGenerator.Reset();
        NodeMap.Clear();
        
        AddressToNodeMap.clear();
        HostNameToNodeMap.clear();
        TransactionToNodeMap.clear();

        OnlineNodeCount = 0;
        RegisteredNodeCount = 0;
    }

    virtual void OnAfterSnapshotLoaded() override
    {
        AddressToNodeMap.clear();
        HostNameToNodeMap.clear();
        TransactionToNodeMap.clear();

        OnlineNodeCount = 0;
        RegisteredNodeCount = 0;

        for (const auto& pair : NodeMap) {
            auto* node = pair.second;
            const auto& address = node->GetAddress();

            YCHECK(AddressToNodeMap.insert(std::make_pair(address, node)).second);
            HostNameToNodeMap.insert(std::make_pair(Stroka(GetServiceHostName(address)), node));

            UpdateNodeCounters(node, +1);

            if (node->GetTransaction()) {
                RegisterLeaseTransaction(node);
            }
        }
    }

    virtual void OnRecoveryStarted() override
    {
        Profiler.SetEnabled(false);

        // Reset runtime info.
        for (const auto& pair : NodeMap) {
            auto* node = pair.second;

            node->ResetHints();
            
            for (auto& queue : node->ChunkReplicationQueues()) {
                queue.clear();
            }

            node->ChunkRemovalQueue().clear();
        }
    }

    virtual void OnRecoveryComplete() override
    {
        Profiler.SetEnabled(true);

        for (const auto& pair : NodeMap) {
            auto* node = pair.second;
            RefreshNodeConfig(node);
        }
    }

    virtual void OnLeaderActive() override
    {
        for (const auto& pair : NodeMap) {
            auto* node = pair.second;
            if (!node->GetTransaction()) {
                LOG_INFO("Missing node transaction, retrying unregistration (NodeId: %d, Address: %s)",
                    node->GetId(),
                    ~node->GetAddress());
                PostUnregisterCommit(node);
            }
        }
    }


    void UpdateNodeCounters(TNode* node, int delta)
    {
        switch (node->GetState()) {
            case ENodeState::Registered:
                RegisteredNodeCount += delta;
                break;
            case ENodeState::Online:
                OnlineNodeCount += delta;
                break;
            default:
                break;
        }
    }


    void RegisterLeaseTransaction(TNode* node)
    {
        auto* transaction = node->GetTransaction();
        YCHECK(transaction);
        YCHECK(TransactionToNodeMap.insert(std::make_pair(transaction, node)).second);
    }

    void UnregisterLeaseTransaction(TNode* node)
    {
        auto* transaction = node->GetTransaction();
        if (!transaction)
            return;

        YCHECK(TransactionToNodeMap.erase(transaction) == 1);
        node->SetTransaction(nullptr);
    }

    void RenewNodeLease(TNode* node)
    {
        auto* transaction = node->GetTransaction();
        if (!transaction)
            return;

        auto timeout = GetLeaseTimeout(node);
        transaction->SetTimeout(timeout);

        if (IsLeader()) {
            auto transactionManager = Bootstrap->GetTransactionManager();
            transactionManager->PingTransaction(transaction);
        }
    }

    TDuration GetLeaseTimeout(TNode* node)
    {
        switch (node->GetState()) {
            case ENodeState::Registered:
                return Config->RegisteredNodeTimeout;
            case ENodeState::Online:
                return Config->OnlineNodeTimeout;
            default:
                YUNREACHABLE();
        }
    }

    void OnTransactionFinished(TTransaction* transaction)
    {
        auto it = TransactionToNodeMap.find(transaction);
        if (it == TransactionToNodeMap.end())
            return;

        auto* node = it->second;
        LOG_INFO_UNLESS(IsRecovery(), "Node lease expired (NodeId: %d, Address: %s)",
            node->GetId(),
            ~node->GetAddress());

        UnregisterLeaseTransaction(node);

        if (IsLeader()) {
            PostUnregisterCommit(node);
        }
    }


    void RegisterNodeInCypress(TNode* node)
    {
        // We're already in the state thread but need to postpone the planned changes and enqueue a callback.
        // Doing otherwise will turn node registration and Cypress update into a single
        // logged change, which is undesirable.
        auto metaStateFacade = Bootstrap->GetMetaStateFacade();
        BIND(&TImpl::DoRegisterNodeInCypress, MakeStrong(this), node->GetId())
            .Via(metaStateFacade->GetEpochInvoker())
            .Run();
    }

    void DoRegisterNodeInCypress(TNodeId nodeId)
    {
        auto* node = FindNode(nodeId);
        if (!node)
            return;

        auto* transaction = node->GetTransaction();
        if (!transaction)
            return;

        auto objectManager = Bootstrap->GetObjectManager();
        auto rootService = objectManager->GetRootService();

        const auto& address = node->GetAddress();
        
        auto nodePath = "//sys/nodes/" + ToYPathLiteral(address);
        auto orchidPath = nodePath + "/orchid";

        try {
            {
                auto req = TCypressYPathProxy::Create(nodePath);
                req->set_type(EObjectType::CellNode);
                req->set_ignore_existing(true);

                auto defaultAttributes = ConvertToAttributes(New<TNodeConfig>());
                ToProto(req->mutable_node_attributes(), *defaultAttributes);

                auto asyncResult = ExecuteVerb(rootService, req);
                auto result = WaitFor(asyncResult);
                THROW_ERROR_EXCEPTION_IF_FAILED(*result);
            }

            {
                auto req = TCypressYPathProxy::Create(orchidPath);
                req->set_type(EObjectType::Orchid);
                req->set_ignore_existing(true);

                auto attributes = CreateEphemeralAttributes();
                attributes->Set("remote_address", address);
                ToProto(req->mutable_node_attributes(), *attributes);

                auto asyncResult = ExecuteVerb(rootService, req);
                auto result = WaitFor(asyncResult);
                THROW_ERROR_EXCEPTION_IF_FAILED(*result);
            }

            {
                auto req = TCypressYPathProxy::Lock(nodePath);
                req->set_mode(ELockMode::Shared);
                SetTransactionId(req, transaction->GetId());

                auto asyncResult = ExecuteVerb(rootService, req);
                auto result = WaitFor(asyncResult);
                THROW_ERROR_EXCEPTION_IF_FAILED(*result);
            }
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "Error registering node in Cypress");
        }
    }

    TNode* DoRegisterNode(const TNodeDescriptor& descriptor, const TNodeStatistics& statistics)
    {
        PROFILE_TIMING ("/node_register_time") {
            const auto& address = descriptor.Address;
            auto config = GetNodeConfigByAddress(address);
            auto nodeId = GenerateNodeId();

            auto* node = new TNode(nodeId, descriptor, config);
            node->SetState(ENodeState::Registered);
            node->Statistics() = statistics;

            NodeMap.Insert(nodeId, node);
            AddressToNodeMap.insert(std::make_pair(address, node));
            HostNameToNodeMap.insert(std::make_pair(Stroka(GetServiceHostName(address)), node));
            
            UpdateNodeCounters(node, +1);

            // Create lease transaction.
            auto transactionManager = Bootstrap->GetTransactionManager();
            auto timeout = GetLeaseTimeout(node);
            auto* transaction = transactionManager->StartTransaction(nullptr, timeout);
            node->SetTransaction(transaction);
            RegisterLeaseTransaction(node);

            // Set "title" attribute.
            auto objectManager = Bootstrap->GetObjectManager();
            auto* attributeSet = objectManager->GetOrCreateAttributes(TVersionedObjectId(transaction->GetId()));
            auto title = ConvertToYsonString(Sprintf("Lease for node %s", ~node->GetAddress()));
            YCHECK(attributeSet->Attributes().insert(std::make_pair("title", title)).second);
            
            if (IsLeader()) {
                RegisterNodeInCypress(node);
            }

            LOG_INFO_UNLESS(IsRecovery(), "Node registered (NodeId: %d, Address: %s, %s)",
                nodeId,
                ~address,
                ~ToString(statistics));

            NodeRegistered_.Fire(node);

            return node;
        }
    }

    void DoUnregisterNode(TNode* node)
    {
        PROFILE_TIMING ("/node_unregister_time") {
            auto nodeId = node->GetId();

            LOG_INFO_UNLESS(IsRecovery(), "Node unregistered (NodeId: %d, Address: %s)",
                nodeId,
                ~node->GetAddress());

            auto* transaction = node->GetTransaction();
            if (transaction && transaction->GetState() == ETransactionState::Active) {
                auto transactionManager = Bootstrap->GetTransactionManager();
                transactionManager->AbortTransaction(transaction);
            }

            UnregisterLeaseTransaction(node);
            
            const auto& address = node->GetAddress();
            YCHECK(AddressToNodeMap.erase(address) == 1);
            {
                auto hostNameRange = HostNameToNodeMap.equal_range(Stroka(GetServiceHostName(address)));
                for (auto it = hostNameRange.first; it != hostNameRange.second; ++it) {
                    if (it->second == node) {
                        HostNameToNodeMap.erase(it);
                        break;
                    }
                }
            }

            UpdateNodeCounters(node, -1);

            NodeUnregistered_.Fire(node);

            NodeMap.Remove(nodeId);
        }
    }


    void PostUnregisterCommit(TNode* node)
    {
        // Prevent multiple attempts to unregister the node.
        if (node->GetUnregisterPending())
            return;
        node->SetUnregisterPending(true);

        auto nodeId = node->GetId();

        TReqUnregisterNode message;
        message.set_node_id(nodeId);

        auto invoker = Bootstrap->GetMetaStateFacade()->GetEpochInvoker();
        CreateUnregisterNodeMutation(message)
            ->OnSuccess(BIND(&TThis::OnUnregisterCommitSucceeded, MakeStrong(this), nodeId).Via(invoker))
            ->OnError(BIND(&TThis::OnUnregisterCommitFailed, MakeStrong(this), nodeId).Via(invoker))
            ->PostCommit();
    }

    void OnUnregisterCommitSucceeded(TNodeId nodeId)
    {
        LOG_INFO("Node unregister commit succeeded (NodeId: %d)",
            nodeId);
    }

    void OnUnregisterCommitFailed(TNodeId nodeId, const TError& error)
    {
        LOG_ERROR(error, "Node unregister commit failed (NodeId: %d)",
            nodeId);
    }


    void OnNodeConfigUpdated(TNode* node)
    {
        if (node->GetConfig()->Banned) {
            LOG_INFO_UNLESS(IsRecovery(), "Node banned (Address: %s)",
                ~node->GetAddress());
            if (IsLeader()) {
                PostUnregisterCommit(node);
            }
        }
    }

};

DEFINE_ENTITY_MAP_ACCESSORS(TNodeTracker::TImpl, Node, TNode, TNodeId, NodeMap)

///////////////////////////////////////////////////////////////////////////////

TNodeTracker::TNodeTracker(
    TNodeTrackerConfigPtr config,
    TBootstrap* bootstrap)
    : Impl(New<TImpl>(config, bootstrap))
{ }

void TNodeTracker::Initialize()
{
    Impl->Initialize();
}

TNodeTracker::~TNodeTracker()
{ }

TNode* TNodeTracker::FindNodeByAddress(const Stroka& address)
{
    return Impl->FindNodeByAddress(address);
}

TNode* TNodeTracker::GetNodeByAddress(const Stroka& address)
{
    return Impl->GetNodeByAddress(address);
}

TNode* TNodeTracker::FindNodeByHostName(const Stroka& hostName)
{
    return Impl->FindNodeByHostName(hostName);
}

TNode* TNodeTracker::GetNodeOrThrow(TNodeId id)
{
    return Impl->GetNodeOrThrow(id);
}

TNodeConfigPtr TNodeTracker::FindNodeConfigByAddress(const Stroka& address)
{
    return Impl->FindNodeConfigByAddress(address);
}

TNodeConfigPtr TNodeTracker::GetNodeConfigByAddress(const Stroka& address)
{
    return Impl->GetNodeConfigByAddress(address);
}

TMutationPtr TNodeTracker::CreateRegisterNodeMutation(
    const TReqRegisterNode& request)
{
    return Impl->CreateRegisterNodeMutation(request);
}

TMutationPtr TNodeTracker::CreateUnregisterNodeMutation(
    const TReqUnregisterNode& request)
{
    return Impl->CreateUnregisterNodeMutation(request);
}

TMutationPtr TNodeTracker::CreateFullHeartbeatMutation(
    TCtxFullHeartbeatPtr context)
{
    return Impl->CreateFullHeartbeatMutation(context);
}

TMutationPtr TNodeTracker::CreateIncrementalHeartbeatMutation(
    TCtxIncrementalHeartbeatPtr context)
{
    return Impl->CreateIncrementalHeartbeatMutation(context);
}

void TNodeTracker::RefreshNodeConfig(TNode* node)
{
    return Impl->RefreshNodeConfig(node);
}

TTotalNodeStatistics TNodeTracker::GetTotalNodeStatistics()
{
    return Impl->GetTotalNodeStatistics();
}

int TNodeTracker::GetRegisteredNodeCount()
{
    return Impl->GetRegisteredNodeCount();
}

int TNodeTracker::GetOnlineNodeCount()
{
    return Impl->GetOnlineNodeCount();
}

DELEGATE_ENTITY_MAP_ACCESSORS(TNodeTracker, Node, TNode, TNodeId, *Impl)

DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeRegistered, *Impl);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeUnregistered, *Impl);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*), NodeConfigUpdated, *Impl);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*, const TReqFullHeartbeat&), FullHeartbeat, *Impl);
DELEGATE_SIGNAL(TNodeTracker, void(TNode*, const TReqIncrementalHeartbeat&, TRspIncrementalHeartbeat*), IncrementalHeartbeat, *Impl);

///////////////////////////////////////////////////////////////////////////////

} // namespace NNodeTrackerServer
} // namespace NYT
