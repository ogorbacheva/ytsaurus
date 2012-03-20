#pragma once

#include "public.h"

#include <ytlib/election/leader_lookup.h>
#include <ytlib/transaction_client/transaction_manager.h>

namespace NYT {
namespace NCellScheduler {

////////////////////////////////////////////////////////////////////////////////

struct TCellSchedulerConfig
    : public TConfigurable
{
    //! RPC interface port number.
    int RpcPort;

    //! HTTP monitoring interface port number.
    int MonitoringPort;

    NElection::TLeaderLookup::TConfig::TPtr Masters;
    NTransactionClient::TTransactionManager::TConfig::TPtr Transactions;

    TCellSchedulerConfig()
    {
        Register("rpc_port", RpcPort)
            .Default(11000);
        Register("monitoring_port", MonitoringPort)
            .Default(10000);
        Register("masters", Masters);
        Register("transactions", Transactions)
            .DefaultNew();
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellScheduler
} // namespace NYT
