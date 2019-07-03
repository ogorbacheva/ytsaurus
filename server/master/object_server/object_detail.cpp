#include "object_detail.h"
#include "private.h"
#include "attribute_set.h"
#include "object_manager.h"
#include "object_service.h"
#include "type_handler_detail.h"

#include <yt/server/master/table_server/table_node.h>

#include <yt/server/master/cell_master/bootstrap.h>
#include <yt/server/master/cell_master/config.h>
#include <yt/server/master/cell_master/config_manager.h>
#include <yt/server/master/cell_master/hydra_facade.h>
#include <yt/server/master/cell_master/multicell_manager.h>
#include <yt/server/master/cell_master/serialize.h>

#include <yt/server/master/cypress_server/virtual.h>

#include <yt/server/lib/election/election_manager.h>

#include <yt/server/lib/hydra/mutation_context.h>

#include <yt/server/lib/misc/interned_attributes.h>

#include <yt/server/master/object_server/object_manager.h>
#include <yt/server/master/object_server/type_handler.h>

#include <yt/server/master/security_server/account.h>
#include <yt/server/master/security_server/acl.h>
#include <yt/server/master/security_server/security_manager.h>
#include <yt/server/master/security_server/user.h>
#include <yt/server/master/security_server/group.h>
#include <yt/server/lib/security_server/proto/security_manager.pb.h>

#include <yt/server/master/transaction_server/transaction.h>

#include <yt/ytlib/cypress_client/cypress_ypath_proxy.h>
#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/election/cell_manager.h>

#include <yt/client/object_client/helpers.h>

#include <yt/core/misc/enum.h>
#include <yt/core/misc/string.h>

#include <yt/core/rpc/helpers.h>
#include <yt/core/rpc/message.h>
#include <yt/core/rpc/proto/rpc.pb.h>

#include <yt/core/ypath/tokenizer.h>

#include <yt/core/ytree/exception_helpers.h>
#include <yt/core/ytree/fluent.h>

#include <yt/core/yson/string.h>
#include <yt/core/yson/async_consumer.h>
#include <yt/core/yson/attribute_consumer.h>

namespace NYT::NObjectServer {

using namespace NRpc;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;
using namespace NCellMaster;
using namespace NCypressClient;
using namespace NObjectClient;
using namespace NSecurityClient;
using namespace NSecurityServer;
using namespace NTableServer;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

TObjectProxyBase::TObjectProxyBase(
    TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TObjectBase* object)
    : Bootstrap_(bootstrap)
    , Metadata_(metadata)
    , Object_(object)
{
    YT_ASSERT(Bootstrap_);
    YT_ASSERT(Metadata_);
    YT_ASSERT(Object_);
}

TObjectId TObjectProxyBase::GetId() const
{
    return Object_->GetId();
}

TObjectBase* TObjectProxyBase::GetObject() const
{
    return Object_;
}

const IAttributeDictionary& TObjectProxyBase::Attributes() const
{
    return *const_cast<TObjectProxyBase*>(this)->GetCombinedAttributes();
}

IAttributeDictionary* TObjectProxyBase::MutableAttributes()
{
    return GetCombinedAttributes();
}

DEFINE_YPATH_SERVICE_METHOD(TObjectProxyBase, GetBasicAttributes)
{
    DeclareNonMutating();

    context->SetRequestInfo();

    TGetBasicAttributesContext getBasicAttributesContext;
    if (request->has_permission()) {
        getBasicAttributesContext.Permission = CheckedEnumCast<EPermission>(request->permission());
    }
    if (request->has_columns()) {
        getBasicAttributesContext.Columns = FromProto<std::vector<TString>>(request->columns().items());
    }
    getBasicAttributesContext.OmitInaccessibleColumns = request->omit_inaccessible_columns();
    getBasicAttributesContext.PopulateSecurityTags = request->populate_security_tags();
    getBasicAttributesContext.CellTag = CellTagFromId(GetId());

    GetBasicAttributes(&getBasicAttributesContext);

    ToProto(response->mutable_object_id(), GetId());
    response->set_cell_tag(getBasicAttributesContext.CellTag);
    if (getBasicAttributesContext.OmittedInaccessibleColumns) {
        ToProto(response->mutable_omitted_inaccessible_columns()->mutable_items(), *getBasicAttributesContext.OmittedInaccessibleColumns);
    }
    if (getBasicAttributesContext.SecurityTags) {
        ToProto(response->mutable_security_tags()->mutable_items(), getBasicAttributesContext.SecurityTags->Items);
    }

    context->SetResponseInfo();
    context->Reply();
}

void TObjectProxyBase::GetBasicAttributes(TGetBasicAttributesContext* context)
{
    const auto& securityManager = Bootstrap_->GetSecurityManager();
    if (context->Permission) {
        securityManager->ValidatePermission(Object_, *context->Permission);
    }
}

DEFINE_YPATH_SERVICE_METHOD(TObjectProxyBase, CheckPermission)
{
    DeclareNonMutating();

    const auto& userName = request->user();
    auto permission = CheckedEnumCast<EPermission>(request->permission());

    TPermissionCheckOptions checkOptions;
    if (request->has_columns()) {
        checkOptions.Columns = FromProto<std::vector<TString>>(request->columns().items());
    }

    context->SetRequestInfo("User: %v, Permission: %v, Columns: %v",
        userName,
        permission,
        checkOptions.Columns);

    const auto& securityManager = Bootstrap_->GetSecurityManager();
    auto* user = securityManager->GetUserByNameOrThrow(userName);

    auto checkResponse = securityManager->CheckPermission(Object_, user, permission, checkOptions);

    const auto& objectManager = Bootstrap_->GetObjectManager();

    auto fillResult = [&] (auto* protoResult, const auto& result) {
        protoResult->set_action(static_cast<int>(result.Action));
        if (result.Object) {
            ToProto(protoResult->mutable_object_id(), result.Object->GetId());
            const auto& handler = objectManager->GetHandler(result.Object);
            protoResult->set_object_name(handler->GetName(result.Object));
        }
        if (result.Subject) {
            ToProto(protoResult->mutable_subject_id(), result.Subject->GetId());
            protoResult->set_subject_name(result.Subject->GetName());
        }
    };

    fillResult(response, checkResponse);
    if (checkResponse.Columns) {
        for (const auto& result : *checkResponse.Columns) {
            fillResult(response->mutable_columns()->add_items(), result);
        }
    }

    context->SetResponseInfo("Action: %v", checkResponse.Action);
    context->Reply();
}

void TObjectProxyBase::Invoke(const IServiceContextPtr& context)
{
    const auto& requestHeader = context->RequestHeader();

    // Validate that mutating requests are only being invoked inside mutations or recovery.
    const auto& ypathExt = requestHeader.GetExtension(NYTree::NProto::TYPathHeaderExt::ypath_header_ext);
    YT_VERIFY(!ypathExt.mutating() || NHydra::HasMutationContext());

    const auto& objectManager = Bootstrap_->GetObjectManager();
    if (requestHeader.HasExtension(NObjectClient::NProto::TPrerequisitesExt::prerequisites_ext)) {
        const auto& prerequisitesExt = requestHeader.GetExtension(NObjectClient::NProto::TPrerequisitesExt::prerequisites_ext);
        objectManager->ValidatePrerequisites(prerequisitesExt);
    }

    {
        TStringBuilder builder;
        TDelimitedStringBuilderWrapper delimitedBuilder(&builder);

        delimitedBuilder->AppendFormat("TargetObjectId: %v", GetVersionedId());

        if (!ypathExt.path().empty()) {
            delimitedBuilder->AppendFormat("RequestPathSuffix: %v", ypathExt.path());
        }

        context->SetRawRequestInfo(builder.Flush(), true);
    }

    NProfiling::TWallTimer timer;

    TSupportsAttributes::Invoke(context);

    const auto& Profiler = objectManager->GetProfiler();
    auto* counter = objectManager->GetMethodCumulativeExecuteTimeCounter(Object_->GetType(), context->GetMethod());
    Profiler.Increment(*counter, timer.GetElapsedValue());
}

void TObjectProxyBase::DoWriteAttributesFragment(
    IAsyncYsonConsumer* consumer,
    const std::optional<std::vector<TString>>& attributeKeys,
    bool stable)
{
    const auto& customAttributes = Attributes();

    if (attributeKeys) {
        for (const auto& key : *attributeKeys) {
            TAttributeValueConsumer attributeValueConsumer(consumer, key);

            auto value = customAttributes.FindYson(key);
            if (value) {
                attributeValueConsumer.OnRaw(value);
                continue;
            }

            auto internedKey = GetInternedAttributeKey(key);
            if (GetBuiltinAttribute(internedKey, &attributeValueConsumer)) {
                continue;
            }

            auto asyncValue = GetBuiltinAttributeAsync(internedKey);
            if (asyncValue) {
                attributeValueConsumer.OnRaw(std::move(asyncValue));
                continue; // just for the symmetry
            }
        }
    } else {
        std::vector<ISystemAttributeProvider::TAttributeDescriptor> builtinAttributes;
        ListBuiltinAttributes(&builtinAttributes);

        auto userKeys = customAttributes.List();

        if (stable) {
            std::sort(
                userKeys.begin(),
                userKeys.end());

            std::sort(
                builtinAttributes.begin(),
                builtinAttributes.end(),
                [] (const ISystemAttributeProvider::TAttributeDescriptor& lhs, const ISystemAttributeProvider::TAttributeDescriptor& rhs) {
                    return lhs.InternedKey < rhs.InternedKey;
                });
        }

        for (const auto& key : userKeys) {
            auto value = customAttributes.GetYson(key);
            consumer->OnKeyedItem(key);
            consumer->OnRaw(value);
        }

        for (const auto& descriptor : builtinAttributes) {
            auto key = descriptor.InternedKey;
            auto uninternedKey = TString(GetUninternedAttributeKey(key));
            TAttributeValueConsumer attributeValueConsumer(consumer, uninternedKey);

            if (descriptor.Opaque) {
                attributeValueConsumer.OnEntity();
                continue;
            }

            if (GetBuiltinAttribute(key, &attributeValueConsumer)) {
                continue;
            }

            auto asyncValue = GetBuiltinAttributeAsync(key);
            if (asyncValue) {
                attributeValueConsumer.OnRaw(std::move(asyncValue));
                continue; // just for the symmetry
            }
        }
    }
}

bool TObjectProxyBase::ShouldHideAttributes()
{
    return true;
}

bool TObjectProxyBase::DoInvoke(const IServiceContextPtr& context)
{
    DISPATCH_YPATH_SERVICE_METHOD(GetBasicAttributes);
    DISPATCH_YPATH_SERVICE_METHOD(Get);
    DISPATCH_YPATH_SERVICE_METHOD(List);
    DISPATCH_YPATH_SERVICE_METHOD(Set);
    DISPATCH_YPATH_SERVICE_METHOD(Remove);
    DISPATCH_YPATH_SERVICE_METHOD(Exists);
    DISPATCH_YPATH_SERVICE_METHOD(CheckPermission);
    return TYPathServiceBase::DoInvoke(context);
}

void TObjectProxyBase::SetAttribute(
    const NYTree::TYPath& path,
    TReqSet* request,
    TRspSet* response,
    const TCtxSetPtr& context)
{
    TSupportsAttributes::SetAttribute(path, request, response, context);
    ReplicateAttributeUpdate(context);
}

void TObjectProxyBase::RemoveAttribute(
    const NYTree::TYPath& path,
    TReqRemove* request,
    TRspRemove* response,
    const TCtxRemovePtr& context)
{
    TSupportsAttributes::RemoveAttribute(path, request, response, context);
    ReplicateAttributeUpdate(context);
}

void TObjectProxyBase::ReplicateAttributeUpdate(IServiceContextPtr context)
{
    if (!IsPrimaryMaster())
        return;

    const auto& objectManager = Bootstrap_->GetObjectManager();
    const auto& handler = objectManager->GetHandler(Object_->GetType());
    auto flags = handler->GetFlags();

    if (None(flags & ETypeFlags::ReplicateAttributes))
        return;

    auto replicationCellTags = handler->GetReplicationCellTags(Object_);
    PostToMasters(std::move(context), replicationCellTags);
}

IAttributeDictionary* TObjectProxyBase::GetCustomAttributes()
{
    YT_ASSERT(CustomAttributes_);
    return CustomAttributes_;
}

ISystemAttributeProvider* TObjectProxyBase::GetBuiltinAttributeProvider()
{
    return this;
}

void TObjectProxyBase::ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors)
{
    auto* acd = FindThisAcd();
    bool hasAcd = acd;
    bool hasOwner = acd && acd->GetOwner();
    bool isForeign = Object_->IsForeign();

    descriptors->push_back(EInternedAttributeKey::Id);
    descriptors->push_back(EInternedAttributeKey::Type);
    descriptors->push_back(EInternedAttributeKey::Builtin);
    descriptors->push_back(EInternedAttributeKey::RefCounter);
    descriptors->push_back(EInternedAttributeKey::EphemeralRefCounter);
    descriptors->push_back(EInternedAttributeKey::WeakRefCounter);
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ImportRefCounter)
        .SetPresent(isForeign));
    descriptors->push_back(EInternedAttributeKey::Foreign);
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::InheritAcl)
        .SetPresent(hasAcd)
        .SetWritable(true)
        .SetWritePermission(EPermission::Administer)
        .SetReplicated(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Acl)
        .SetPresent(hasAcd)
        .SetWritable(true)
        .SetWritePermission(EPermission::Administer)
        .SetReplicated(true));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Owner)
        .SetWritable(true)
        .SetPresent(hasOwner));
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::EffectiveAcl)
        .SetOpaque(true));
    descriptors->push_back(EInternedAttributeKey::UserAttributeKeys);
    descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::LifeStage)
        .SetReplicated(true)
        .SetMandatory(true));
}

const THashSet<TInternedAttributeKey>& TObjectProxyBase::GetBuiltinAttributeKeys()
{
    return Metadata_->BuiltinAttributeKeysCache.GetBuiltinAttributeKeys(this);
}

bool TObjectProxyBase::GetBuiltinAttribute(TInternedAttributeKey key, IYsonConsumer* consumer)
{
    const auto& securityManager = Bootstrap_->GetSecurityManager();
    const auto& objectManager = Bootstrap_->GetObjectManager();

    bool isForeign = Object_->IsForeign();
    auto* acd = FindThisAcd();

    switch (key) {
        case EInternedAttributeKey::Id:
            BuildYsonFluently(consumer)
                .Value(ToString(GetId()));
            return true;

        case EInternedAttributeKey::Type:
            BuildYsonFluently(consumer)
                .Value(TypeFromId(GetId()));
            return true;

        case EInternedAttributeKey::Builtin:
            BuildYsonFluently(consumer)
                .Value(Object_->IsBuiltin());
            return true;

        case EInternedAttributeKey::RefCounter:
            BuildYsonFluently(consumer)
                .Value(objectManager->GetObjectRefCounter(Object_));
            return true;

        case EInternedAttributeKey::EphemeralRefCounter:
            BuildYsonFluently(consumer)
                .Value(objectManager->GetObjectEphemeralRefCounter(Object_));
            return true;

        case EInternedAttributeKey::WeakRefCounter:
            BuildYsonFluently(consumer)
                .Value(objectManager->GetObjectWeakRefCounter(Object_));
            return true;

        case EInternedAttributeKey::ImportRefCounter:
            if (!isForeign) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(Object_->GetImportRefCounter());
            return true;

        case EInternedAttributeKey::Foreign:
            BuildYsonFluently(consumer)
                .Value(isForeign);
            return true;

        case EInternedAttributeKey::InheritAcl:
            if (!acd) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(acd->GetInherit());
            return true;

        case EInternedAttributeKey::Acl:
            if (!acd) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(acd->Acl());
            return true;

        case EInternedAttributeKey::Owner:
            if (!acd || !acd->GetOwner()) {
                break;
            }
            BuildYsonFluently(consumer)
                .Value(acd->GetOwner()->GetName());
            return true;

        case EInternedAttributeKey::EffectiveAcl:
            BuildYsonFluently(consumer)
                .Value(securityManager->GetEffectiveAcl(Object_));
            return true;

        case EInternedAttributeKey::UserAttributeKeys: {
            std::vector<TAttributeDescriptor> systemAttributes;
            ReserveAndListSystemAttributes(&systemAttributes);

            auto customAttributes = GetCustomAttributes()->List();
            THashSet<TString> customAttributesSet(customAttributes.begin(), customAttributes.end());

            for (const auto& attribute : systemAttributes) {
                if (attribute.Custom) {
                    customAttributesSet.erase(GetUninternedAttributeKey(attribute.InternedKey));
                }
            }

            BuildYsonFluently(consumer)
                .Value(customAttributesSet);
            return true;
        }

        case EInternedAttributeKey::LifeStage:
            BuildYsonFluently(consumer)
                .Value(Object_->GetLifeStage());
            return true;

        default:
            break;
    }

    return false;
}

TFuture<TYsonString> TObjectProxyBase::GetBuiltinAttributeAsync(TInternedAttributeKey /*key*/)
{
    return std::nullopt;
}

bool TObjectProxyBase::SetBuiltinAttribute(TInternedAttributeKey key, const TYsonString& value)
{
    auto securityManager = Bootstrap_->GetSecurityManager();
    auto* acd = FindThisAcd();
    if (!acd) {
        return false;
    }

    switch (key) {
        case EInternedAttributeKey::InheritAcl: {
            ValidateNoTransaction();

            acd->SetInherit(ConvertTo<bool>(value));
            return true;
        }

        case EInternedAttributeKey::Acl: {
            ValidateNoTransaction();

            TAccessControlList newAcl;
            Deserialize(newAcl, ConvertToNode(value), securityManager);

            acd->ClearEntries();
            for (const auto& ace : newAcl.Entries) {
                acd->AddEntry(ace);
            }

            return true;
        }

        case EInternedAttributeKey::Owner: {
            ValidateNoTransaction();

            auto name = ConvertTo<TString>(value);
            auto* owner = securityManager->GetSubjectByNameOrThrow(name);
            auto* user = securityManager->GetAuthenticatedUser();
            auto* superusers = securityManager->GetSuperusersGroup();
            if (user != owner && user->RecursiveMemberOf().find(superusers) == user->RecursiveMemberOf().end()) {
                THROW_ERROR_EXCEPTION(
                    NSecurityClient::EErrorCode::AuthorizationError,
                    "Access denied: can only set owner to self");
            }

            acd->SetOwner(owner);

            return true;
        }

        default:
            break;
    }

    return false;
}

bool TObjectProxyBase::RemoveBuiltinAttribute(TInternedAttributeKey /*key*/)
{
    return false;
}

void TObjectProxyBase::ValidateCustomAttributeUpdate(
    const TString& /*key*/,
    const TYsonString& /*oldValue*/,
    const TYsonString& /*newValue*/)
{ }

void TObjectProxyBase::GuardedValidateCustomAttributeUpdate(
    const TString& key,
    const TYsonString& oldValue,
    const TYsonString& newValue)
{
    try {
        if (newValue) {
            ValidateCustomAttributeLength(newValue);
        }
        ValidateCustomAttributeUpdate(key, oldValue, newValue);
    } catch (const std::exception& ex) {
        if (newValue) {
            THROW_ERROR_EXCEPTION("Error setting custom attribute %Qv",
                ToYPathLiteral(key))
                << ex;
        } else {
            THROW_ERROR_EXCEPTION("Error removing custom attribute %Qv",
                ToYPathLiteral(key))
                << ex;
        }
    }
}

void TObjectProxyBase::ValidateCustomAttributeLength(const TYsonString& value)
{
    auto size = value.GetData().length();
    auto limit = GetDynamicCypressManagerConfig()->MaxAttributeSize;
    if (size > limit) {
        THROW_ERROR_EXCEPTION(
            NYTree::EErrorCode::MaxAttributeSizeViolation,
            "Attribute size limit exceeded: %v > %v",
            size,
            limit);
    }
}

void TObjectProxyBase::DeclareMutating()
{
    YT_VERIFY(NHydra::HasMutationContext());
}

void TObjectProxyBase::DeclareNonMutating()
{ }

void TObjectProxyBase::ValidateTransaction()
{
    if (!GetVersionedId().IsBranched()) {
        THROW_ERROR_EXCEPTION("Operation cannot be performed outside of a transaction");
    }
}

void TObjectProxyBase::ValidateNoTransaction()
{
    if (GetVersionedId().IsBranched()) {
        THROW_ERROR_EXCEPTION("Operation cannot be performed in transaction");
    }
}

void TObjectProxyBase::ValidatePermission(EPermissionCheckScope scope, EPermission permission, const TString& /* user */)
{
    YT_VERIFY(scope == EPermissionCheckScope::This);
    ValidatePermission(Object_, permission);
}

void TObjectProxyBase::ValidatePermission(TObjectBase* object, EPermission permission)
{
    YT_VERIFY(object);
    const auto& securityManager = Bootstrap_->GetSecurityManager();
    auto* user = securityManager->GetAuthenticatedUser();
    securityManager->ValidatePermission(object, user, permission);
}

bool TObjectProxyBase::IsRecovery() const
{
    return Bootstrap_->GetHydraFacade()->GetHydraManager()->IsRecovery();
}

bool TObjectProxyBase::IsLeader() const
{
    return Bootstrap_->GetHydraFacade()->GetHydraManager()->IsLeader();
}

bool TObjectProxyBase::IsFollower() const
{
    return Bootstrap_->GetHydraFacade()->GetHydraManager()->IsFollower();
}

bool TObjectProxyBase::IsPrimaryMaster() const
{
    return Bootstrap_->IsPrimaryMaster();
}

bool TObjectProxyBase::IsSecondaryMaster() const
{
    return Bootstrap_->IsSecondaryMaster();
}

void TObjectProxyBase::RequireLeader() const
{
    Bootstrap_->GetHydraFacade()->RequireLeader();
}

void TObjectProxyBase::PostToSecondaryMasters(IServiceContextPtr context)
{
    const auto& multicellManager = Bootstrap_->GetMulticellManager();
    multicellManager->PostToSecondaryMasters(
        TCrossCellMessage(Object_->GetId(), std::move(context)));
}

void TObjectProxyBase::PostToMasters(IServiceContextPtr context, const TCellTagList& cellTags)
{
    const auto& multicellManager = Bootstrap_->GetMulticellManager();
    multicellManager->PostToMasters(
        TCrossCellMessage(Object_->GetId(), std::move(context)),
        cellTags);
}

void TObjectProxyBase::PostToMaster(IServiceContextPtr context, TCellTag cellTag)
{
    const auto& multicellManager = Bootstrap_->GetMulticellManager();
    multicellManager->PostToMaster(
        TCrossCellMessage(Object_->GetId(), std::move(context)),
        cellTag);
}

const NCypressServer::TDynamicCypressManagerConfigPtr& TObjectProxyBase::GetDynamicCypressManagerConfig() const
{
    return Bootstrap_->GetConfigManager()->GetConfig()->CypressManager;
}

////////////////////////////////////////////////////////////////////////////////

TNontemplateNonversionedObjectProxyBase::TCustomAttributeDictionary::TCustomAttributeDictionary(
    TNontemplateNonversionedObjectProxyBase* proxy)
    : Proxy_(proxy)
{ }

std::vector<TString> TNontemplateNonversionedObjectProxyBase::TCustomAttributeDictionary::List() const
{
    const auto* object = Proxy_->Object_;
    const auto* attributes = object->GetAttributes();
    std::vector<TString> keys;
    if (attributes) {
        for (const auto& pair : attributes->Attributes()) {
            // Attribute cannot be empty (i.e. deleted) in null transaction.
            YT_ASSERT(pair.second);
            keys.push_back(pair.first);
        }
    }
    return keys;
}

TYsonString TNontemplateNonversionedObjectProxyBase::TCustomAttributeDictionary::FindYson(const TString& key) const
{
    const auto* object = Proxy_->Object_;
    const auto* attributes = object->GetAttributes();
    if (!attributes) {
        return TYsonString();
    }

    auto it = attributes->Attributes().find(key);
    if (it == attributes->Attributes().end()) {
        return TYsonString();
    }

    // Attribute cannot be empty (i.e. deleted) in null transaction.
    YT_ASSERT(it->second);
    return it->second;
}

void TNontemplateNonversionedObjectProxyBase::TCustomAttributeDictionary::SetYson(const TString& key, const TYsonString& value)
{
    auto oldValue = FindYson(key);
    Proxy_->GuardedValidateCustomAttributeUpdate(key, oldValue, value);

    auto* object = Proxy_->Object_;
    auto* attributes = object->GetMutableAttributes();
    attributes->Attributes()[key] = value;
}

bool TNontemplateNonversionedObjectProxyBase::TCustomAttributeDictionary::Remove(const TString& key)
{
    auto oldValue = FindYson(key);
    Proxy_->GuardedValidateCustomAttributeUpdate(key, oldValue, TYsonString());

    auto* object = Proxy_->Object_;
    if (!object->GetAttributes()) {
        return false;
    }

    auto* attributes = object->GetMutableAttributes();
    auto it = attributes->Attributes().find(key);
    if (it == attributes->Attributes().end()) {
        return false;
    }

    // Attribute cannot be empty (i.e. deleted) in null transaction.
    YT_ASSERT(it->second);
    attributes->Attributes().erase(it);
    if (attributes->Attributes().empty()) {
        object->ClearAttributes();
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

TNontemplateNonversionedObjectProxyBase::TNontemplateNonversionedObjectProxyBase(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TObjectBase* object)
    : TObjectProxyBase(bootstrap, metadata, object)
    , CustomAttributesImpl_(this)
{
    CustomAttributes_ = &CustomAttributesImpl_;
}

bool TNontemplateNonversionedObjectProxyBase::DoInvoke(const IServiceContextPtr& context)
{
    DISPATCH_YPATH_SERVICE_METHOD(Remove);
    return TObjectProxyBase::DoInvoke(context);
}

void TNontemplateNonversionedObjectProxyBase::GetSelf(
    TReqGet* /*request*/,
    TRspGet* response,
    const TCtxGetPtr& context)
{
    ValidatePermission(EPermissionCheckScope::This, EPermission::Read);
    context->SetRequestInfo();

    response->set_value("#");
    context->Reply();
}

void TNontemplateNonversionedObjectProxyBase::ValidateRemoval()
{
    THROW_ERROR_EXCEPTION("Object cannot be removed explicitly");
}

void TNontemplateNonversionedObjectProxyBase::RemoveSelf(
    TReqRemove* /*request*/,
    TRspRemove* /*response*/,
    const TCtxRemovePtr& context)
{
    ValidatePermission(EPermissionCheckScope::This, EPermission::Remove);
    ValidateRemoval();

    const auto& objectManager = Bootstrap_->GetObjectManager();
    if (objectManager->GetObjectRefCounter(Object_) != 1) {
        THROW_ERROR_EXCEPTION("Object is in use");
    }

    objectManager->UnrefObject(Object_);

    context->Reply();
}

TVersionedObjectId TNontemplateNonversionedObjectProxyBase::GetVersionedId() const
{
    return TVersionedObjectId(Object_->GetId());
}

TAccessControlDescriptor* TNontemplateNonversionedObjectProxyBase::FindThisAcd()
{
    const auto& securityManager = Bootstrap_->GetSecurityManager();
    return securityManager->FindAcd(Object_);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectServer

