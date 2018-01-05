#include "job_spec_service.h"
#include "controller_agent.h"
#include "private.h"

#include <yt/server/cell_scheduler/bootstrap.h>

#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/job_tracker_client/job_spec_service_proxy.h>

#include <yt/ytlib/scheduler/helpers.h>
#include <yt/ytlib/scheduler/scheduler_service_proxy.h>

#include <yt/ytlib/api/native_client.h>

#include <yt/core/rpc/response_keeper.h>
#include <yt/core/rpc/service_detail.h>

#include <yt/core/ytree/permission.h>

namespace NYT {
namespace NControllerAgent {

using namespace NRpc;
using namespace NCellScheduler;
using namespace NApi;
using namespace NYTree;
using namespace NYson;
using namespace NCypressClient;
using namespace NConcurrency;
using namespace NJobTrackerClient;

////////////////////////////////////////////////////////////////////

class TJobSpecService
    : public TServiceBase
{
public:
    explicit TJobSpecService(TBootstrap* bootstrap)
        : TServiceBase(
            // TODO(babenko): better queue
            bootstrap->GetControlInvoker(EControlQueue::Default),
            TJobSpecServiceProxy::GetDescriptor(),
            ControllerAgentLogger)
        , Bootstrap_(bootstrap)
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GetJobSpecs));
    }

private:
    TBootstrap* const Bootstrap_;

    DECLARE_RPC_SERVICE_METHOD(NJobTrackerClient::NProto, GetJobSpecs)
    {
        auto controllerAgent = Bootstrap_->GetControllerAgent();
        controllerAgent->ValidateConnected();

        std::vector<TJobSpecRequest> jobSpecRequests;
        for (const auto& jobSpecRequest : request->requests()) {
            jobSpecRequests.emplace_back(TJobSpecRequest{
                FromProto<TOperationId>(jobSpecRequest.operation_id()),
                FromProto<TJobId>(jobSpecRequest.job_id())
            });
        }

        auto results = WaitFor(controllerAgent->ExtractJobSpecs(jobSpecRequests))
            .ValueOrThrow();

        std::vector<TSharedRef> jobSpecs;
        results.reserve(jobSpecRequests.size());
        for (size_t index = 0; index < jobSpecRequests.size(); ++index) {
            const auto& subrequest = jobSpecRequests[index];
            const auto& subresponse = results[index];
            auto* protoSubresponse = response->add_responses();
            ToProto(protoSubresponse->mutable_error(), subresponse);
            if (subresponse.IsOK()) {
                jobSpecs.push_back(subresponse.Value());
            } else {
                LOG_DEBUG(subresponse, "Failed to extract job spec (OperationId: %v, JobId: %v)",
                    subrequest.OperationId,
                    subrequest.JobId);
                jobSpecs.emplace_back();
            }
        }

        response->Attachments() = std::move(jobSpecs);
        context->Reply();
    }
};

DEFINE_REFCOUNTED_TYPE(TJobSpecService)

IServicePtr CreateJobSpecService(TBootstrap* bootstrap)
{
    return New<TJobSpecService>(bootstrap);
}

////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT

