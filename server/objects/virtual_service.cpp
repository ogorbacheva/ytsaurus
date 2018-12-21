#include "virtual_service.h"
#include "db_schema.h"

namespace NYP::NServer::NObjects {

////////////////////////////////////////////////////////////////////////////////

const TScalarAttributeSchema<TVirtualService, TVirtualService::TSpec> TVirtualService::SpecSchema{
    &VirtualServicesTable.Fields.Spec,
    [] (TVirtualService* vs) { return &vs->Spec(); }
};

TVirtualService::TVirtualService(
    const TObjectId& id,
    IObjectTypeHandler* typeHandler,
    ISession* session)
    : TObject(id, TObjectId(), typeHandler, session)
    , Spec_(this, &SpecSchema)
{ }

EObjectType TVirtualService::GetType() const
{
    return EObjectType::VirtualService;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NServer::NObjects

