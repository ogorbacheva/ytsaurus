#include "scheduler_service.h"
#include "private.h"
#include "scheduler.h"
#include "bootstrap.h"

#include <yt/client/scheduler/operation_id_or_alias.h>

#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/scheduler/helpers.h>
#include <yt/ytlib/scheduler/scheduler_service_proxy.h>
#include <yt/ytlib/scheduler/config.h>

#include <yt/ytlib/api/native/client.h>

#include <yt/core/rpc/response_keeper.h>

#include <yt/core/ytree/permission.h>

namespace NYT::NScheduler {

using namespace NRpc;
using namespace NApi;
using namespace NYTree;
using namespace NYson;
using namespace NCypressClient;
using namespace NConcurrency;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

class TSchedulerService
    : public TServiceBase
{
public:
    TSchedulerService(TBootstrap* bootstrap)
        : TServiceBase(
            bootstrap->GetControlInvoker(EControlQueue::UserRequest),
            TSchedulerServiceProxy::GetDescriptor(),
            SchedulerLogger)
        , Bootstrap_(bootstrap)
        , ResponseKeeper_(Bootstrap_->GetResponseKeeper())
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(StartOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(AbortOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(SuspendOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ResumeOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(CompleteOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(UpdateOperationParameters));
    }

private:
    TBootstrap* const Bootstrap_;
    const TResponseKeeperPtr ResponseKeeper_;

    DECLARE_RPC_SERVICE_METHOD(NProto, StartOperation)
    {
        auto type = EOperationType(request->type());
        auto transactionId = GetTransactionId(context);
        auto mutationId = context->GetMutationId();
        const auto& user = context->GetUser();

        IMapNodePtr spec;
        try {
            spec = ConvertToNode(TYsonString(request->spec()))->AsMap();
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error parsing operation spec")
                << ex;
        }

        context->SetRequestInfo("Type: %v, TransactionId: %v, User: %v",
            type,
            transactionId,
            user);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context)) {
            return;
        }

        auto asyncResult = scheduler->StartOperation(
            type,
            transactionId,
            mutationId,
            spec,
            user);

        auto operation = WaitFor(asyncResult)
            .ValueOrThrow();

        auto id = operation->GetId();
        ToProto(response->mutable_operation_id(), id);

        context->SetResponseInfo("OperationId: %v", id);
        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, AbortOperation)
    {
        auto operationIdOrAlias = FromProto<TOperationIdOrAlias>(*request);

        context->SetRequestInfo("OperationId: %v",
            operationIdOrAlias);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context)) {
            return;
        }

        const auto& user = context->GetUser();

        auto error = TError("Operation aborted by user request")
            << TErrorAttribute("user", user);
        if (request->has_abort_message()) {
            error = error << TError(request->abort_message());
        }

        auto operation = scheduler->GetOperationOrThrow(operationIdOrAlias);
        auto asyncResult = scheduler->AbortOperation(
            operation,
            error,
            user);

        context->ReplyFrom(asyncResult);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, SuspendOperation)
    {
        auto operationIdOrAlias = FromProto<TOperationIdOrAlias>(*request);

        bool abortRunningJobs = request->abort_running_jobs();

        context->SetRequestInfo("OperationId: %v, AbortRunningJobs: %v",
            operationIdOrAlias,
            abortRunningJobs);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context)) {
            return;
        }

        auto operation = scheduler->GetOperationOrThrow(operationIdOrAlias);
        auto asyncResult = scheduler->SuspendOperation(
            operation,
            context->GetUser(),
            abortRunningJobs);

        context->ReplyFrom(asyncResult);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, ResumeOperation)
    {
        auto operationIdOrAlias = FromProto<TOperationIdOrAlias>(*request);

        context->SetRequestInfo("OperationId: %v",
            operationIdOrAlias);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context)) {
            return;
        }

        auto operation = scheduler->GetOperationOrThrow(operationIdOrAlias);
        auto asyncResult = scheduler->ResumeOperation(
            operation,
            context->GetUser());

        context->ReplyFrom(asyncResult);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, CompleteOperation)
    {
        auto operationIdOrAlias = FromProto<TOperationIdOrAlias>(*request);

        context->SetRequestInfo("OperationId: %v",
            operationIdOrAlias);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context)) {
            return;
        }

        auto operation = scheduler->GetOperationOrThrow(operationIdOrAlias);
        auto asyncResult = scheduler->CompleteOperation(
            operation,
            TError("Operation completed by user request"),
            context->GetUser());

        context->ReplyFrom(asyncResult);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, UpdateOperationParameters)
    {
        auto operationIdOrAlias = FromProto<TOperationIdOrAlias>(*request);

        context->SetRequestInfo("OperationId: %v",
            operationIdOrAlias);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context)) {
            return;
        }

        INodePtr parametersNode;
        try {
            parametersNode = ConvertToNode(TYsonString(request->parameters()));
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error parsing operation parameters")
                << ex;
        }

        auto operation = scheduler->GetOperationOrThrow(operationIdOrAlias);
        auto asyncResult = scheduler->UpdateOperationParameters(operation, context->GetUser(), parametersNode);
        context->ReplyFrom(asyncResult);
    }
};

IServicePtr CreateSchedulerService(TBootstrap* bootstrap)
{
    return New<TSchedulerService>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler

