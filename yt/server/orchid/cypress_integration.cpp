#include "stdafx.h"
#include "cypress_integration.h"

#include <core/misc/lazy_ptr.h>

#include <core/concurrency/action_queue.h>

#include <core/ytree/ephemeral_node_factory.h>
#include <core/ytree/ypath_detail.h>

#include <core/ytree/ypath.pb.h>

#include <ytlib/orchid/private.h>

#include <core/rpc/channel.h>
#include <core/rpc/message.h>
#include <core/rpc/caching_channel_factory.h>
#include <core/rpc/bus_channel.h>
#include <core/rpc/helpers.h>

#include <ytlib/orchid/orchid_service_proxy.h>

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/hydra_facade.h>

#include <server/object_server/object_manager.h>
#include <server/object_server/object_proxy.h>

#include <server/cypress_server/node.h>
#include <server/cypress_server/virtual.h>

#include <server/hydra/mutation_context.h>
#include <server/hydra/hydra_manager.h>

namespace NYT {
namespace NOrchid {

using namespace NRpc;
using namespace NBus;
using namespace NYTree;
using namespace NHydra;
using namespace NCypressServer;
using namespace NObjectServer;
using namespace NCellMaster;
using namespace NTransactionServer;
using namespace NOrchid::NProto;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = OrchidLogger;

static IChannelFactoryPtr ChannelFactory(CreateCachingChannelFactory(GetBusChannelFactory()));
static TLazyIntrusivePtr<TActionQueue> OrchidQueue(TActionQueue::CreateFactory("Orchid"));

////////////////////////////////////////////////////////////////////////////////

class TOrchidYPathService
    : public IYPathService
{
public:
    TOrchidYPathService(
        TBootstrap* bootstrap,
        TCypressNodeBase* trunkNode,
        TTransaction* transaction)
        : Bootstrap(bootstrap)
        , TrunkNode(trunkNode)
        , Transaction(transaction)
    {
        YCHECK(trunkNode->IsTrunk());
    }

    TResolveResult Resolve(const TYPath& path, IServiceContextPtr /*context*/) override
    {
        return TResolveResult::Here(path);
    }

    void Invoke(IServiceContextPtr context) override
    {
        const auto& ypathExt = context->RequestHeader().GetExtension(NYTree::NProto::TYPathHeaderExt::ypath_header_ext);
        if (ypathExt.mutating()) {
            THROW_ERROR_EXCEPTION("Orchid nodes are read-only");
        }

        // Prevent doing anything during recovery and at followers.
        auto hydraManager = Bootstrap->GetHydraFacade()->GetHydraManager();
        if (!hydraManager->IsLeader()) {
            return;
        }

        auto manifest = LoadManifest();

        auto channel = ChannelFactory->CreateChannel(manifest->RemoteAddress);

        TOrchidServiceProxy proxy(channel);
        proxy.SetDefaultTimeout(manifest->Timeout);

        auto path = GetRedirectPath(manifest, GetRequestYPath(context));
        const auto& method = context->GetMethod();

        auto requestMessage = context->GetRequestMessage();
        NRpc::NProto::TRequestHeader requestHeader;
        if (!ParseRequestHeader(requestMessage, &requestHeader)) {
            context->Reply(TError("Error parsing request header"));
            return;
        }

        SetRequestYPath(&requestHeader, path);

        auto innerRequestMessage = SetRequestHeader(requestMessage, requestHeader);

        auto outerRequest = proxy.Execute();
        outerRequest->Attachments() = innerRequestMessage.ToVector();

        LOG_DEBUG("Sending request to remote Orchid (RemoteAddress: %v, Path: %v, Method: %v, RequestId: %v)",
            manifest->RemoteAddress,
            path,
            method,
            outerRequest->GetRequestId());

        outerRequest->Invoke().Subscribe(
            BIND(
                &TOrchidYPathService::OnResponse,
                MakeStrong(this),
                context,
                manifest,
                path,
                method)
            .Via(OrchidQueue->GetInvoker()));
    }

    // TODO(panin): remove this when getting rid of IAttributeProvider
    virtual void SerializeAttributes(
        NYson::IYsonConsumer* /*consumer*/,
        const TAttributeFilter& /*filter*/,
        bool /*sortKeys*/) override
    {
        YUNREACHABLE();
    }

private:
    TBootstrap* Bootstrap;
    TCypressNodeBase* TrunkNode;
    TTransaction* Transaction;

    TOrchidManifestPtr LoadManifest()
    {
        auto objectManager = Bootstrap->GetObjectManager();
        auto proxy = objectManager->GetProxy(TrunkNode, Transaction);
        auto manifest = New<TOrchidManifest>();
        auto manifestNode = ConvertToNode(proxy->Attributes());
        try {
            manifest->Load(manifestNode);
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error parsing Orchid manifest")
                << ex;
        }
        return manifest;
    }

    void OnResponse(
        IServiceContextPtr context,
        TOrchidManifestPtr manifest,
        const TYPath& path,
        const Stroka& method,
        const TOrchidServiceProxy::TErrorOrRspExecutePtr& rspOrError)
    {
        if (rspOrError.IsOK()) {
            LOG_DEBUG("Orchid request succeded");
            const auto& rsp = rspOrError.Value();
            auto innerResponseMessage = TSharedRefArray(rsp->Attachments());
            context->Reply(innerResponseMessage);
        } else {
            LOG_DEBUG(rspOrError, "Orchid request failed");
            context->Reply(TError("Error executing an Orchid request")
                << TErrorAttribute("path", path)
                << TErrorAttribute("method", method)
                << TErrorAttribute("remote_address", manifest->RemoteAddress)
                << TErrorAttribute("remote_root", manifest->RemoteRoot)
                << rspOrError);
        }
    }

    static Stroka GetRedirectPath(TOrchidManifestPtr manifest, const TYPath& path)
    {
        return manifest->RemoteRoot + path;
    }
};

INodeTypeHandlerPtr CreateOrchidTypeHandler(TBootstrap* bootstrap)
{
    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::Orchid,
        BIND([=] (TCypressNodeBase* trunkNode, TTransaction* transaction) -> IYPathServicePtr {
            return New<TOrchidYPathService>(bootstrap, trunkNode, transaction);
        }),
        EVirtualNodeOptions::None);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NOrchid
} // namespace NYT
