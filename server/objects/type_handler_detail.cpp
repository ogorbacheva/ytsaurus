#include "type_handler_detail.h"
#include "helpers.h"

#include <yp/server/master/bootstrap.h>

#include <yp/server/access_control/access_control_manager.h>

#include <yt/ytlib/query_client/ast.h>

namespace NYP::NServer::NObjects {

using namespace NAccessControl;

using namespace NYT::NYTree;
using namespace NYT::NQueryClient;
using namespace NYT::NQueryClient::NAst;

using std::placeholders::_1;
using std::placeholders::_2;

////////////////////////////////////////////////////////////////////////////////

TObjectTypeHandlerBase::TObjectTypeHandlerBase(
    NMaster::TBootstrap* bootstrap,
    EObjectType type)
    : Bootstrap_(bootstrap)
    , Type_(type)
    , SchemaId_(FormatEnum(Type_))
{
    RootAttributeSchema_ = MakeAttributeSchema(TString())
        ->AddChildren({
            MetaAttributeSchema_ = MakeAttributeSchema("meta")
                ->AddChildren({
                    IdAttributeSchema_ = MakeAttributeSchema("id")
                        ->SetAttribute(TObject::IdSchema),

                    MakeAttributeSchema("type")
                        ->SetExpressionBuilder(
                            [type = Type_] (IQueryContext* /*context*/) {
                                return New<TLiteralExpression>(NYT::NQueryClient::TSourceLocation(), FormatEnum(type));
                            }),

                    MakeAttributeSchema("creation_time")
                        ->SetAttribute(TObject::CreationTimeSchema),

                    MakeAttributeSchema("acl")
                        ->SetAttribute(TObject::AclSchema)
                        ->SetUpdatable()
                        ->SetValidator<TObject>(std::bind(&TObjectTypeHandlerBase::ValidateAcl, this, _1, _2)),

                    MakeEtcAttributeSchema()
                        ->SetAttribute(TObject::MetaEtcSchema)
                        ->SetUpdatable()
                        ->SetValidator<TObject>(std::bind(&TObjectTypeHandlerBase::ValidateMetaEtc, this, _1, _2))
                }),


            SpecAttributeSchema_ = MakeAttributeSchema("spec")
                ->SetUpdatable(),

            StatusAttributeSchema_ = MakeAttributeSchema("status"),

            LabelsAttributeSchema_ = MakeAttributeSchema("labels")
                ->SetAttribute(TObject::LabelsSchema)
                ->SetUpdatable(),

            AnnotationsAttributeSchema_ = MakeAttributeSchema("annotations")
                ->SetAnnotationsAttribute(),

            ControlAttributeSchema_ = MakeAttributeSchema("control")
                ->SetControlAttribute()
        });

    if (Type_ != EObjectType::Schema) {
        MetaAttributeSchema_->AddChild(
            MakeAttributeSchema("inherit_acl")
                ->SetAttribute(TObject::InheritAclSchema)
                ->SetUpdatable());
    }
}

EObjectType TObjectTypeHandlerBase::GetType()
{
    return Type_;
}

EObjectType TObjectTypeHandlerBase::GetParentType()
{
    return EObjectType::Null;
}

TObject* TObjectTypeHandlerBase::GetParent(TObject* /*object*/)
{
    return nullptr;
}

const TDBField* TObjectTypeHandlerBase::GetParentIdField()
{
    return nullptr;
}

TChildrenAttributeBase* TObjectTypeHandlerBase::GetParentChildrenAttribute(TObject* /*parent*/)
{
    Y_UNREACHABLE();
}

TObjectId TObjectTypeHandlerBase::GetSchemaObjectId()
{
    if (Type_ == EObjectType::Schema) {
        return TObjectId();
    }
    return SchemaId_;
}

TObject* TObjectTypeHandlerBase::GetSchemaObject(TObject* object)
{
    if (Type_ == EObjectType::Schema) {
        return nullptr;
    }
    auto* session = object->GetSession();
    return session->GetObject(EObjectType::Schema, SchemaId_);
}

TAttributeSchema* TObjectTypeHandlerBase::GetRootAttributeSchema()
{
    return RootAttributeSchema_;
}

TAttributeSchema* TObjectTypeHandlerBase::GetIdAttributeSchema()
{
    return IdAttributeSchema_;
}

TAttributeSchema* TObjectTypeHandlerBase::GetParentIdAttributeSchema()
{
    return ParentIdAttributeSchema_;
}

void TObjectTypeHandlerBase::BeforeObjectCreated(
    TTransaction* /*transaction*/,
    TObject* object)
{
    for (auto* attribute : object->Attributes()) {
        attribute->OnObjectCreated();
    }

    object->CreationTime() = TInstant::Now();
    object->MetaEtc()->set_uuid(GenerateUuid());
    object->Labels() = GetEphemeralNodeFactory()->CreateMap();

    const auto& accessControlManager = Bootstrap_->GetAccessControlManager();
    object->InheritAcl() = true;
    auto permissions = GetDefaultPermissions();
    if (!permissions.empty()) {
        NClient::NApi::NProto::TAccessControlEntry ace;
        ace.set_action(NClient::NApi::NProto::ACA_ALLOW);
        for (auto permission : GetDefaultPermissions()) {
            ace.add_permissions(static_cast<NClient::NApi::NProto::EAccessControlPermission>(permission));
        }
        if (accessControlManager->HasAuthenticatedUser()) {
            ace.add_subjects(accessControlManager->GetAuthenticatedUser());
        }
        object->Acl()->push_back(std::move(ace));
    }
}

void TObjectTypeHandlerBase::AfterObjectCreated(
    TTransaction* /*transaction*/,
    TObject* /*object*/)
{ }

void TObjectTypeHandlerBase::BeforeObjectRemoved(
    TTransaction* /*transaction*/,
    TObject* object)
{
    if (object->IsBuiltin()) {
        THROW_ERROR_EXCEPTION("Cannot remove built-in %v %v",
            GetCapitalizedHumanReadableTypeName(object->GetType()),
            GetObjectDisplayName(object));
    }
}

void TObjectTypeHandlerBase::AfterObjectRemoved(
    TTransaction* /*transaction*/,
    TObject* /*object*/)
{ }

TAttributeSchema* TObjectTypeHandlerBase::MakeAttributeSchema(
    const TString& name)
{
    auto schemaHolder = std::make_unique<TAttributeSchema>(this, name);
    auto* schema = schemaHolder.get();
    AttributeSchemas_.push_back(std::move(schemaHolder));
    return schema;
}

TAttributeSchema* TObjectTypeHandlerBase::MakeEtcAttributeSchema()
{
    return MakeAttributeSchema(TString())
        ->SetEtc();
}

std::vector<EAccessControlPermission> TObjectTypeHandlerBase::GetDefaultPermissions()
{
    return {
        EAccessControlPermission::Read,
        EAccessControlPermission::Write,
    };
}

bool TObjectTypeHandlerBase::IsObjectNameSupported() const
{
    return false;
}

void TObjectTypeHandlerBase::ValidateMetaEtc(TTransaction* /*transaction*/, TObject* object)
{
    const auto& metaEtcNew = object->MetaEtc().Load();
    const auto& metaEtcOld = object->MetaEtc().LoadOld();

    if (object->DidExist() && metaEtcOld.uuid() != metaEtcNew.uuid()) {
        THROW_ERROR_EXCEPTION("Changing /meta/uuid is forbidden");
    }

    if (metaEtcNew.has_name() && !IsObjectNameSupported()) {
        THROW_ERROR_EXCEPTION("Cannot set /meta/name for %v objects",
            GetLowercaseHumanReadableTypeName(GetType()));
    }
}

void TObjectTypeHandlerBase::ValidateAcl(TTransaction* transaction, TObject* object)
{
    const auto& oldAcl = object->Acl().LoadOld();
    const auto& acl = object->Acl().Load();

    auto doPerSubject = [] (const auto& acl, const auto& callback) {
        for (const auto& ace : acl) {
            for (const auto& subjectId : ace.subjects()) {
                callback(subjectId);
            }
        }
    };

    THashSet<TObjectId> oldSubjectIds;
    doPerSubject(oldAcl, [&oldSubjectIds] (const TObjectId& subjectId) {
        oldSubjectIds.insert(subjectId);
    });

    doPerSubject(acl, [&oldSubjectIds, transaction] (const TObjectId& subjectId) {
        if (!oldSubjectIds.contains(subjectId)) {
            ValidateSubjectExists(transaction, subjectId);
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYP::NServer::NObjects

