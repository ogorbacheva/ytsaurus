#include "scheduler_service.h"
#include "private.h"
#include "scheduler.h"

#include <yt/server/cell_scheduler/bootstrap.h>

#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/scheduler/helpers.h>
#include <yt/ytlib/scheduler/scheduler_service_proxy.h>

#include <yt/ytlib/api/native_client.h>

#include <yt/core/rpc/response_keeper.h>

#include <yt/core/ytree/permission.h>

namespace NYT {
namespace NScheduler {

using namespace NRpc;
using namespace NCellScheduler;
using namespace NApi;
using namespace NYTree;
using namespace NYson;
using namespace NCypressClient;
using namespace NSecurityClient;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////

class TSchedulerService
    : public TServiceBase
{
public:
    TSchedulerService(TBootstrap* bootstrap)
        : TServiceBase(
            bootstrap->GetControlInvoker(),
            TSchedulerServiceProxy::GetServiceName(),
            SchedulerLogger,
            TSchedulerServiceProxy::GetProtocolVersion())
        , Bootstrap_(bootstrap)
        , ResponseKeeper_(Bootstrap_->GetResponseKeeper())
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(StartOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(AbortOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(SuspendOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ResumeOperation));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(CompleteOperation));
    }

private:
    TBootstrap* const Bootstrap_;
    const TResponseKeeperPtr ResponseKeeper_;


    DECLARE_RPC_SERVICE_METHOD(NProto, StartOperation)
    {
        auto type = EOperationType(request->type());
        auto transactionId = GetTransactionId(context);
        auto mutationId = GetMutationId(context);
        const auto& user = context->GetUser();

        IMapNodePtr spec;
        try {
            spec = ConvertToNode(TYsonString(request->spec()))->AsMap();
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error parsing operation spec")
                << ex;
        }

        context->SetRequestInfo("Type: %v, TransactionId: %v",
            type,
            transactionId);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context))
            return;

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
        auto operationId = FromProto<TOperationId>(request->operation_id());
        const auto& user = context->GetUser();

        context->SetRequestInfo("OperationId: %v", operationId);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context))
            return;

        ValidatePermission(user, operationId, EPermission::Write);

        auto error = TError("Operation aborted by user request");
        if (request->has_abort_message()) {
            error = error << TError(request->abort_message());
        }

        auto operation = scheduler->GetOperationOrThrow(operationId);
        auto asyncResult = scheduler->AbortOperation(
            operation,
            error);

        context->ReplyFrom(asyncResult);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, SuspendOperation)
    {
        auto operationId = FromProto<TOperationId>(request->operation_id());
        const auto& user = context->GetUser();

        context->SetRequestInfo("OperationId: %v", operationId);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context))
            return;

        ValidatePermission(user, operationId, EPermission::Write);

        auto operation = scheduler->GetOperationOrThrow(operationId);
        auto asyncResult = scheduler->SuspendOperation(operation);

        context->ReplyFrom(asyncResult);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, ResumeOperation)
    {
        auto operationId = FromProto<TOperationId>(request->operation_id());
        const auto& user = context->GetUser();

        context->SetRequestInfo("OperationId: %v", operationId);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context))
            return;

        ValidatePermission(user, operationId, EPermission::Write);

        auto operation = scheduler->GetOperationOrThrow(operationId);
        auto asyncResult = scheduler->ResumeOperation(operation);

        context->ReplyFrom(asyncResult);
    }

    DECLARE_RPC_SERVICE_METHOD(NProto, CompleteOperation)
    {
        auto operationId = FromProto<TOperationId>(request->operation_id());
        const auto& user = context->GetUser();

        context->SetRequestInfo("OperationId: %v", operationId);

        auto scheduler = Bootstrap_->GetScheduler();
        scheduler->ValidateConnected();

        if (ResponseKeeper_->TryReplyFrom(context))
            return;

        ValidatePermission(user, operationId, EPermission::Write);

        auto operation = scheduler->GetOperationOrThrow(operationId);
        auto asyncResult = scheduler->CompleteOperation(
            operation,
            TError("Operation completed by user request"));

        context->ReplyFrom(asyncResult);
    }


    void ValidatePermission(
        const Stroka& user,
        const TOperationId& operationId,
        EPermission permission)
    {
        auto path = GetOperationPath(operationId);

        auto client = Bootstrap_->GetMasterClient();
        auto asyncResult = client->CheckPermission(user, path, permission);
        auto resultOrError = WaitFor(asyncResult);
        if (!resultOrError.IsOK()) {
            THROW_ERROR_EXCEPTION("Error checking permission for operation %v",
                operationId)
                << resultOrError;
        }

        const auto& result = resultOrError.Value();
        if (result.Action == ESecurityAction::Deny) {
            THROW_ERROR_EXCEPTION("User %Qv has been denied access to operation %v",
                user,
                operationId);
        }
    }
};

DEFINE_REFCOUNTED_TYPE(TSchedulerService)

IServicePtr CreateSchedulerService(TBootstrap* bootstrap)
{
    return New<TSchedulerService>(bootstrap);
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

