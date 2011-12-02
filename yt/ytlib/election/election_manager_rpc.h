#pragma once

#include "common.h"
#include "election_manager_rpc.pb.h"

#include "../rpc/service.h"
#include "../rpc/client.h"

namespace NYT {
namespace NElection {

////////////////////////////////////////////////////////////////////////////////

class TElectionManagerProxy
    : public NRpc::TProxyBase
{
public:
    typedef TIntrusivePtr<TElectionManagerProxy> TPtr;

    DECLARE_ENUM(EState,
        (Stopped)
        (Voting)
        (Leading)
        (Following)
    );

    RPC_DECLARE_PROXY(ElectionManager,
        ((InvalidState)(1))
        ((InvalidLeader)(2))
        ((InvalidEpoch)(3))
    );

    TElectionManagerProxy(NRpc::IChannel* channel)
        : TProxyBase(channel, GetServiceName())
    { }

    DEFINE_RPC_PROXY_METHOD(NElection::NProto, PingFollower)
    DEFINE_RPC_PROXY_METHOD(NElection::NProto, GetStatus)

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NElection
} // namespace NYT
