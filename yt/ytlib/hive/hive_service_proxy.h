#pragma once

#include "public.h"

#include <yt/ytlib/hive/hive_service.pb.h>

#include <yt/core/rpc/client.h>

namespace NYT {
namespace NHiveClient {

////////////////////////////////////////////////////////////////////////////////

class THiveServiceProxy
    : public NRpc::TProxyBase
{
public:
    static Stroka GetServiceName()
    {
        return "HiveService";
    }

    static int GetProtocolVersion()
    {
        return 1;
    }

    explicit THiveServiceProxy(NRpc::IChannelPtr channel)
        : TProxyBase(channel, GetServiceName(), GetProtocolVersion())
    { }

    DEFINE_RPC_PROXY_METHOD(NProto, Ping);
    DEFINE_RPC_PROXY_METHOD(NProto, SyncCells);
    DEFINE_RPC_PROXY_METHOD(NProto, PostMessages);
    DEFINE_RPC_PROXY_METHOD(NProto, SendMessages);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NHiveClient
} // namespace NYT
