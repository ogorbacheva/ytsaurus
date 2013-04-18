#pragma once

#include "public.h"

#include <ytlib/misc/periodic_invoker.h>

#include <ytlib/job_tracker_client/job_tracker_service_proxy.h>

#include <server/cell_node/public.h>

namespace NYT {
namespace NExecAgent {

////////////////////////////////////////////////////////////////////////////////

class TSchedulerConnector
    : public TRefCounted
{
public:
    TSchedulerConnector(
        TSchedulerConnectorConfigPtr config,
        NCellNode::TBootstrap* bootstrap);

    void Start();

private:
    typedef TSchedulerConnector TThis;

    TSchedulerConnectorConfigPtr Config;
    NCellNode::TBootstrap* Bootstrap;
    IInvokerPtr ControlInvoker;

    TPeriodicInvokerPtr HeartbeatInvoker;

    void SendHeartbeat();
    void OnHeartbeatResponse(NJobTrackerClient::TJobTrackerServiceProxy::TRspHeartbeatPtr rsp);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NExecAgent
} // namespace NYT
