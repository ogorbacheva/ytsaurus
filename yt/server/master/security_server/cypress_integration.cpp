#include "cypress_integration.h"
#include "account.h"
#include "group.h"
#include "user.h"
#include "network_project.h"

#include <yt/server/master/cell_master/bootstrap.h>

#include <yt/server/master/cypress_server/virtual.h>

#include <yt/server/master/object_server/object_detail.h>
#include <yt/server/master/object_server/object_manager.h>

#include <yt/server/master/security_server/security_manager.h>

#include <yt/server/lib/misc/interned_attributes.h>
#include <yt/server/lib/misc/object_helpers.h>

#include <yt/client/object_client/helpers.h>

#include <yt/core/misc/collection_helpers.h>

#include <yt/core/ytree/fluent.h>
#include <yt/core/ytree/virtual.h>

namespace NYT::NSecurityServer {

using namespace NYTree;
using namespace NCypressServer;
using namespace NCellMaster;
using namespace NObjectClient;

////////////////////////////////////////////////////////////////////////////////

class TVirtualAccountMap
    : public TVirtualMapBase
{
public:
    TVirtualAccountMap(TBootstrap* bootstrap, INodePtr owningNode)
        : TVirtualMapBase(owningNode)
        , Bootstrap_(bootstrap)
    { }

private:
    using TBase = TVirtualMapBase;

    TBootstrap* const Bootstrap_;

    virtual std::vector<TString> GetKeys(i64 sizeLimit) const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return ToNames(GetValues(securityManager->Accounts(), sizeLimit));
    }

    virtual i64 GetSize() const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return securityManager->Accounts().GetSize();
    }

    virtual IYPathServicePtr FindItemService(TStringBuf key) const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* account = securityManager->FindAccountByName(TString(key));
        if (!IsObjectAlive(account)) {
            return nullptr;
        }

        const auto& objectManager = Bootstrap_->GetObjectManager();
        return objectManager->GetProxy(account);
    }

    virtual void ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors) override
    {
        TBase::ListSystemAttributes(descriptors);

        descriptors->push_back(EInternedAttributeKey::TotalResourceUsage);
        descriptors->push_back(EInternedAttributeKey::TotalCommittedResourceUsage);
        descriptors->push_back(EInternedAttributeKey::TotalResourceLimits);
    }

    virtual bool GetBuiltinAttribute(TInternedAttributeKey key, NYson::IYsonConsumer* consumer) override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        const auto& chunkManager = Bootstrap_->GetChunkManager();

        switch (key) {
            case EInternedAttributeKey::TotalResourceUsage: {
                TClusterResources resources;
                for (const auto& [accountId, account] : securityManager->Accounts()) {
                    resources += account->ClusterStatistics().ResourceUsage;
                }
                auto serializer = New<TSerializableClusterResources>(chunkManager, resources);
                BuildYsonFluently(consumer)
                    .Value(serializer);
                return true;
            }

            case EInternedAttributeKey::TotalCommittedResourceUsage: {
                TClusterResources resources;
                for (const auto& [accountId, account] : securityManager->Accounts()) {
                    resources += account->ClusterStatistics().CommittedResourceUsage;
                }
                auto serializer = New<TSerializableClusterResources>(chunkManager, resources);
                BuildYsonFluently(consumer)
                    .Value(serializer);
                return true;
            }

            case EInternedAttributeKey::TotalResourceLimits: {
                TClusterResources resources;
                for (const auto& [accountId, account] : securityManager->Accounts()) {
                    resources += account->ClusterResourceLimits();
                }
                auto serializer = New<TSerializableClusterResources>(chunkManager, resources);
                BuildYsonFluently(consumer)
                    .Value(serializer);
                return true;
            }

            default:
                return TBase::GetBuiltinAttribute(key, consumer);
        }
    }
};

INodeTypeHandlerPtr CreateAccountMapTypeHandler(TBootstrap* bootstrap)
{
    YT_VERIFY(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::AccountMap,
        BIND([=] (INodePtr owningNode) -> IYPathServicePtr {
            return New<TVirtualAccountMap>(bootstrap, owningNode);
        }),
        EVirtualNodeOptions::RedirectSelf);
}

////////////////////////////////////////////////////////////////////////////////

class TVirtualUserMap
    : public TVirtualMapBase
{
public:
    TVirtualUserMap(TBootstrap* bootstrap, INodePtr owningNode)
        : TVirtualMapBase(owningNode)
        , Bootstrap_(bootstrap)
    { }

private:
    TBootstrap* const Bootstrap_;

    virtual std::vector<TString> GetKeys(i64 sizeLimit) const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return ToNames(GetValues(securityManager->Users(), sizeLimit));
    }

    virtual i64 GetSize() const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return securityManager->Users().GetSize();
    }

    virtual IYPathServicePtr FindItemService(TStringBuf key) const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* user = securityManager->FindUserByName(TString(key));
        if (!IsObjectAlive(user)) {
            return nullptr;
        }

        const auto& objectManager = Bootstrap_->GetObjectManager();
        return objectManager->GetProxy(user);
    }
};

INodeTypeHandlerPtr CreateUserMapTypeHandler(TBootstrap* bootstrap)
{
    YT_VERIFY(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::UserMap,
        BIND([=] (INodePtr owningNode) -> IYPathServicePtr {
            return New<TVirtualUserMap>(bootstrap, owningNode);
        }),
        EVirtualNodeOptions::RedirectSelf);
}

////////////////////////////////////////////////////////////////////////////////

class TVirtualGroupMap
    : public TVirtualMapBase
{
public:
    TVirtualGroupMap(TBootstrap* bootstrap, INodePtr owningNode)
        : TVirtualMapBase(owningNode)
        , Bootstrap_(bootstrap)
    { }

private:
    TBootstrap* const Bootstrap_;

    virtual std::vector<TString> GetKeys(i64 sizeLimit) const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return ToNames(GetValues(securityManager->Groups(), sizeLimit));
    }

    virtual i64 GetSize() const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return securityManager->Groups().GetSize();
    }

    virtual IYPathServicePtr FindItemService(TStringBuf key) const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* group = securityManager->FindGroupByName(TString(key));
        if (!IsObjectAlive(group)) {
            return nullptr;
        }

        const auto& objectManager = Bootstrap_->GetObjectManager();
        return objectManager->GetProxy(group);
    }
};

INodeTypeHandlerPtr CreateGroupMapTypeHandler(TBootstrap* bootstrap)
{
    YT_VERIFY(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::GroupMap,
        BIND([=] (INodePtr owningNode) -> IYPathServicePtr {
            return New<TVirtualGroupMap>(bootstrap, owningNode);
        }),
        EVirtualNodeOptions::RedirectSelf);
}

////////////////////////////////////////////////////////////////////////////////

class TVirtualNetworkProjectMap
    : public TVirtualMapBase
{
public:
    TVirtualNetworkProjectMap(TBootstrap* bootstrap, INodePtr owningNode)
        : TVirtualMapBase(owningNode)
        , Bootstrap_(bootstrap)
    { }

private:
    TBootstrap* const Bootstrap_;

    virtual std::vector<TString> GetKeys(i64 sizeLimit) const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return ToNames(GetValues(securityManager->NetworkProjects(), sizeLimit));
    }

    virtual i64 GetSize() const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return securityManager->NetworkProjects().GetSize();
    }

    virtual IYPathServicePtr FindItemService(TStringBuf key) const override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* networkProject = securityManager->FindNetworkProjectByName(static_cast<TString>(key));
        if (!IsObjectAlive(networkProject)) {
            return nullptr;
        }

        const auto& objectManager = Bootstrap_->GetObjectManager();
        return objectManager->GetProxy(networkProject);
    }
};

INodeTypeHandlerPtr CreateNetworkProjectMapTypeHandler(TBootstrap* bootstrap)
{
    YT_VERIFY(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::NetworkProjectMap,
        BIND([=] (INodePtr owningNode) -> IYPathServicePtr {
            return New<TVirtualNetworkProjectMap>(bootstrap, owningNode);
        }),
        EVirtualNodeOptions::RedirectSelf);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSecurityServer
