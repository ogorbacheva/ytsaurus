#include "stdafx.h"
#include "account_proxy.h"
#include "account.h"
#include "security_manager.h"

#include <ytlib/yson/yson_consumer.h>

#include <ytlib/security_client/account_ypath.pb.h>

#include <server/object_server/object_detail.h>

#include <server/cell_master/bootstrap.h>

namespace NYT {
namespace NSecurityServer {

using namespace NYTree;
using namespace NRpc;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

class TAccountProxy
    : public NObjectServer::TNonversionedObjectProxyBase<TAccount>
{
public:
    TAccountProxy(
        NCellMaster::TBootstrap* bootstrap,
        const TAccountId& id,
        TAccountMetaMap* map)
        : TBase(
            bootstrap,
            id,
            map)
    { }

private:
    typedef NObjectServer::TNonversionedObjectProxyBase<TAccount> TBase;

    virtual void ValidateRemoval() override
    {
        auto securityManager = Bootstrap->GetSecurityManager();
        if (Id == securityManager->GetSysAccount()->GetId()) {
            THROW_ERROR_EXCEPTION("Cannot remove system account");
        }
    }

    virtual void ListSystemAttributes(std::vector<TAttributeInfo>* attributes) const override
    {
        attributes->push_back("name");
        attributes->push_back("resource_usage");
        attributes->push_back("node_count");
        TBase::ListSystemAttributes(attributes);
    }

    virtual bool GetSystemAttribute(const Stroka& key, NYson::IYsonConsumer* consumer) const override
    {
        const auto* account = GetThisTypedImpl();

        if (key == "name") {
            BuildYsonFluently(consumer)
                .Value(account->GetName());
            return true;
        }

        if (key == "resource_usage") {
            BuildYsonFluently(consumer)
                .Value(account->ResourceUsage());
            return true;
        }

        if (key == "node_count") {
            BuildYsonFluently(consumer)
                .Value(account->NodeCount());
            return true;
        }

        return TBase::GetSystemAttribute(key, consumer);
    }

    bool SetSystemAttribute(const Stroka& key, const NYTree::TYsonString& value) override
    {
        return TBase::SetSystemAttribute(key, value);
    }

};

IObjectProxyPtr CreateAccountProxy(
    NCellMaster::TBootstrap* bootstrap,
    const TAccountId& id,
    TAccountMetaMap* map)
{
    return New<TAccountProxy>(
        bootstrap,
        id,
        map);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NSecurityServer
} // namespace NYT

