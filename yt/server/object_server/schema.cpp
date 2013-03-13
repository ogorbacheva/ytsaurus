#include "stdafx.h"
#include "schema.h"
#include "private.h"
#include "type_handler.h"

#include <server/cell_master/bootstrap.h>

namespace NYT {
namespace NObjectServer {

using namespace NTransactionServer;
using namespace NSecurityServer;
using namespace NObjectClient;
using namespace NYTree;
using namespace NYson;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

TSchemaObject::TSchemaObject(const TObjectId& id)
    : TNonversionedObjectBase(id)
    , Acd_(this)
{ }

void TSchemaObject::Save(const TSaveContext& context) const
{
    TNonversionedObjectBase::Save(context);

    NSecurityServer::Save(context, Acd_);
}

void TSchemaObject::Load(const TLoadContext& context)
{
    TNonversionedObjectBase::Load(context);

    NSecurityServer::Load(context, Acd_);
}

////////////////////////////////////////////////////////////////////////////////

class TSchemaProxy
    : public TNonversionedObjectProxyBase<TSchemaObject>
{
public:
    TSchemaProxy(
        TBootstrap* bootstrap,
        TSchemaObject* object)
        : TBase(bootstrap, object)
    {
        Logger = ObjectServerLogger;
    }

private:
    typedef TNonversionedObjectProxyBase<TSchemaObject> TBase;

    virtual bool GetSystemAttribute(const Stroka& key, NYson::IYsonConsumer* consumer) override
    {
        if (key == "type") {
            auto type = TypeFromSchemaType(TypeFromId(GetId()));
            BuildYsonFluently(consumer)
                .Value(Sprintf("schema:%s", ~CamelCaseToUnderscoreCase(type.ToString())));
            return true;
        }

        return TBase::GetSystemAttribute(key, consumer);
    }

};


IObjectProxyPtr CreateSchemaProxy(TBootstrap* bootstrap, TSchemaObject* object)
{
    return New<TSchemaProxy>(bootstrap, object);
}

////////////////////////////////////////////////////////////////////////////////

class TSchemaTypeHandler
    : public IObjectTypeHandler
{
public:
    TSchemaTypeHandler(
        TBootstrap* bootstrap,
        EObjectType type)
        : Bootstrap(bootstrap)
        , Type(type)
    { }

    virtual EObjectType GetType() const override
    {
        return SchemaTypeFromType(Type);
    }

    virtual Stroka GetName(TObjectBase* object) override
    {
        UNUSED(object);
        return Sprintf("%s schema", ~FormatEnum(Type).Quote());
    }

    virtual TObjectBase* FindObject(const TObjectId& id) override
    {
        auto objectManager = Bootstrap->GetObjectManager();
        auto* object = objectManager->GetSchema(Type);
        return id == object->GetId() ? object : nullptr;
    }

    virtual IObjectProxyPtr GetProxy(
        TObjectBase* object,
        NTransactionServer::TTransaction* transaction) override
    {
        UNUSED(transaction);
        UNUSED(object);
        auto objectManager = Bootstrap->GetObjectManager();
        return objectManager->GetSchemaProxy(Type);
    }

    virtual TObjectBase* Create(
        NTransactionServer::TTransaction* transaction,
        NSecurityServer::TAccount* account,
        IAttributeDictionary* attributes,
        TReqCreateObject* request,
        TRspCreateObject* response) override
    {
        UNUSED(transaction);
        UNUSED(account);
        UNUSED(attributes);
        UNUSED(request);
        UNUSED(response);
        YUNREACHABLE();
    }

    virtual void Destroy(TObjectBase* object) override
    {
        UNUSED(object);
        YUNREACHABLE();
    }

    virtual void Unstage(
        TObjectBase* object,
        NTransactionServer::TTransaction* transaction,
        bool recursive) override
    {
        UNUSED(object);
        UNUSED(transaction);
        UNUSED(recursive);
        YUNREACHABLE();
    }

    virtual TNullable<TTypeCreationOptions> GetCreationOptions() const override
    {
        return Null;
    }

    virtual NSecurityServer::TAccessControlDescriptor* FindAcd(TObjectBase* object) override
    {
        return &static_cast<TSchemaObject*>(object)->Acd();
    }

    virtual TObjectBase* GetParent(TObjectBase* object) override
    {
        UNUSED(object);
        return nullptr;
    }

    virtual EPermissionSet GetSupportedPermissions() const override
    {
        auto permissions = NonePermissions;

        auto objectManager = Bootstrap->GetObjectManager();
        auto handler = objectManager->GetHandler(Type);

        if (!TypeIsVersioned(Type)) {
            permissions |= handler->GetSupportedPermissions();
        }

        auto options = handler->GetCreationOptions();
        if (options) {
            permissions |= EPermission::Create;
        }

        return permissions;
    }

private:
    TBootstrap* Bootstrap;
    EObjectType Type;

};

IObjectTypeHandlerPtr CreateSchemaTypeHandler(TBootstrap* bootstrap, EObjectType type)
{
    return New<TSchemaTypeHandler>(bootstrap, type);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT
