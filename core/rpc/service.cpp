#include "service.h"

namespace NYT::NRpc {

////////////////////////////////////////////////////////////////////////////////

namespace {

TError MakeCanceledError()
{
    return TError("RPC request canceled");
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

void IServiceContext::SetRequestInfo()
{
    SetRawRequestInfo(TString(), false);
}

void IServiceContext::SetResponseInfo()
{
    SetRawResponseInfo(TString(), false);
}

void IServiceContext::ReplyFrom(TFuture<TSharedRefArray> asyncMessage)
{
    asyncMessage.Subscribe(BIND([=, this_ = MakeStrong(this)] (const TErrorOr<TSharedRefArray>& result) {
        if (result.IsOK()) {
            Reply(result.Value());
        } else {
            Reply(TError(result));
        }
    }));
    SubscribeCanceled(BIND([asyncMessage = std::move(asyncMessage)] {
        asyncMessage.Cancel(MakeCanceledError());
    }));
}

void IServiceContext::ReplyFrom(TFuture<void> asyncError)
{
    asyncError.Subscribe(BIND([=, this_ = MakeStrong(this)] (const TError& error) {
        Reply(error);
    }));
    SubscribeCanceled(BIND([asyncError = std::move(asyncError)] {
        asyncError.Cancel(MakeCanceledError());
    }));
}

////////////////////////////////////////////////////////////////////////////////

TServiceId::TServiceId(const TString& serviceName, TRealmId realmId)
    : ServiceName(serviceName)
    , RealmId(realmId)
{ }

bool operator == (const TServiceId& lhs, const TServiceId& rhs)
{
    return lhs.ServiceName == rhs.ServiceName && lhs.RealmId == rhs.RealmId;
}

bool operator != (const TServiceId& lhs, const TServiceId& rhs)
{
    return !(lhs == rhs);
}

TString ToString(const TServiceId& serviceId)
{
    auto result = serviceId.ServiceName;
    if (!serviceId.RealmId.IsEmpty()) {
        result.append(':');
        result.append(ToString(serviceId.RealmId));
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc
