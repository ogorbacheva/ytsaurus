#ifndef HELPERS_INL_H_
#error "Direct inclusion of this file is not allowed, include helpers.h"
#endif

#include "private.h"

#include <yt/ytlib/api/client.h>

#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/object_client/public.h>
#include <yt/ytlib/object_client/helpers.h>
#include <yt/ytlib/object_client/object_service_proxy.h>
#include <yt/ytlib/object_client/object_ypath_proxy.h>

#include <yt/core/misc/protobuf_helpers.h>

#include <yt/core/ytree/permission.h>

namespace NYT {
namespace NChunkClient {

////////////////////////////////////////////////////////////////////////////////

template <class T>
void GetUserObjectBasicAttributes(
    NApi::IClientPtr client, 
    TMutableRange<T> objects,
    NYTree::EPermission permission,
    const NObjectClient::TTransactionId& transactionId,
    const NLogging::TLogger& logger,
    bool suppressAccessTracking)
{
    const auto& Logger = logger;

    LOG_INFO("Getting basic attributes of user objects");

    auto channel = client->GetMasterChannelOrThrow(NApi::EMasterChannelKind::LeaderOrFollower);
    NObjectClient::TObjectServiceProxy proxy(channel);

    auto batchReq = proxy.ExecuteBatch();

    for (auto iterator = objects.Begin(); iterator != objects.End(); ++iterator) {
        const auto& userObject = *iterator;
        auto req = NObjectClient::TObjectYPathProxy::GetBasicAttributes(userObject.Path.GetPath());
        req->set_permissions(static_cast<ui32>(permission));
        NCypressClient::SetTransactionId(req, transactionId);
        NCypressClient::SetSuppressAccessTracking(req, suppressAccessTracking);
        batchReq->AddRequest(req, "get_basic_attributes");
    }

    auto batchRspOrError = NConcurrency::WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(batchRspOrError, "Error getting basic attributes of user objects");
    const auto& batchRsp = batchRspOrError.Value();

    auto rspsOrError = batchRsp->GetResponses<NObjectClient::TObjectYPathProxy::TRspGetBasicAttributes>("get_basic_attributes");
    for (auto iterator = objects.Begin(); iterator != objects.End(); ++iterator) {
        auto& userObject = *iterator;
        const auto& path = userObject.Path.GetPath();
        const auto& rspOrError = rspsOrError[iterator - objects.Begin()];
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error getting basic attributes of user object %v",
            path);
        const auto& rsp = rspOrError.Value();

        userObject.ObjectId = NYT::FromProto<NObjectClient::TObjectId>(rsp->object_id());
        userObject.CellTag = rsp->cell_tag();

        userObject.Type = NObjectClient::TypeFromId(userObject.ObjectId);
        if (userObject.Type != NCypressClient::EObjectType::File && userObject.Type != NCypressClient::EObjectType::Table) {
            THROW_ERROR_EXCEPTION("Object %v has invalid type: expected %Qlv or %Qlv, actual %Qlv",
                path,
                NCypressClient::EObjectType::File,
                NCypressClient::EObjectType::Table,
                userObject.Type);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
