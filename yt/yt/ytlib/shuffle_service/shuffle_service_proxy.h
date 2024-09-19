#pragma once

#include <yt/yt/ytlib/shuffle_service/proto/shuffle_service.pb.h>

#include <yt/yt/core/rpc/client.h>

namespace NYT::NShuffle {

////////////////////////////////////////////////////////////////////////////////

class TShuffleServiceProxy
    : public NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TShuffleServiceProxy, ShuffleService);

    DEFINE_RPC_PROXY_METHOD(NProto, StartShuffle);
    DEFINE_RPC_PROXY_METHOD(NProto, FinishShuffle);
    DEFINE_RPC_PROXY_METHOD(NProto, RegisterChunks);
    DEFINE_RPC_PROXY_METHOD(NProto, FetchChunks);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NShuffle
