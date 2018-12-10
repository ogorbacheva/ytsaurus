#pragma once

#include "public.h"

#include <yt/server/exec_agent/public.h>

#include <yt/core/bus/tcp/config.h>

#include <yt/core/concurrency/public.h>

#include <yt/core/misc/proc.h>

#include <yt/core/yson/public.h>

#include <yt/ytlib/job_tracker_client/public.h>

namespace NYT::NJobProxy {

////////////////////////////////////////////////////////////////////////////////

class TJobSatelliteConnectionConfig
    : public NYTree::TYsonSerializable
{
public:
    //JobProxy -> JobSatellite connection.
    NBus::TTcpBusServerConfigPtr SatelliteRpcServerConfig;
    //Job -> JobSatellite -> JobProxy synchronization.
    NBus::TTcpBusClientConfigPtr JobProxyRpcClientConfig;
    NExecAgent::EJobEnvironmentType EnvironmentType;
    bool EnableSecureVaultVariablesInJobShell;

    TJobSatelliteConnectionConfig()
    {
        RegisterParameter("satellite_rpc_server", SatelliteRpcServerConfig)
            .DefaultNew();
        RegisterParameter("job_proxy_rpc_client", JobProxyRpcClientConfig)
            .DefaultNew();
        RegisterParameter("environment_type", EnvironmentType)
            .Default(NExecAgent::EJobEnvironmentType::Simple);
        RegisterParameter("enable_secure_vault_variables_in_job_shell", EnableSecureVaultVariablesInJobShell)
            .Default(true);
    }
};

DEFINE_REFCOUNTED_TYPE(TJobSatelliteConnectionConfig)

////////////////////////////////////////////////////////////////////////////////

class TJobSatelliteConnection
{
public:
    TJobSatelliteConnection(
        const NJobTrackerClient::TJobId& jobId,
        NBus::TTcpBusServerConfigPtr jobProxyRpcServerConfig,
        NExecAgent::EJobEnvironmentType environmentType,
        bool enableSecureVaultVariablesInJobShell);
    TString GetConfigPath() const;
    NBus::TTcpBusClientConfigPtr GetRpcClientConfig() const;
    const NJobTrackerClient::TJobId& GetJobId() const;

    void MakeConfig();

private:
    const NJobTrackerClient::TJobId JobId_;

    TString ConfigFile_;
    TJobSatelliteConnectionConfigPtr ConnectionConfig_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy
