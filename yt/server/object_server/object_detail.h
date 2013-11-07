#pragma once

#include "public.h"
#include "object.h"
#include "object_proxy.h"
#include "object_manager.h"

#include <core/misc/property.h>

#include <server/hydra/entity_map.h>

#include <core/ytree/ypath_detail.h>
#include <core/ytree/system_attribute_provider.h>

#include <ytlib/object_client/object_ypath.pb.h>
#include <ytlib/object_client/object_service_proxy.h>

#include <server/cell_master/public.h>

#include <server/transaction_server/public.h>

#include <server/security_server/public.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

class TStagedObject
{
    DEFINE_BYVAL_RW_PROPERTY(NTransactionServer::TTransaction*, StagingTransaction);
    DEFINE_BYVAL_RW_PROPERTY(NSecurityServer::TAccount*, StagingAccount);

public:
    TStagedObject();

    void Save(NCellMaster::TSaveContext& context) const;
    void Load(NCellMaster::TLoadContext& context);

    //! Returns True if the object is the staging area of some transaction.
    bool IsStaged() const;

};

////////////////////////////////////////////////////////////////////////////////

// We need definition of this class in header because we want to inherit it.
class TUserAttributeDictionary
    : public NYTree::IAttributeDictionary
{
public:
    TUserAttributeDictionary(TObjectManagerPtr objectManager, const TObjectId& objectId);

    // NYTree::IAttributeDictionary members
    virtual std::vector<Stroka> List() const override;
    virtual TNullable<NYTree::TYsonString> FindYson(const Stroka& key) const override;
    virtual void SetYson(const Stroka& key, const NYTree::TYsonString& value) override;
    virtual bool Remove(const Stroka& key) override;

protected:
    TObjectManagerPtr ObjectManager;
    TObjectId ObjectId;

};

////////////////////////////////////////////////////////////////////////////////

class TObjectProxyBase
    : public virtual NYTree::TSupportsAttributes
    , public virtual NYTree::ISystemAttributeProvider
    , public virtual IObjectProxy
{
public:
    TObjectProxyBase(
        NCellMaster::TBootstrap* bootstrap,
        TObjectBase* object);
    ~TObjectProxyBase();

    // IObjectProxy members
    virtual const TObjectId& GetId() const override;
    virtual const NYTree::IAttributeDictionary& Attributes() const override;
    virtual NYTree::IAttributeDictionary* MutableAttributes() override;
    virtual void Invoke(NRpc::IServiceContextPtr context) override;
    virtual void SerializeAttributes(
        NYson::IYsonConsumer* consumer,
        const NYTree::TAttributeFilter& filter,
        bool sortKeys) override;

protected:
    friend class TObjectManager;

    NCellMaster::TBootstrap* Bootstrap;
    TObjectBase* Object;

    std::unique_ptr<NYTree::IAttributeDictionary> UserAttributes;

    DECLARE_YPATH_SERVICE_METHOD(NObjectClient::NProto, GetId);
    DECLARE_YPATH_SERVICE_METHOD(NObjectClient::NProto, CheckPermission);

    //! Returns the full object id that coincides with #Id
    //! for non-versioned objects and additionally includes transaction id for
    //! versioned ones.
    virtual TVersionedObjectId GetVersionedId() const = 0;

    //! Returns the ACD for the object or |nullptr| is none exists.
    virtual NSecurityServer::TAccessControlDescriptor* FindThisAcd() = 0;

    void GuardedInvoke(NRpc::IServiceContextPtr context);
    virtual void BeforeInvoke(NRpc::IServiceContextPtr context);
    virtual void AfterInvoke(NRpc::IServiceContextPtr context);
    virtual bool DoInvoke(NRpc::IServiceContextPtr context) override;

    // NYTree::TSupportsAttributes members
    virtual NYTree::IAttributeDictionary* GetUserAttributes() override;
    virtual ISystemAttributeProvider* GetSystemAttributeProvider() override;

    virtual std::unique_ptr<NYTree::IAttributeDictionary> DoCreateUserAttributes();

    // NYTree::ISystemAttributeProvider members
    virtual void ListSystemAttributes(std::vector<TAttributeInfo>* attributes) override;
    virtual bool GetSystemAttribute(const Stroka& key, NYson::IYsonConsumer* consumer) override;
    virtual TAsyncError GetSystemAttributeAsync(const Stroka& key, NYson::IYsonConsumer* consumer) override;
    virtual bool SetSystemAttribute(const Stroka& key, const NYTree::TYsonString& value) override;

    TObjectBase* GetSchema(EObjectType type);
    TObjectBase* GetThisSchema();

    void ValidateTransaction();
    void ValidateNoTransaction();

    // TSupportsPermissions members
    virtual void ValidatePermission(
        NYTree::EPermissionCheckScope scope,
        NYTree::EPermission permission) override;

    void ValidatePermission(
        TObjectBase* object,
        NYTree::EPermission permission);

    bool IsRecovery() const;
    bool IsLeader() const;

    void ValidateActiveLeader() const;
    void ForwardToLeader(NRpc::IServiceContextPtr context);
    void OnLeaderResponse(NRpc::IServiceContextPtr context, NObjectClient::TObjectServiceProxy::TRspExecuteBatchPtr batchRsp);

};

////////////////////////////////////////////////////////////////////////////////

class TNontemplateNonversionedObjectProxyBase
    : public TObjectProxyBase
{
public:
    TNontemplateNonversionedObjectProxyBase(
        NCellMaster::TBootstrap* bootstrap,
        TObjectBase* object);

protected:
    virtual bool DoInvoke(NRpc::IServiceContextPtr context) override;

    virtual void GetSelf(TReqGet* request, TRspGet* response, TCtxGetPtr context) override;

    virtual void ValidateRemoval();
    virtual void RemoveSelf(TReqRemove* request, TRspRemove* response, TCtxRemovePtr context) override;

    virtual TVersionedObjectId GetVersionedId() const override;
    virtual NSecurityServer::TAccessControlDescriptor* FindThisAcd() override;

};

////////////////////////////////////////////////////////////////////////////////

template <class TObject>
class TNonversionedObjectProxyBase
    : public TNontemplateNonversionedObjectProxyBase
{
public:
    TNonversionedObjectProxyBase(NCellMaster::TBootstrap* bootstrap, TObject* object)
        : TNontemplateNonversionedObjectProxyBase(bootstrap, object)
    { }

protected:
    const TObject* GetThisTypedImpl() const
    {
        return static_cast<const TObject*>(Object);
    }

    TObject* GetThisTypedImpl()
    {
        return static_cast<TObject*>(Object);
    }

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT

