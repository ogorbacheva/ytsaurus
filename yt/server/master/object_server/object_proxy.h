#pragma once

#include "public.h"

#include <yt/core/ytree/attribute_owner.h>
#include <yt/core/ytree/system_attribute_provider.h>
#include <yt/core/ytree/ypath_service.h>

namespace NYT::NObjectServer {

////////////////////////////////////////////////////////////////////////////////

//! Provides a way for arbitrary objects to serve YPath requests.
struct IObjectProxy
    : public virtual NYTree::IYPathService
    , public virtual NYTree::IAttributeOwner
    , public virtual NYTree::ISystemAttributeProvider
{
    //! Returns object id.
    virtual TObjectId GetId() const = 0;

    //! Returns the corresponding object.
    virtual TObject* GetObject() const = 0;
};

DEFINE_REFCOUNTED_TYPE(IObjectProxy)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectServer

