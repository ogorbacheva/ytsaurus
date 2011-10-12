#include "cypress_service.h"

#include "../ytree/yson_writer.h"

namespace NYT {
namespace NCypress {

using namespace NRpc;
using namespace NYTree;
using namespace NMetaState;
using namespace NTransaction::NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = CypressLogger;

////////////////////////////////////////////////////////////////////////////////

TCypressService::TCypressService(
    TCypressManager::TPtr cypressManager,
    TTransactionManager::TPtr transactionManager,
    IInvoker::TPtr serviceInvoker,
    NRpc::TServer::TPtr server)
    : TMetaStateServiceBase(
        serviceInvoker,
        TCypressServiceProxy::GetServiceName(),
        CypressLogger.GetCategory())
    , CypressManager(cypressManager)
    , TransactionManager(transactionManager)
{
    YASSERT(~cypressManager != NULL);
    YASSERT(~serviceInvoker != NULL);
    YASSERT(~server!= NULL);

    RegisterMethods();

    server->RegisterService(this);
}

void TCypressService::RegisterMethods()
{
    RegisterMethod(RPC_SERVICE_METHOD_DESC(Get));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(Set));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(Lock));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(Remove));
}

void TCypressService::ValidateTransactionId(const TTransactionId& transactionId)
{
    if (TransactionManager->FindTransaction(transactionId) == NULL) {
        ythrow TServiceException(EErrorCode::NoSuchTransaction) << 
            Sprintf("Invalid transaction id (TransactionId: %s)", ~transactionId.ToString());
    }
}

void TCypressService::ExecuteRecoverable(
    const TTransactionId& transactionId,
    NRpc::TServiceContext::TPtr context,
    IAction::TPtr action)
{
    ValidateTransactionId(transactionId);

    try {
        action->Do();
    } catch (const TServiceException&) {
        throw;
    } catch (...) {
        context->Reply(EErrorCode::RecoverableError);
    }
}

void TCypressService::ExecuteUnrecoverable(
    const TTransactionId& transactionId,
    NRpc::TServiceContext::TPtr context,
    IAction::TPtr action)
{
    ValidateTransactionId(transactionId);

    try {
        action->Do();
    } catch (const TServiceException&) {
        throw;
    } catch (...) {
        context->Reply(EErrorCode::UnrecoverableError);

        TMsgAbortTransaction message;
        message.SetTransactionId(transactionId.ToProto());
        CommitChange(
            TransactionManager, message,
            &TTransactionManager::AbortTransaction);
    }
}

////////////////////////////////////////////////////////////////////////////////

RPC_SERVICE_METHOD_IMPL(TCypressService, Get)
{
    UNUSED(response);

    auto transactionId = TTransactionId::FromProto(request->GetTransactionId());
    Stroka path = request->GetPath();

    context->SetRequestInfo("TransactionId: %s, Path: %s",
        ~transactionId.ToString(),
        ~path);

    ExecuteRecoverable(
        transactionId,
        context->GetUntypedContext(),
        FromMethod(
            &TCypressService::DoGet,
            TPtr(this),
            transactionId,
            path,
            context));
}

void TCypressService::DoGet(
    const TTransactionId& transactionId,
    const Stroka& path,
    TCtxGet::TPtr context)
{
    Stroka output;
    TStringOutput outputStream(output);
    TYsonWriter writer(&outputStream, false); // TODO: use binary

    CypressManager->GetYPath(transactionId, path, &writer);

    auto* response = &context->Response();
    response->SetValue(output);

    context->Reply();
}

RPC_SERVICE_METHOD_IMPL(TCypressService, Set)
{
    UNUSED(response);

    auto transactionId = TTransactionId::FromProto(request->GetTransactionId());
    Stroka path = request->GetPath();
    Stroka value = request->GetValue();

    context->SetRequestInfo("TransactionId: %s, Path: %s",
        ~transactionId.ToString(),
        ~path);

    ExecuteUnrecoverable(
        transactionId,
        context->GetUntypedContext(),
        FromMethod(
            &TCypressService::DoSet,
            TPtr(this),
            transactionId,
            path,
            value,
            context));
}

void TCypressService::DoSet(
    const TTransactionId& transactionId,
    const Stroka& path,
    const Stroka& value,
    TCtxSet::TPtr context)
{
    NProto::TMsgSet message;
    message.SetTransactionId(transactionId.ToProto());
    message.SetPath(path);
    message.SetValue(value);

    CommitChange(
        this, context, CypressManager, message,
        &TCypressManager::SetYPath,
        ECommitMode::MayFail);
}

RPC_SERVICE_METHOD_IMPL(TCypressService, Remove)
{
    UNUSED(response);

    auto transactionId = TTransactionId::FromProto(request->GetTransactionId());
    Stroka path = request->GetPath();

    context->SetRequestInfo("TransactionId: %s, Path: %s",
        ~transactionId.ToString(),
        ~path);

    ExecuteRecoverable(
        transactionId,
        context->GetUntypedContext(),
        FromMethod(
            &TCypressService::DoRemove,
            TPtr(this),
            transactionId,
            path,
            context));
}

void TCypressService::DoRemove(
    const TTransactionId& transactionId,
    const Stroka& path,
    TCtxRemove::TPtr context)
{
    NProto::TMsgRemove message;
    message.SetTransactionId(transactionId.ToProto());
    message.SetPath(path);

    CommitChange(
        this, context, CypressManager, message,
        &TCypressManager::RemoveYPath,
        ECommitMode::MayFail);
}

RPC_SERVICE_METHOD_IMPL(TCypressService, Lock)
{
    UNUSED(response);

    auto transactionId = TTransactionId::FromProto(request->GetTransactionId());
    Stroka path = request->GetPath();

    context->SetRequestInfo("TransactionId: %s, Path: %s",
        ~transactionId.ToString(),
        ~path);

    ExecuteRecoverable(
        transactionId,
        context->GetUntypedContext(),
        FromMethod(
            &TCypressService::DoLock,
            TPtr(this),
            transactionId,
            path,
            context));
}

void TCypressService::DoLock(
    const TTransactionId& transactionId,
    const Stroka& path,
    TCtxLock::TPtr context)
{
    NProto::TMsgLock message;
    message.SetTransactionId(transactionId.ToProto());
    message.SetPath(path);

    CommitChange(
        this, context, CypressManager, message,
        &TCypressManager::LockYPath,
        ECommitMode::MayFail);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT
