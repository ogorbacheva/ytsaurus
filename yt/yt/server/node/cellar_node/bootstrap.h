#pragma once

#include "public.h"

#include <yt/yt/server/node/cluster_node/bootstrap.h>

#include <yt/yt/server/lib/security_server/public.h>

namespace NYT::NCellarNode {

////////////////////////////////////////////////////////////////////////////////

struct IBootstrap
    : public virtual NClusterNode::IBootstrapBase
{
    virtual ~IBootstrap() = default;

    virtual void Initialize() = 0;
    virtual void Run() = 0;

    virtual const IInvokerPtr& GetTransactionTrackerInvoker() const = 0;

    virtual const NSecurityServer::IResourceLimitsManagerPtr& GetResourceLimitsManager() const = 0;

    virtual const NCellarAgent::ICellarManagerPtr& GetCellarManager() const = 0;

    virtual const NCellarNode::IMasterConnectorPtr& GetMasterConnector() const = 0;

    virtual void ScheduleCellarHeartbeat(bool immediately) const = 0;
};

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IBootstrap> CreateBootstrap(NClusterNode::IBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellarNode
