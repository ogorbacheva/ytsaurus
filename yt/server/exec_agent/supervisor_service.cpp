#include "stdafx.h"
#include "supervisor_service.h"
#include "supervisor_service_proxy.h"
#include "job.h"
#include "private.h"

#include <ytlib/node_tracker_client/helpers.h>

#include <server/job_agent/job_controller.h>

#include <server/cell_node/bootstrap.h>

namespace NYT {
namespace NExecAgent {

using namespace NJobAgent;
using namespace NNodeTrackerClient;
using namespace NCellNode;

////////////////////////////////////////////////////////////////////////////////

TSupervisorService::TSupervisorService(TBootstrap* bootstrap)
    : NRpc::TServiceBase(
        bootstrap->GetControlInvoker(),
        TSupervisorServiceProxy::GetServiceName(),
        ExecAgentLogger.GetCategory())
    , Bootstrap(bootstrap)
{
    RegisterMethod(
        RPC_SERVICE_METHOD_DESC(GetJobSpec)
        .SetResponseHeavy(true));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(OnJobFinished));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(OnJobProgress)
        .SetOneWay(true));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(UpdateResourceUsage)
        .SetOneWay(true));
}

DEFINE_RPC_SERVICE_METHOD(TSupervisorService, GetJobSpec)
{
    auto jobId = FromProto<TJobId>(request->job_id());
    context->SetRequestInfo("JobId: %s", ~ToString(jobId));

    auto jobController = Bootstrap->GetJobController();
    auto job = jobController->GetJobOrThrow(jobId);

    *response->mutable_job_spec() = job->GetSpec();
    *response->mutable_resource_usage() = job->GetResourceUsage();

    context->Reply();
}

DEFINE_RPC_SERVICE_METHOD(TSupervisorService, OnJobFinished)
{
    auto jobId = FromProto<TJobId>(request->job_id());
    const auto& result = request->result();
    auto error = FromProto(result.error());
    context->SetRequestInfo("JobId: %s, Error: %s",
        ~ToString(jobId),
        ~ToString(error));

    auto jobController = Bootstrap->GetJobController();
    auto job = jobController->GetJobOrThrow(jobId);

    jobController->SetJobResult(job, result);

    context->Reply();
}

DEFINE_ONE_WAY_RPC_SERVICE_METHOD(TSupervisorService, OnJobProgress)
{
    auto jobId = FromProto<TJobId>(request->job_id());
    double progress = request->progress();

    context->SetRequestInfo("JobId: %s, Progress: %lf",
        ~ToString(jobId),
        progress);

    auto jobController = Bootstrap->GetJobController();
    auto job = jobController->GetJobOrThrow(jobId);

    jobController->UpdateJobProgress(job, progress);
}

DEFINE_ONE_WAY_RPC_SERVICE_METHOD(TSupervisorService, UpdateResourceUsage)
{
    auto jobId = FromProto<TJobId>(request->job_id());
    const auto& resourceUsage = request->resource_usage();

    context->SetRequestInfo("JobId: %s, ResourceUsage: {%s}",
        ~ToString(jobId),
        ~FormatResources(resourceUsage));

    auto jobController = Bootstrap->GetJobController();
    auto job = jobController->GetJobOrThrow(jobId);

    jobController->UpdateJobResourceUsage(job, resourceUsage);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NExecAgent
} // namespace NYT
