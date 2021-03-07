#include "proxy_role_proxy.h"
#include "proxy_role.h"

#include <yt/server/lib/misc/interned_attributes.h>

#include <yt/server/master/object_server/object_detail.h>

#include <yt/core/ytree/fluent.h>

namespace NYT::NSecurityServer {

using namespace NYTree;
using namespace NYson;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

class TProxyRoleProxy
    : public TNonversionedObjectProxyBase<TProxyRole>
{
public:
    TProxyRoleProxy(
        NCellMaster::TBootstrap* bootstrap,
        TObjectTypeMetadata* metadata,
        TProxyRole* proxyRole)
        : TBase(bootstrap, metadata, proxyRole)
    { }

private:
    using TBase = TNonversionedObjectProxyBase<TProxyRole>;

    virtual void ValidateRemoval() override
    {
        const auto* proxyRole = GetThisImpl();
        if (proxyRole->IsBuiltin()) {
            THROW_ERROR_EXCEPTION("Cannot remove a built-in %v",
                proxyRole->GetLowercaseObjectName());
        }
        ValidatePermission(EPermissionCheckScope::This, EPermission::Remove);
    }

    virtual void ListSystemAttributes(std::vector<ISystemAttributeProvider::TAttributeDescriptor>* descriptors) override
    {
        TBase::ListSystemAttributes(descriptors);

        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Name));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ProxyKind));
    }

    virtual bool GetBuiltinAttribute(TInternedAttributeKey key, NYson::IYsonConsumer* consumer) override
    {
        const auto* proxyKind = GetThisImpl();

        switch (key) {
            case EInternedAttributeKey::Name:
                BuildYsonFluently(consumer)
                    .Value(proxyKind->GetName());
                return true;

            case EInternedAttributeKey::ProxyKind:
                BuildYsonFluently(consumer)
                    .Value(proxyKind->GetProxyKind());
                return true;

            default:
                break;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }
};

////////////////////////////////////////////////////////////////////////////////

IObjectProxyPtr CreateProxyRoleProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TProxyRole* proxyRole)
{
    return New<TProxyRoleProxy>(bootstrap, metadata, proxyRole);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSecurityServer
