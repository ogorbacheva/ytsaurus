#pragma once

#include <yt/ytlib/auth/public.h>

#include <yt/ytlib/monitoring/public.h>

#include <yt/core/actions/public.h>

#include <yt/core/concurrency/public.h>

#include <yt/core/http/public.h>

#include <yt/core/ytree/public.h>

#include "public.h"

namespace NYT::NClickHouseProxy {

////////////////////////////////////////////////////////////////////////////////

class TBootstrap
{
public:
    TBootstrap(TClickHouseProxyServerConfigPtr config, NYTree::INodePtr configNode);
    ~TBootstrap();

    const TClickHouseProxyServerConfigPtr& GetConfig() const;
    const IInvokerPtr& GetControlInvoker() const;
    const IInvokerPtr& GetWorkerInvoker() const;
    const NAuth::ITokenAuthenticatorPtr& GetTokenAuthenticator() const;

    void Run();

private:
    const TClickHouseProxyServerConfigPtr Config_;
    const NYTree::INodePtr ConfigNode_;

    const NConcurrency::TActionQueuePtr ControlQueue_;
    const NConcurrency::TThreadPoolPtr WorkerPool_;
    const NConcurrency::IPollerPtr HttpPoller_;

    NMonitoring::TMonitoringManagerPtr MonitoringManager_;
    NHttp::IServerPtr MonitoringHttpServer_;
    NHttp::IServerPtr ClickHouseProxyServer_;
    TClickHouseProxyHandlerPtr ClickHouseProxy_;

    NAuth::TAuthenticationManagerPtr AuthenticationManager_;
    NAuth::ITokenAuthenticatorPtr TokenAuthenticator_;

    void DoRun();
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseProxy
