#pragma once

#include "common.h"
#include "meta_state_manager.h"
#include "meta_state_manager_rpc.h"
#include "cell_manager.h"
#include "follower_tracker.h"
#include "snapshot_store.h"
#include "decorated_meta_state.h"

#include "../misc/periodic_invoker.h"

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TFollowerPinger
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TFollowerPinger> TPtr;

    struct TConfig
        : public TConfigBase
    {
        TDuration PingInterval;
        TDuration RpcTimeout;

        TConfig()
            : PingInterval(TDuration::MilliSeconds(1000))
            , RpcTimeout(TDuration::MilliSeconds(1000))
        {
            Register("ping_interval", PingInterval)
                .GreaterThan(TDuration())
                .Default(TDuration::MilliSeconds(1000));
            Register("rpc_timeout", PingInterval)
                .GreaterThan(TDuration())
                .Default(TDuration::MilliSeconds(1000));
        }
    };

    TFollowerPinger(
        const TConfig& config,
        TDecoratedMetaState::TPtr metaState,
        TCellManager::TPtr cellManager,
        TFollowerTracker::TPtr followerTracker,
        TSnapshotStore::TPtr snapshotStore,
        const TEpoch& epoch,
        IInvoker::TPtr controlInvoker);

    void Stop();

private:
    typedef TMetaStateManagerProxy TProxy;

    void SendPing();
    void OnSendPing(TProxy::TRspPingFollower::TPtr response, TPeerId followerId);

    TConfig Config;
    TPeriodicInvoker::TPtr PeriodicInvoker;
    TDecoratedMetaState::TPtr MetaState;
    TCellManager::TPtr CellManager;
    TFollowerTracker::TPtr FollowerTracker;
    TSnapshotStore::TPtr SnapshotStore;
    TEpoch Epoch;
    TCancelableInvoker::TPtr ControlInvoker;

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);
    DECLARE_THREAD_AFFINITY_SLOT(StateThread);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
