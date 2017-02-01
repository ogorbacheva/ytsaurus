#pragma once

#include "public.h"

#include <yt/core/bus/config.h>

#include <yt/core/concurrency/public.h>

#include <yt/core/misc/proc.h>

#include <yt/core/yson/public.h>

#include <yt/ytlib/job_tracker_client/public.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////////////////

class TJobSatelliteConnectionConfig
    : public NYTree::TYsonSerializable
{
public:
    //JobProxy -> JobSatellite connection.
    NBus::TTcpBusServerConfigPtr SatelliteRpcServerConfig;
    //Job -> JobSatellite -> JobProxy synchronization.
    NBus::TTcpBusClientConfigPtr JobProxyRpcClientConfig;

    TJobSatelliteConnectionConfig()
    {
        RegisterParameter("satellite_rpc_server", SatelliteRpcServerConfig)
            .DefaultNew();
        RegisterParameter("job_proxy_rpc_client", JobProxyRpcClientConfig)
            .DefaultNew();
    }
};

DEFINE_REFCOUNTED_TYPE(TJobSatelliteConnectionConfig)

////////////////////////////////////////////////////////////////////////////////

class TJobSatelliteConnection
{
public:
    TJobSatelliteConnection(
        const NJobTrackerClient::TJobId& jobId,
        NBus::TTcpBusServerConfigPtr jobProxyRpcServerConfig);

    Stroka GetConfigPath() const;
    NBus::TTcpBusClientConfigPtr GetRpcClientConfig() const;
    const NJobTrackerClient::TJobId& GetJobId() const;

    void MakeConfig();

private:
    const NJobTrackerClient::TJobId JobId_;

    Stroka ConfigFile_;
    TJobSatelliteConnectionConfigPtr ConnectionConfigPtr_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
