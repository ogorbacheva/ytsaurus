#include "stdafx.h"
#include "sys_node.h"
#include "private.h"

#include <core/ytree/fluent.h>

#include <server/cypress_server/node_detail.h>
#include <server/cypress_server/node_proxy_detail.h>

#include <server/hydra/hydra_manager.h>

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/hydra_facade.h>

namespace NYT {
namespace NObjectServer {

using namespace NYson;
using namespace NYTree;
using namespace NCypressServer;
using namespace NTransactionServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

class TSysNodeProxy
    : public TMapNodeProxy
{
public:
    TSysNodeProxy(
        INodeTypeHandlerPtr typeHandler,
        TBootstrap* bootstrap,
        TTransaction* transaction,
        TMapNode* trunkNode)
        : TBase(
            typeHandler,
            bootstrap,
            transaction,
            trunkNode)
    { }

private:
    typedef TMapNodeProxy TBase;

    void ListSystemAttributes(std::vector<TAttributeInfo>* attributes)
    {
        attributes->push_back("cell_tag");
        attributes->push_back("cell_id");
        attributes->push_back("last_committed_revision");
        TBase::ListSystemAttributes(attributes);
    }

    bool GetBuiltinAttribute(const Stroka& key, IYsonConsumer* consumer)
    {
        if (key == "cell_tag") {
            BuildYsonFluently(consumer)
                .Value(Bootstrap_->GetCellTag());
            return true;
        }

        if (key == "cell_tag") {
            BuildYsonFluently(consumer)
                .Value(Bootstrap_->GetCellId());
            return true;
        }

        if (key == "last_committed_revision") {
            auto hydraFacade = Bootstrap_->GetHydraFacade();
            auto hydraManager = hydraFacade->GetHydraManager();
            BuildYsonFluently(consumer)
                .Value(hydraManager->GetCommittedVersion().ToRevision());
            return true;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }

};

////////////////////////////////////////////////////////////////////////////////

class TSysNodeTypeHandler
    : public TMapNodeTypeHandler
{
public:
    explicit TSysNodeTypeHandler(TBootstrap* bootstrap)
        : TMapNodeTypeHandler(bootstrap)
    { }

    virtual EObjectType GetObjectType() override
    {
        return EObjectType::SysNode;
    }

private:
    virtual ICypressNodeProxyPtr DoGetProxy(
        TMapNode* trunkNode,
        NTransactionServer::TTransaction* transaction) override
    {
        return New<TSysNodeProxy>(
            this,
            Bootstrap_,
            transaction,
            trunkNode);
    }

};

INodeTypeHandlerPtr CreateSysNodeTypeHandler(TBootstrap* bootstrap)
{
    return New<TSysNodeTypeHandler>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT
