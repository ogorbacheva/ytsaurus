#include "stdafx.h"
#include "scheduler_channel.h"
#include "config.h"

#include <ytlib/object_client/object_service_proxy.h>

#include <ytlib/ytree/ypath_proxy.h>

#include <ytlib/bus/config.h>
#include <ytlib/bus/tcp_client.h>

#include <ytlib/rpc/retrying_channel.h>
#include <ytlib/rpc/roaming_channel.h>
#include <ytlib/rpc/bus_channel.h>

namespace NYT {
namespace NScheduler {

using namespace NBus;
using namespace NRpc;
using namespace NObjectClient;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

namespace {

TErrorOr<IChannelPtr> OnSchedulerAddressFound(TYPathProxy::TRspGetPtr rsp)
{
    if (!rsp->IsOK()) {
        return rsp->GetError();
    }

    auto address = ConvertTo<Stroka>(TYsonString(rsp->value()));
    auto config = New<TTcpBusClientConfig>(address);
    // TODO(babenko): get rid of this hardcoded priority
    config->Priority = 6;
    auto client = CreateTcpBusClient(config);
    return CreateBusChannel(client);
}

} // namespace

IChannelPtr CreateSchedulerChannel(
    TSchedulerConnectionConfigPtr config,
    IChannelPtr masterChannel)
{
    auto roamingChannel = CreateRoamingChannel(
        config->RpcTimeout,
        BIND([=] () -> TFuture< TErrorOr<IChannelPtr> > {
            TObjectServiceProxy proxy(masterChannel);
            auto req = TYPathProxy::Get("//sys/scheduler/@address");
            return proxy.Execute(req).Apply(BIND(&OnSchedulerAddressFound));
        }));
    return CreateRetryingChannel(config, roamingChannel);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
