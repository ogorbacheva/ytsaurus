  #pragma once

#include "id.h"
#include "object_proxy.h"
#include "object_manager.h"
#include "object_ypath.pb.h"
#include "ypath.pb.h"

#include <ytlib/misc/property.h>
#include <ytlib/meta_state/map.h>
#include <ytlib/ytree/ypath_detail.h>
#include <ytlib/ytree/fluent.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

class TObjectBase
{
public:
    TObjectBase();

    //! Increments the object's reference counter.
    /*!
     *  \returns the incremented counter.
     */
    i32 RefObject();

    //! Decrements the object's reference counter.
    /*!
     *  \note
     *  Objects do not self-destruct, it's callers responsibility to check
     *  if the counter reaches zero.
     *  
     *  \returns the decremented counter.
     */
    i32 UnrefObject();

    //! Returns the current reference counter.
    i32 GetObjectRefCounter() const;

protected:
    TObjectBase(const TObjectBase& other);

    void Save(TOutputStream* output) const;
    void Load(TInputStream* input);

    i32 RefCounter;

};

////////////////////////////////////////////////////////////////////////////////

class TObjectWithIdBase
    : public TObjectBase
{
    DEFINE_BYVAL_RO_PROPERTY(TObjectId, Id);

public:
    TObjectWithIdBase();
    TObjectWithIdBase(const TObjectId& id);
    TObjectWithIdBase(const TObjectWithIdBase& other);

};

////////////////////////////////////////////////////////////////////////////////

class TObjectProxyBase
    : public virtual NYTree::TYPathServiceBase
    , public virtual NYTree::TSupportsGet
    , public virtual NYTree::TSupportsList
    , public virtual NYTree::TSupportsSet
    , public virtual NYTree::TSupportsRemove
    , public virtual IObjectProxy
{
public:
    TObjectProxyBase(
        TObjectManager* objectManager,
        const TObjectId& id,
        const Stroka& loggingCategory = ObjectServerLogger.GetCategory());

    virtual TObjectId GetId() const;

protected:
    TObjectManager::TPtr ObjectManager;
    TObjectId Id;

    virtual TResolveResult ResolveAttributes(const NYTree::TYPath& path, const Stroka& verb);

    NYTree::TYson DoGetAttribute(const Stroka& name, bool* isSystem = NULL);
    void DoSetAttribute(const Stroka name, NYTree::INode* value, bool isSystem);

    DECLARE_RPC_SERVICE_METHOD(NObjectServer::NProto, GetId);

    virtual void DoInvoke(NRpc::IServiceContext* context);

    virtual bool IsWriteRequest(NRpc::IServiceContext* context) const;

    //! Populates the list of all system attributes supported by this object.
    /*!
     *  \note
     *  Must not clear #attributes since additional items may be added in inheritors.
     */
    virtual void GetSystemAttributes(yvector<NYTree::TAttributeInfo>* attributes);

    //! Gets the value of a system attribute.
    /*!
     *  \returns False if there is no system attribute with the given name.
     */
    virtual bool GetSystemAttribute(const Stroka& name, NYTree::IYsonConsumer* consumer);

    //! Sets the value of a system attribute.
    /*! 
     *  \returns False if the attribute cannot be set or
     *  there is no system attribute with the given name.
     */
    virtual bool SetSystemAttribute(const Stroka& name, NYTree::TYsonProducer* producer);

    virtual void GetAttribute(const NYTree::TYPath& path, TReqGet* request, TRspGet* response, TCtxGet* context);
    virtual void ListAttribute(const NYTree::TYPath& path, TReqList* request, TRspList* response, TCtxList* context);
    virtual void SetAttribute(const NYTree::TYPath& path, TReqSet* request, TRspSet* response, TCtxSet* context);
    virtual void RemoveAttribute(const NYTree::TYPath& path, TReqRemove* request, TRspRemove* response, TCtxRemove* context);

    // The following methods provide means for accessing attribute sets.
    // In particular, these methods are responsible for resolving object and transaction ids.
    virtual yhash_set<Stroka> ListUserAttributes();
    // Returns empty TYson if the attribute is not found.
    virtual NYTree::TYson GetUserAttribute(const Stroka& name);
    virtual void SetUserAttribute(const Stroka& name, const NYTree::TYson& value);
    virtual bool RemoveUserAttribute(const Stroka& name);
};

////////////////////////////////////////////////////////////////////////////////

template <class TObject>
class TUnversionedObjectProxyBase
    : public TObjectProxyBase
{
public:
    typedef typename NMetaState::TMetaStateMap<TObjectId, TObject> TMap;

    TUnversionedObjectProxyBase(
        TObjectManager* objectManager,
        const TObjectId& id,
        TMap* map,
        const Stroka& loggingCategory = ObjectServerLogger.GetCategory())
        : TObjectProxyBase(objectManager, id, loggingCategory)
        , Map(map)
    {
        YASSERT(map);
    }

protected:
    TMap* Map;

    virtual void GetSelf(TReqGet* request, TRspGet* response, TCtxGet* context)
    {
        UNUSED(request);

        response->set_value(NYTree::BuildYsonFluently().Entity());
        context->Reply();
    }


    const TObject& GetTypedImpl() const
    {
        return Map->Get(GetId());
    }

    TObject& GetTypedImplForUpdate()
    {
        return Map->GetForUpdate(GetId());
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT

