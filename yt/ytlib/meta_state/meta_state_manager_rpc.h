#pragma once

#include "common.h"
#include "meta_state_manager_rpc.pb.h"

#include "../rpc/service.h"
#include "../rpc/client.h"

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TMetaStateManagerProxy
    : public NRpc::TProxyBase
{
public:
    typedef TIntrusivePtr<TMetaStateManagerProxy> TPtr;

    RPC_DECLARE_PROXY(MetaStateManager,
        ((InvalidSegmentId)(1))
        ((InvalidEpoch)(2))
        ((InvalidVersion)(3))
        ((InvalidStatus)(4))
        ((IOError)(5))
        ((Busy)(6))
    );

    TMetaStateManagerProxy(NRpc::IChannel* channel)
        : TProxyBase(channel, GetServiceName())
    { }

    DEFINE_RPC_PROXY_METHOD(NMetaState::NProto, ReadSnapshot);
    DEFINE_RPC_PROXY_METHOD(NMetaState::NProto, ReadChangeLog);
    DEFINE_RPC_PROXY_METHOD(NMetaState::NProto, GetSnapshotInfo);
    DEFINE_RPC_PROXY_METHOD(NMetaState::NProto, GetChangeLogInfo);
    DEFINE_RPC_PROXY_METHOD(NMetaState::NProto, ApplyChanges);
    DEFINE_RPC_PROXY_METHOD(NMetaState::NProto, AdvanceSegment);
    DEFINE_RPC_PROXY_METHOD(NMetaState::NProto, PingFollower);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
