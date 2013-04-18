#include "stdafx.h"
#include "master_connector.h"
#include "scheduler.h"
#include "private.h"
#include "helpers.h"
#include "snapshot_builder.h"

#include <ytlib/misc/periodic_invoker.h>
#include <ytlib/misc/thread_affinity.h>
#include <ytlib/misc/delayed_invoker.h>
#include <ytlib/misc/address.h>

#include <ytlib/actions/async_pipeline.h>
#include <ytlib/actions/parallel_awaiter.h>
#include <ytlib/actions/action_queue.h>

#include <ytlib/rpc/serialized_channel.h>

#include <ytlib/transaction_client/transaction_manager.h>
#include <ytlib/transaction_client/transaction.h>
#include <ytlib/transaction_client/transaction_ypath_proxy.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>

#include <ytlib/yson/yson_consumer.h>

#include <ytlib/ytree/ypath_proxy.h>
#include <ytlib/ytree/fluent.h>
#include <ytlib/ytree/node.h>

#include <ytlib/scheduler/helpers.h>

#include <ytlib/meta_state/rpc_helpers.h>

#include <ytlib/security_client/public.h>

#include <ytlib/object_client/master_ypath_proxy.h>

#include <server/cell_scheduler/bootstrap.h>

namespace NYT {
namespace NScheduler {

using namespace NYTree;
using namespace NYson;
using namespace NCypressClient;
using namespace NObjectClient;
using namespace NChunkClient;
using namespace NTransactionClient;
using namespace NMetaState;
using namespace NRpc;
using namespace NSecurityClient;
using namespace NTransactionClient::NProto;

////////////////////////////////////////////////////////////////////

static NLog::TLogger& SILENT_UNUSED Logger = SchedulerLogger;

////////////////////////////////////////////////////////////////////

class TMasterConnector::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TSchedulerConfigPtr config,
        NCellScheduler::TBootstrap* bootstrap)
        : Config(config)
        , Bootstrap(bootstrap)
        , Proxy(Bootstrap->GetMasterChannel())
        , Connected(false)
    { }

    void Start()
    {
        Bootstrap->GetControlInvoker()->Invoke(BIND(
            &TImpl::StartConnecting,
            MakeStrong(this)));
    }

    bool IsConnected() const
    {
        return Connected;
    }


    TAsyncError CreateOperationNode(TOperationPtr operation)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        auto id = operation->GetOperationId();
        LOG_INFO("Creating operation node (OperationId: %s)",
            ~ToString(id));

        auto* list = CreateUpdateList(operation);

        auto batchReq = StartBatchRequest(list);
        {
            auto req = TYPathProxy::Set(GetOperationPath(id));
            TYsonProducer producer(BIND(&TImpl::BuildOperationNode, operation));
            req->set_value(ConvertToYsonString(producer).Data());
            GenerateRpcMutationId(req);
            batchReq->AddRequest(req);
        }

        return batchReq->Invoke().Apply(
            BIND(
                &TImpl::OnOperationNodeCreated,
                MakeStrong(this),
                operation,
                CancelableContext)
            .AsyncVia(Bootstrap->GetControlInvoker()));
    }

    TAsyncError ResetRevivingOperationNode(TOperationPtr operation)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);
        YCHECK(operation->GetState() == EOperationState::Reviving);

        auto id = operation->GetOperationId();
        LOG_INFO("Resetting reviving operation node (OperationId: %s)",
            ~ToString(id));

        auto* list = GetUpdateList(operation);

        auto batchReq = StartBatchRequest(list);
        {
            auto req = TYPathProxy::Set(GetOperationPath(id) + "/@");
            TYsonProducer producer(BIND(&TImpl::BuildRevivingOperationAttributes, operation));
            req->set_value(ConvertToYsonString(producer).Data());
            GenerateRpcMutationId(req);
            batchReq->AddRequest(req);
        }

        return batchReq->Invoke().Apply(
            BIND(
                &TImpl::OnRevivingOperationNodeReset,
                MakeStrong(this),
                operation)
            .AsyncVia(Bootstrap->GetControlInvoker()));
    }

    TFuture<void> FlushOperationNode(TOperationPtr operation)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        auto id = operation->GetOperationId();
        LOG_INFO("Flushing operation node (OperationId: %s)",
            ~ToString(id));

        auto* list = GetUpdateList(operation);
        list->State = EUpdateListState::Flushing;

        // Create a batch update for this particular operation.
        auto batchReq = StartBatchRequest(list);
        PrepareOperationUpdate(list, batchReq);

        batchReq->Invoke().Apply(
            BIND(&TImpl::OnOperationNodeFlushed, MakeStrong(this), operation)
                .Via(CancelableControlInvoker));

        return list->FlushedPromise;
    }

    TFuture<void> FinalizeOperationNode(TOperationPtr operation)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        auto id = operation->GetOperationId();
        LOG_INFO("Finalizing operation node (OperationId: %s)",
            ~ToString(id));

        auto* list = GetUpdateList(operation);
        list->State = EUpdateListState::Finalizing;

        // Create a batch update for this particular operation.
        auto batchReq = StartBatchRequest(list);
        PrepareOperationUpdate(list, batchReq);

        batchReq->Invoke().Subscribe(
            BIND(&TImpl::OnOperationNodeFinalized, MakeStrong(this), operation)
                .Via(CancelableControlInvoker));

        return list->FinalizedPromise;
    }

    void CreateJobNode(TJobPtr job, const TChunkId& stdErrChunkId)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        LOG_DEBUG("Creating job node (OperationId: %s, JobId: %s, StdErrChunkId: %s)",
            ~ToString(job->GetOperation()->GetOperationId()),
            ~ToString(job->GetId()),
            ~ToString(stdErrChunkId));

        auto* list = GetUpdateList(job->GetOperation());
        list->PendingJobs.insert(std::make_pair(job, stdErrChunkId));
    }


    void AddGlobalWatcherRequester(TWatcherRequester requester)
    {
        GlobalWatcherRequesters.push_back(requester);
    }

    void AddGlobalWatcherHandler(TWatcherHandler handler)
    {
        GlobalWatcherHandlers.push_back(handler);
    }


    void AddOperationWatcherRequester(TOperationPtr operation, TWatcherRequester requester)
    {
        auto* list = GetOrCreateWatcherList(operation);
        list->WatcherRequesters.push_back(requester);
    }

    void AddOperationWatcherHandler(TOperationPtr operation, TWatcherHandler handler)
    {
        auto* list = GetOrCreateWatcherList(operation);
        list->WatcherHandlers.push_back(handler);
    }


    DEFINE_SIGNAL(void(const TMasterHandshakeResult& result), MasterConnected);
    DEFINE_SIGNAL(void(), MasterDisconnected);

    DEFINE_SIGNAL(void(TOperationPtr operation), UserTransactionAborted);
    DEFINE_SIGNAL(void(TOperationPtr operation), SchedulerTransactionAborted);

private:
    TSchedulerConfigPtr Config;
    NCellScheduler::TBootstrap* Bootstrap;

    TObjectServiceProxy Proxy;

    TCancelableContextPtr CancelableContext;
    IInvokerPtr CancelableControlInvoker;

    bool Connected;

    NTransactionClient::ITransactionPtr LockTransaction;

    TPeriodicInvokerPtr TransactionRefreshInvoker;
    TPeriodicInvokerPtr OperationNodesUpdateInvoker;
    TPeriodicInvokerPtr WatchersInvoker;
    TPeriodicInvokerPtr SnapshotInvoker;

    std::vector<TWatcherRequester> GlobalWatcherRequesters;
    std::vector<TWatcherHandler>   GlobalWatcherHandlers;

    DECLARE_ENUM(EUpdateListState,
        (Active)
        (Flushing)
        (Flushed)
        (Finalizing)
        (Finalized)
    );

    struct TUpdateList
    {
        TUpdateList(IChannelPtr masterChannel, TOperationPtr operation)
            : Operation(operation)
            , State(EUpdateListState::Active)
            , FlushedPromise(NewPromise<void>())
            , FinalizedPromise(NewPromise<void>())
            , Proxy(CreateSerializedChannel(masterChannel))
        { }

        TOperationPtr Operation;
        yhash_map<TJobPtr, TChunkId> PendingJobs;
        EUpdateListState State;
        TPromise<void> FlushedPromise;
        TPromise<void> FinalizedPromise;
        TObjectServiceProxy Proxy;
    };

    yhash_map<TOperationId, TUpdateList> UpdateLists;

    struct TWatcherList
    {
        explicit TWatcherList(TOperationPtr operation)
            : Operation(operation)
        { }

        TOperationPtr Operation;
        std::vector<TWatcherRequester> WatcherRequesters;
        std::vector<TWatcherHandler>   WatcherHandlers;
    };

    yhash_map<TOperationId, TWatcherList> WatcherLists;

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);


    void StartConnecting()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        LOG_INFO("Connecting to master");

        New<TRegistrationPipeline>(this)
            ->Create()
            ->Run()
            .Subscribe(BIND(&TImpl::OnConnected, MakeStrong(this))
                .Via(Bootstrap->GetControlInvoker()));
    }

    void OnConnected(TValueOrError<TMasterHandshakeResult> resultOrError)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        if (!resultOrError.IsOK()) {
            LOG_ERROR(resultOrError, "Error connecting to master");
            TDelayedInvoker::Submit(
                BIND(&TImpl::StartConnecting, MakeStrong(this))
                    .Via(Bootstrap->GetControlInvoker()),
                Config->ConnectRetryPeriod);
            return;
        }

        LOG_INFO("Master connected");

        YCHECK(!Connected);
        Connected = true;

        CancelableContext = New<TCancelableContext>();
        CancelableControlInvoker = CancelableContext->CreateInvoker(Bootstrap->GetControlInvoker());

        const auto& result = resultOrError.Value();
        FOREACH (auto operation, result.Operations) {
            CreateUpdateList(operation);
        }
        FOREACH (auto handler, GlobalWatcherHandlers) {
            handler.Run(result.WatcherResponses);
        }

        LockTransaction->SubscribeAborted(
            BIND(&TImpl::OnLockTransactionAborted, MakeWeak(this))
                .Via(CancelableControlInvoker));

        StartRefresh();
        StartSnapshots();

        MasterConnected_.Fire(result);
    }

    void OnLockTransactionAborted()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        LOG_WARNING("Lock transaction aborted");

        Disconnect();
    }


    class TRegistrationPipeline
        : public TRefCounted
    {
    public:
        explicit TRegistrationPipeline(TIntrusivePtr<TImpl> owner)
            : Owner(owner)
        { }

        TAsyncPipeline<TMasterHandshakeResult>::TPtr Create()
        {
            return StartAsyncPipeline(Owner->Bootstrap->GetControlInvoker())
                ->Add(BIND(&TRegistrationPipeline::Round1, MakeStrong(this)))
                ->Add(BIND(&TRegistrationPipeline::Round2, MakeStrong(this)))
                ->Add(BIND(&TRegistrationPipeline::Round3, MakeStrong(this)))
                ->Add(BIND(&TRegistrationPipeline::Round4, MakeStrong(this)))
                ->Add(BIND(&TRegistrationPipeline::Round5, MakeStrong(this)))
                ->Add(BIND(&TRegistrationPipeline::Round6, MakeStrong(this)))
                ->Add(BIND(&TRegistrationPipeline::Round7, MakeStrong(this)))
                ->Add(BIND(&TRegistrationPipeline::Round8, MakeStrong(this)));
        }

    private:
        TIntrusivePtr<TImpl> Owner;
        std::vector<TOperationId> OperationIds;
        TMasterHandshakeResult Result;

        // Round 1:
        // - Start lock transaction.
        TObjectServiceProxy::TInvExecuteBatch Round1()
        {
            auto batchReq = Owner->StartBatchRequest(false);
            {
                auto req = TMasterYPathProxy::CreateObject();
                req->set_type(EObjectType::Transaction);

                auto* reqExt = req->MutableExtension(TReqCreateTransactionExt::create_transaction_ext);
                reqExt->set_timeout(Owner->Config->LockTransactionTimeout.MilliSeconds());

                auto attributes = CreateEphemeralAttributes();
                attributes->Set("title", Sprintf("Scheduler lock at %s", ~TAddressResolver::Get()->GetLocalHostName()));
                ToProto(req->mutable_object_attributes(), *attributes);

                GenerateRpcMutationId(req);
                batchReq->AddRequest(req, "start_lock_tx");
            }
            return batchReq->Invoke();
        }

        // Round 2:
        // - Take lock.
        TObjectServiceProxy::TInvExecuteBatch Round2(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
        {
            THROW_ERROR_EXCEPTION_IF_FAILED(*batchRsp);
            {
                auto rsp = batchRsp->GetResponse<TMasterYPathProxy::TRspCreateObject>("start_lock_tx");
                THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error starting lock transaction");
                auto transactionId = FromProto<TTransactionId>(rsp->object_id());

                TTransactionAttachOptions options(transactionId);
                options.AutoAbort = true;
                auto transactionManager = Owner->Bootstrap->GetTransactionManager();
                Owner->LockTransaction = transactionManager->Attach(options);

                LOG_INFO("Lock transaction is %s", ~ToString(transactionId));
            }

            auto batchReq = Owner->StartBatchRequest();
            {
                auto req = TCypressYPathProxy::Lock("//sys/scheduler/lock");
                SetTransactionId(req, Owner->LockTransaction);
                req->set_mode(ELockMode::Exclusive);
                GenerateRpcMutationId(req);
                batchReq->AddRequest(req, "take_lock");
            }
            return batchReq->Invoke();
        }

        // Round 3:
        // - Publish scheduler address.
        // - Update orchid address.
        // - Request operations and their states.
        TObjectServiceProxy::TInvExecuteBatch Round3(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
        {
            THROW_ERROR_EXCEPTION_IF_FAILED(batchRsp->GetCumulativeError());

            auto batchReq = Owner->StartBatchRequest();
            auto schedulerAddress = Owner->Bootstrap->GetLocalAddress();
            {
                auto req = TYPathProxy::Set("//sys/scheduler/@address");
                req->set_value(ConvertToYsonString(TRawString(schedulerAddress)).Data());
                GenerateRpcMutationId(req);
                batchReq->AddRequest(req, "set_scheduler_address");
            }
            {
                auto req = TYPathProxy::Set("//sys/scheduler/orchid&/@remote_address");
                req->set_value(ConvertToYsonString(TRawString(schedulerAddress)).Data());
                GenerateRpcMutationId(req);
                batchReq->AddRequest(req, "set_orchid_address");
            }
            {
                auto req = TYPathProxy::List("//sys/operations");
                auto* attributeFilter = req->mutable_attribute_filter();
                attributeFilter->set_mode(EAttributeFilterMode::MatchingOnly);
                attributeFilter->add_keys("state");
                batchReq->AddRequest(req, "list_operations");
            }
            return batchReq->Invoke();
        }

        // Round 4:
        // - Request attributes for unfinished operations.
        TObjectServiceProxy::TInvExecuteBatch Round4(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
        {
            THROW_ERROR_EXCEPTION_IF_FAILED(*batchRsp);
            {
                auto rsp = batchRsp->GetResponse<TYPathProxy::TRspList>("list_operations");
                THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error getting operations list");
                auto operationsListNode = ConvertToNode(TYsonString(rsp->keys()));
                auto operationsList = operationsListNode->AsList();
                LOG_INFO("Operations list received, %d operations total",
                    static_cast<int>(operationsList->GetChildCount()));
                OperationIds.clear();
                FOREACH (auto operationNode, operationsList->GetChildren()) {
                    auto id = TOperationId::FromString(operationNode->GetValue<Stroka>());
                    auto state = operationNode->Attributes().Get<EOperationState>("state");
                    if (IsOperationInProgress(state)) {
                        OperationIds.push_back(id);
                    }
                }
            }

            auto batchReq = Owner->StartBatchRequest();
            {
                LOG_INFO("Fetching attributes for %d unfinished operations",
                    static_cast<int>(OperationIds.size()));
                FOREACH (const auto& operationId, OperationIds) {
                    auto req = TYPathProxy::Get(GetOperationPath(operationId));
                    // Keep in sync with CreateOperationFromAttributes.
                    auto* attributeFilter = req->mutable_attribute_filter();
                    attributeFilter->set_mode(EAttributeFilterMode::MatchingOnly);
                    attributeFilter->add_keys("operation_type");
                    attributeFilter->add_keys("user_transaction_id");
                    attributeFilter->add_keys("sync_scheduler_transaction_id");
                    attributeFilter->add_keys("async_scheduler_transaction_id");
                    attributeFilter->add_keys("input_transaction_id");
                    attributeFilter->add_keys("output_transaction_id");
                    attributeFilter->add_keys("spec");
                    attributeFilter->add_keys("authenticated_user");
                    attributeFilter->add_keys("start_time");
                    attributeFilter->add_keys("state");
                    batchReq->AddRequest(req, "get_op_attr");
                }
            }
            return batchReq->Invoke();
        }

        // Round 5:
        // - Recreate operation instance from fetched data.
        // - Try to ping the previous incarnations of scheduler transactions.
        static const int TransactionsPerOperation = 5;

        TObjectServiceProxy::TInvExecuteBatch Round5(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
        {
            THROW_ERROR_EXCEPTION_IF_FAILED(*batchRsp);

            {
                auto rsps = batchRsp->GetResponses<TYPathProxy::TRspGet>("get_op_attr");
                YCHECK(rsps.size() == OperationIds.size());

                for (int index = 0; index < static_cast<int>(rsps.size()); ++index) {
                    const auto& operationId = OperationIds[index];
                    auto rsp = rsps[index];
                    THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error getting operation attributes (OperationId: %s)",
                        ~ToString(operationId));
                    auto operationNode = ConvertToNode(TYsonString(rsp->value()));
                    auto operation = Owner->CreateOperationFromAttributes(operationId, operationNode->Attributes());
                    Result.Operations.push_back(operation);
                }
            }

            auto batchReq = Owner->StartBatchRequest();
            FOREACH (auto operation, Result.Operations) {
                operation->SetState(EOperationState::Reviving);

                auto schedulePing = [&] (ITransactionPtr transaction) {
                    if (transaction) {
                        auto req = TTransactionYPathProxy::Ping(FromObjectId(transaction->GetId()));
                        batchReq->AddRequest(req, "ping_tx");
                    } else {
                        batchReq->AddRequest(nullptr, "ping_tx");
                    }
                };

                // See TransactionsPerOperation above.
                schedulePing(operation->GetUserTransaction());
                schedulePing(operation->GetSyncSchedulerTransaction());
                schedulePing(operation->GetAsyncSchedulerTransaction());
                schedulePing(operation->GetInputTransaction());
                schedulePing(operation->GetOutputTransaction());
            }

            return batchReq->Invoke();
        }

        // Round 6:
        // - Check ping responses.
        // - If some of them have failed then abort all operations transactions and also
        //   remove the snapshot.
        TObjectServiceProxy::TInvExecuteBatch Round6(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
        {
            THROW_ERROR_EXCEPTION_IF_FAILED(*batchRsp);

            {
                auto rsps = batchRsp->GetResponses<TTransactionYPathProxy::TRspPing>("ping_tx");
                YCHECK(rsps.size() == 5 * Result.Operations.size());

                for (int i = 0; i < static_cast<int>(Result.Operations.size()); ++i) {
                    auto operation = Result.Operations[i];
                    for (int j = i * TransactionsPerOperation; j < (i + 1) * TransactionsPerOperation; ++j) {
                        auto rsp = rsps[j];
                        if (rsp && !rsp->IsOK() && !operation->GetCleanStart()) {
                            LOG_INFO("Error renewing operation transaction, will use clean start (OperationId: %s)",
                                ~ToString(operation->GetOperationId()));
                            operation->SetCleanStart(true);
                        }
                    }
                }
            }

            auto batchReq = Owner->StartBatchRequest();
            FOREACH (auto operation, Result.Operations) {
                if (!operation->GetCleanStart()) {
                    LOG_INFO("Reusing operation transactions (OperationId: %s)",
                        ~ToString(operation->GetOperationId()));
                    continue;
                }

                auto scheduleAbort = [&] (ITransactionPtr transaction) {
                    if (transaction) {
                        auto req = TTransactionYPathProxy::Abort(FromObjectId(transaction->GetId()));
                        batchReq->AddRequest(req, "abort_tx");
                    }
                };

                // Abort transactions.
                // NB: Don't touch user transaction.
                scheduleAbort(operation->GetSyncSchedulerTransaction());
                scheduleAbort(operation->GetAsyncSchedulerTransaction());
                scheduleAbort(operation->GetInputTransaction());
                scheduleAbort(operation->GetOutputTransaction());

                operation->SetSyncSchedulerTransaction(nullptr);
                operation->SetAsyncSchedulerTransaction(nullptr);
                operation->SetInputTransaction(nullptr);
                operation->SetOutputTransaction(nullptr);

                // Remove snapshot.
                {
                    auto req = TYPathProxy::Remove(GetSnapshotPath(operation->GetOperationId()));
                    batchReq->AddRequest(req, "remove_snapshot");
                }
            }

            return batchReq->Invoke();
        }


        // Round 7:
        // - Watcher requests.
        TObjectServiceProxy::TInvExecuteBatch Round7(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
        {
            THROW_ERROR_EXCEPTION_IF_FAILED(*batchRsp);

            // NB: Don't check abort errors, some transactions may have already expired.

            {
                auto rsps = batchRsp->GetResponses<TYPathProxy::TRspRemove>("remove_snapshot");
                FOREACH (auto rsp, rsps) {
                    THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Error removing snapshot");
                }
            }

            // Make watcher requests.
            auto batchReq = Owner->StartBatchRequest();
            FOREACH (auto requester, Owner->GlobalWatcherRequesters) {
                requester.Run(batchReq);
            }
            return batchReq->Invoke();
        }

        // Round 8:
        // - Relax :)
        TMasterHandshakeResult Round8(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
        {
            THROW_ERROR_EXCEPTION_IF_FAILED(*batchRsp);

            Result.WatcherResponses = batchRsp;
            return Result;
        }

    };


    TObjectServiceProxy::TReqExecuteBatchPtr StartBatchRequest(bool requireTransaction = true)
    {
        return DoStartBatchRequest(&Proxy, requireTransaction);
    }

    TObjectServiceProxy::TReqExecuteBatchPtr StartBatchRequest(TUpdateList* list, bool requireTransaction = true)
    {
        return DoStartBatchRequest(&list->Proxy, requireTransaction);
    }

    TObjectServiceProxy::TReqExecuteBatchPtr DoStartBatchRequest(TObjectServiceProxy* proxy, bool requireTransaction = true)
    {
        auto req = proxy->ExecuteBatch();
        if (requireTransaction) {
            YCHECK(LockTransaction);
            req->PrerequisiteTransactionIds().push_back(LockTransaction->GetId());
        }
        return req;
    }


    void Disconnect()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        if (!Connected)
            return;

        LOG_WARNING("Master disconnected");

        Connected = false;

        LockTransaction.Reset();

        ClearUpdateLists();
        
        StopRefresh();
        StopSnapshots();
        
        CancelableContext->Cancel();

        MasterDisconnected_.Fire();

        StartConnecting();
    }


    static void BuildOperationNode(TOperationPtr operation, IYsonConsumer* consumer)
    {
        BuildYsonFluently(consumer)
            .BeginAttributes()
                .Do(BIND(&BuildOperationAttributes, operation))
                .Item("progress").BeginMap().EndMap()
                .Item("opaque").Value("true")
            .EndAttributes()
            .BeginMap()
                .Item("jobs").BeginAttributes()
                    .Item("opaque").Value("true")
                .EndAttributes()
                .BeginMap()
                .EndMap()
            .EndMap();
    }

    static void BuildRevivingOperationAttributes(TOperationPtr operation, IYsonConsumer* consumer)
    {
        BuildYsonFluently(consumer)
            .BeginMap()
                .Do(BIND(&BuildOperationAttributes, operation))
                .Item("progress").BeginMap().EndMap()
            .EndMap();
    }

    static void BuildJobNode(TJobPtr job, IYsonConsumer* consumer)
    {
        BuildYsonFluently(consumer)
            .BeginAttributes()
                .Do(BIND(&BuildJobAttributes, job))
            .EndAttributes()
            .BeginMap()
            .EndMap();
    }


    TOperationPtr CreateOperationFromAttributes(const TOperationId& operationId, const IAttributeDictionary& attributes)
    {
        auto transactionManager = Bootstrap->GetTransactionManager();

        ITransactionPtr userTransaction;
        {
            auto id = attributes.Get<TTransactionId>("user_transaction_id");
            TTransactionAttachOptions options(id);
            options.AutoAbort = false;
            options.Ping = false;
            options.PingAncestors = false;
            userTransaction = id == NullTransactionId ? nullptr : transactionManager->Attach(options);
        }
        
        ITransactionPtr syncTransaction;
        {
            auto id = attributes.Get<TTransactionId>("sync_scheduler_transaction_id");
            TTransactionAttachOptions options(id);
            options.AutoAbort = false;
            options.Ping = true;
            options.PingAncestors = false;
            syncTransaction = id == NullTransactionId ? nullptr : transactionManager->Attach(options);
        }


        ITransactionPtr asyncTransaction;
        {
            auto id = attributes.Get<TTransactionId>("async_scheduler_transaction_id");
            TTransactionAttachOptions options(id);
            options.AutoAbort = false;
            options.Ping = true;
            options.PingAncestors = false;
            asyncTransaction = id == NullTransactionId ? nullptr : transactionManager->Attach(options);
        }

        ITransactionPtr inputTransaction;
        {
            auto id = attributes.Get<TTransactionId>("input_transaction_id");
            TTransactionAttachOptions options(id);
            options.AutoAbort = false;
            options.Ping = true;
            options.PingAncestors = false;
            inputTransaction = id == NullTransactionId ? nullptr : transactionManager->Attach(options);
        }

        ITransactionPtr outputTransaction;
        {
            auto id = attributes.Get<TTransactionId>("output_transaction_id");
            TTransactionAttachOptions options(id);
            options.AutoAbort = false;
            options.Ping = true;
            options.PingAncestors = false;
            outputTransaction = id == NullTransactionId ? nullptr : transactionManager->Attach(options);
        }

        auto operation = New<TOperation>(
            operationId,
            attributes.Get<EOperationType>("operation_type"),
            userTransaction,
            attributes.Get<INodePtr>("spec")->AsMap(),
            attributes.Get<Stroka>("authenticated_user"),
            attributes.Get<TInstant>("start_time"),
            attributes.Get<EOperationState>("state"));

        operation->SetSyncSchedulerTransaction(syncTransaction);
        operation->SetAsyncSchedulerTransaction(asyncTransaction);
        operation->SetInputTransaction(inputTransaction);
        operation->SetOutputTransaction(outputTransaction);

        return operation;
    }


    void StartRefresh()
    {
        TransactionRefreshInvoker = New<TPeriodicInvoker>(
            CancelableControlInvoker,
            BIND(&TImpl::RefreshTransactions, MakeWeak(this)),
            Config->TransactionsRefreshPeriod,
            EPeriodicInvokerMode::Manual);
        TransactionRefreshInvoker->Start();

        OperationNodesUpdateInvoker = New<TPeriodicInvoker>(
            CancelableControlInvoker,
            BIND(&TImpl::UpdateOperationNodes, MakeWeak(this)),
            Config->OperationsUpdatePeriod,
            EPeriodicInvokerMode::Manual);
        OperationNodesUpdateInvoker->Start();

        WatchersInvoker = New<TPeriodicInvoker>(
            CancelableControlInvoker,
            BIND(&TImpl::UpdateWatchers, MakeWeak(this)),
            Config->WatchersUpdatePeriod,
            EPeriodicInvokerMode::Manual);
        WatchersInvoker->Start();
    }

    void StopRefresh()
    {
        if (TransactionRefreshInvoker) {
            TransactionRefreshInvoker->Stop();
            TransactionRefreshInvoker.Reset();
        }

        if (OperationNodesUpdateInvoker) {
            OperationNodesUpdateInvoker->Stop();
            OperationNodesUpdateInvoker.Reset();
        }

        if (WatchersInvoker) {
            WatchersInvoker->Stop();
            WatchersInvoker.Reset();
        }
    }

    
    void StartSnapshots()
    {
        SnapshotInvoker = New<TPeriodicInvoker>(
            CancelableControlInvoker,
            BIND(&TImpl::BuildSnapshot, MakeWeak(this)),
            Config->SnapshotPeriod,
            EPeriodicInvokerMode::Manual);
        SnapshotInvoker->Start();
    }

    void StopSnapshots()
    {
        if (SnapshotInvoker) {
            SnapshotInvoker->Stop();
            SnapshotInvoker.Reset();
        }
    }


    void RefreshTransactions()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        // Collect all transactions that are used by currently running operations.
        yhash_set<TTransactionId> watchSet;
        auto watchTransaction = [&] (ITransactionPtr transaction) {
            if (transaction) {
                watchSet.insert(transaction->GetId());
            }
        };

        auto operations = Bootstrap->GetScheduler()->GetOperations();
        FOREACH (auto operation, operations) {
            if (operation->GetState() == EOperationState::Running) {
                watchTransaction(operation->GetUserTransaction());
                watchTransaction(operation->GetSyncSchedulerTransaction());
                watchTransaction(operation->GetAsyncSchedulerTransaction());
                watchTransaction(operation->GetInputTransaction());
                watchTransaction(operation->GetOutputTransaction());
            }
        }

        // Invoke GetId verbs for these transactions to see if they are alive.
        std::vector<TTransactionId> transactionIdsList;
        auto batchReq = StartBatchRequest();
        FOREACH (const auto& id, watchSet) {
            auto checkReq = TObjectYPathProxy::GetId(FromObjectId(id));
            transactionIdsList.push_back(id);
            batchReq->AddRequest(checkReq, "check_tx");
        }

        LOG_INFO("Refreshing transactions");

        batchReq->Invoke().Subscribe(
            BIND(&TImpl::OnTransactionsRefreshed, MakeStrong(this), transactionIdsList)
                .Via(CancelableControlInvoker));
    }

    void OnTransactionsRefreshed(
        const std::vector<TTransactionId>& transactionIds,
        TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        TransactionRefreshInvoker->ScheduleNext();

        if (!batchRsp->IsOK()) {
            LOG_ERROR(*batchRsp, "Error refreshing transactions");
            Disconnect();
            return;
        }

        LOG_INFO("Transactions refreshed");

        // Collect the list of dead transactions.
        auto rsps = batchRsp->GetResponses("check_tx");
        yhash_set<TTransactionId> deadTransactionIds;
        for (int index = 0; index < static_cast<int>(rsps.size()); ++index) {
            if (!batchRsp->GetResponse(index)->IsOK()) {
                YCHECK(deadTransactionIds.insert(transactionIds[index]).second);
            }
        }

        auto isDead = [&] (ITransactionPtr transaction) {
            return transaction && deadTransactionIds.find(transaction->GetId()) != deadTransactionIds.end();
        };

        // Check every operation is it references a dead transaction.
        // If so, raise an appropriate notification.
        auto operations = Bootstrap->GetScheduler()->GetOperations();
        FOREACH (auto operation, operations) {
            if (operation->GetState() == EOperationState::Running) {
                if (isDead(operation->GetUserTransaction())) {
                    UserTransactionAborted_.Fire(operation);
                }
                if (isDead(operation->GetSyncSchedulerTransaction()) ||
                    isDead(operation->GetAsyncSchedulerTransaction()) ||
                    isDead(operation->GetInputTransaction()) ||
                    isDead(operation->GetOutputTransaction()))
                {
                    SchedulerTransactionAborted_.Fire(operation);
                }
            }
        }
    }


    TUpdateList* CreateUpdateList(TOperationPtr operation)
    {
        TUpdateList list(Bootstrap->GetMasterChannel(), operation);
        auto pair = UpdateLists.insert(std::make_pair(operation->GetOperationId(), list));
        YCHECK(pair.second);
        return &pair.first->second;
    }

    TUpdateList* FindUpdateList(TOperationPtr operation)
    {
        auto it = UpdateLists.find(operation->GetOperationId());
        return it == UpdateLists.end() ? nullptr : &it->second;
    }

    TUpdateList* GetUpdateList(TOperationPtr operation)
    {
        auto* result = FindUpdateList(operation);
        YCHECK(result);
        return result;
    }

    void RemoveUpdateList(TOperationPtr operation)
    {
        YCHECK(UpdateLists.erase(operation->GetOperationId()));
    }

    void ClearUpdateLists()
    {
        UpdateLists.clear();
    }


    TWatcherList* GetOrCreateWatcherList(TOperationPtr operation)
    {
        auto it = WatcherLists.find(operation->GetOperationId());
        if (it == WatcherLists.end()) {
            it = WatcherLists.insert(std::make_pair(
                operation->GetOperationId(),
                TWatcherList(operation))).first;
        }
        return &it->second;
    }

    TWatcherList* FindWatcherList(TOperationPtr operation)
    {
        auto it = WatcherLists.find(operation->GetOperationId());
        return it == WatcherLists.end() ? nullptr : &it->second;
    }


    void UpdateOperationNodes()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        LOG_INFO("Updating nodes for %d operations",
            static_cast<int>(UpdateLists.size()));

        auto awaiter = New<TParallelAwaiter>(CancelableControlInvoker);

        FOREACH (auto& pair, UpdateLists) {
            auto& list = pair.second;
            auto operation = list.Operation;
            if (list.State == EUpdateListState::Active) {
                LOG_DEBUG("Updating operation node (OperationId: %s)",
                    ~ToString(operation->GetOperationId()));

                auto batchReq = StartBatchRequest(&list);
                PrepareOperationUpdate(&list, batchReq);

                awaiter->Await(
                    batchReq->Invoke(),
                    BIND(&TImpl::OnOperationNodeUpdated, MakeStrong(this), operation));
            }
        }

        awaiter->Complete(BIND(&TImpl::OnOperationNodesUpdated, MakeStrong(this)));
    }

    void OnOperationNodeUpdated(
        TOperationPtr operation,
        TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        auto error = batchRsp->GetCumulativeError();
        if (!error.IsOK()) {
            LOG_ERROR(error, "Error updating operation node (OperationId: %s)",
                ~ToString(operation->GetOperationId()));
            Disconnect();
            return;
        }

        LOG_DEBUG("Operation node updated (OperationId: %s)",
            ~ToString(operation->GetOperationId()));
    }

    void OnOperationNodesUpdated()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        LOG_INFO("Operation nodes updated");

        OperationNodesUpdateInvoker->ScheduleNext();
    }


    void PrepareOperationUpdate(
        TOperationPtr operation,
        TObjectServiceProxy::TReqExecuteBatchPtr batchReq)
    {
        auto state = operation->GetState();
        auto operationPath = GetOperationPath(operation->GetOperationId());

        // Set state.
        {
            auto req = TYPathProxy::Set(operationPath + "/@state");
            auto state = operation->GetState();
            req->set_value(ConvertToYsonString(state).Data());
            batchReq->AddRequest(req);
        }

        // Set progress.
        if (state == EOperationState::Running || IsOperationFinished(state)) {
            auto req = TYPathProxy::Set(operationPath + "/@progress");
            req->set_value(BuildYsonStringFluently()
                .BeginMap()
                    .Do(BIND(&IOperationController::BuildProgressYson, operation->GetController()))
                .EndMap().Data());
            batchReq->AddRequest(req);
        }

        // Set result.
        if (operation->IsFinishedState()) {
            auto req = TYPathProxy::Set(operationPath + "/@result");
            req->set_value(ConvertToYsonString(BIND(
                &IOperationController::BuildResultYson,
                operation->GetController())).Data());
            batchReq->AddRequest(req);
        }

        // Set end time, if given.
        if (operation->GetFinishTime()) {
            auto req = TYPathProxy::Set(operationPath + "/@finish_time");
            req->set_value(ConvertToYsonString(operation->GetFinishTime().Get()).Data());
            batchReq->AddRequest(req);
        }
    }

    void PrepareOperationUpdate(
        TUpdateList* list,
        TObjectServiceProxy::TReqExecuteBatchPtr batchReq)
    {
        auto operation = list->Operation;

        PrepareOperationUpdate(operation, batchReq);

        // Create jobs.
        FOREACH (const auto& pair, list->PendingJobs) {
            auto job = pair.first;
            auto chunkId = pair.second;
            auto jobPath = GetJobPath(operation->GetOperationId(), job->GetId());
            auto req = TYPathProxy::Set(jobPath);
            TYsonProducer producer(BIND(&TImpl::BuildJobNode, job));
            req->set_value(ConvertToYsonString(producer).Data());
            batchReq->AddRequest(req);

            if (chunkId != NullChunkId) {
                auto stdErrPath = GetStdErrPath(operation->GetOperationId(), job->GetId());

                auto req = TCypressYPathProxy::Create(stdErrPath);
                GenerateRpcMutationId(req);
                req->set_type(EObjectType::File);

                auto attributes = CreateEphemeralAttributes();
                attributes->Set("replication_factor", 1);
                attributes->Set("account", TmpAccountName);
                ToProto(req->mutable_node_attributes(), *attributes);

                auto* reqExt = req->MutableExtension(NFileClient::NProto::TReqCreateFileExt::create_file_ext);
                ToProto(reqExt->mutable_chunk_id(), chunkId);

                batchReq->AddRequest(req);
            }
        }
        list->PendingJobs.clear();
    }


    TError OnOperationNodeCreated(
        TOperationPtr operation,
        TCancelableContextPtr context,
        TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto operationId = operation->GetOperationId();
        auto error = batchRsp->GetCumulativeError();

        if (!error.IsOK()) {
            auto wrappedError = TError("Error creating operation node (OperationId: %s)",
                ~ToString(operationId))
                << error;

            if (!context->IsCanceled()) {
                RemoveUpdateList(operation);
            }

            LOG_WARNING(wrappedError);
            return wrappedError;
        }

        LOG_INFO("Operation node created (OperationId: %s)",
            ~ToString(operationId));

        return TError();
    }

    TError OnRevivingOperationNodeReset(
        TOperationPtr operation,
        TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        auto operationId = operation->GetOperationId();

        auto error = batchRsp->GetCumulativeError();

        if (error.IsOK()) {
        } else {
            auto wrappedError = TError("Error resetting reviving operation node (OperationId: %s)",
                ~ToString(operationId))
                << error;
            LOG_ERROR(wrappedError);
            return wrappedError;
        }

        LOG_INFO("Reviving operation node reset (OperationId: %s)",
            ~ToString(operationId));

        return TError();
    }

    void OnOperationNodeFlushed(
        TOperationPtr operation,
        TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        auto operationId = operation->GetOperationId();

        auto error = batchRsp->GetCumulativeError();
        if (!error.IsOK()) {
            LOG_ERROR(error, "Error flushing operation node (OperationId: %s)",
                ~ToString(operationId));
            Disconnect();
            return;
        }

        LOG_INFO("Operation node flushed (OperationId: %s)",
            ~ToString(operationId));

        auto* list = GetUpdateList(operation);
        list->State = EUpdateListState::Flushed;
        list->FlushedPromise.Set();
    }

    void OnOperationNodeFinalized(
        TOperationPtr operation,
        TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        auto operationId = operation->GetOperationId();

        auto error = batchRsp->GetCumulativeError();
        if (!error.IsOK()) {
            LOG_ERROR(error, "Error finalizing operation node (OperationId: %s)",
                ~ToString(operationId));
            Disconnect();
            return;
        }

        LOG_INFO("Operation node finalized (OperationId: %s)",
            ~ToString(operationId));

        auto* list = GetUpdateList(operation);
        list->State = EUpdateListState::Finalized; 
        list->FinalizedPromise.Set();

        RemoveUpdateList(operation);
    }

    void OnRevivingOperationNodeFinalized(
        TOperationPtr operation,
        TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        auto id = operation->GetOperationId();
        auto error = batchRsp->GetCumulativeError();

        if (!error.IsOK()) {
            LOG_WARNING(error, "Error finalizing reviving operation node (OperationId: %s)",
                ~ToString(id));
            Disconnect();
            return;
        }

        LOG_INFO("Reviving operation node finalized (OperationId: %s)",
            ~ToString(id));
    }


    void UpdateWatchers()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        LOG_INFO("Updating watchers");

        // Global watchers.
        {
            auto batchReq = StartBatchRequest();
            FOREACH (auto requester, GlobalWatcherRequesters) {
                requester.Run(batchReq);
            }
            batchReq->Invoke().Subscribe(
                BIND(&TImpl::OnGlobalWatchersUpdated, MakeStrong(this))
                    .Via(CancelableControlInvoker));
        }

        // Purge obsolete watchers.
        {
            auto it = WatcherLists.begin();
            while (it != WatcherLists.end()) {
                auto jt = it++;
                const auto& list = jt->second;
                if (list.Operation->IsFinishedState()) {
                    WatcherLists.erase(jt);
                }
            }
        }

        // Per-operation watchers.
        FOREACH (const auto& pair, WatcherLists) {
            const auto& list = pair.second;
            auto operation = list.Operation;
            if (operation->GetState() != EOperationState::Running)
                continue;

            auto batchReq = StartBatchRequest();
            FOREACH (auto requester, list.WatcherRequesters) {
                requester.Run(batchReq);
            }
            batchReq->Invoke().Subscribe(
                BIND(&TImpl::OnOperationWatchersUpdated, MakeStrong(this), operation)
                    .Via(CancelableControlInvoker));
        }

        WatchersInvoker->ScheduleNext();
    }

    void OnGlobalWatchersUpdated(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        if (!batchRsp->IsOK()) {
            LOG_ERROR(*batchRsp, "Error updating global watchers");
            return;
        }

        FOREACH (auto handler, GlobalWatcherHandlers) {
            handler.Run(batchRsp);
        }

        LOG_INFO("Global watchers updated");
    }

    void OnOperationWatchersUpdated(TOperationPtr operation, TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YCHECK(Connected);

        if (!batchRsp->IsOK()) {
            LOG_ERROR(*batchRsp, "Error updating operation watchers (OperationId: %s)",
                ~ToString(operation->GetOperationId()));
            return;
        }

        if (operation->GetState() != EOperationState::Running)
            return;

        auto* list = FindWatcherList(operation);
        if (!list)
            return;

        FOREACH (auto handler, list->WatcherHandlers) {
            handler.Run(batchRsp);
        }

        LOG_INFO("Operation watchers updated (OperationId: %s)",
            ~ToString(operation->GetOperationId()));
    }


    void BuildSnapshot()
    {
        auto builder = New<TSnapshotBuilder>(Config, Bootstrap);
        builder->Run().Subscribe(BIND(&TImpl::OnSnapshotBuilt, MakeWeak(this))
            .Via(CancelableControlInvoker));
    }

    void OnSnapshotBuilt(TError error)
    {
        SnapshotInvoker->ScheduleNext();
    }
};

////////////////////////////////////////////////////////////////////

TMasterConnector::TMasterConnector(
    TSchedulerConfigPtr config,
    NCellScheduler::TBootstrap* bootstrap)
    : Impl(New<TImpl>(config, bootstrap))
{ }

TMasterConnector::~TMasterConnector()
{ }

void TMasterConnector::Start()
{
    Impl->Start();
}

bool TMasterConnector::IsConnected() const
{
    return Impl->IsConnected();
}

TAsyncError TMasterConnector::CreateOperationNode(TOperationPtr operation)
{
    return Impl->CreateOperationNode(operation);
}

TAsyncError TMasterConnector::ResetRevivingOperationNode(TOperationPtr operation)
{
    return Impl->ResetRevivingOperationNode(operation);
}

TFuture<void> TMasterConnector::FlushOperationNode(TOperationPtr operation)
{
    return Impl->FlushOperationNode(operation);
}

TFuture<void> TMasterConnector::FinalizeOperationNode(TOperationPtr operation)
{
    return Impl->FinalizeOperationNode(operation);
}

void TMasterConnector::CreateJobNode(TJobPtr job, const TChunkId& stdErrChunkId)
{
    return Impl->CreateJobNode(job, stdErrChunkId);
}

void TMasterConnector::AddGlobalWatcherRequester(TWatcherRequester requester)
{
    Impl->AddGlobalWatcherRequester(requester);
}

void TMasterConnector::AddGlobalWatcherHandler(TWatcherHandler handler)
{
    Impl->AddGlobalWatcherHandler(handler);
}

void TMasterConnector::AddOperationWatcherRequester(TOperationPtr operation, TWatcherRequester requester)
{
    Impl->AddOperationWatcherRequester(operation, requester);
}

void TMasterConnector::AddOperationWatcherHandler(TOperationPtr operation, TWatcherHandler handler)
{
    Impl->AddOperationWatcherHandler(operation, handler);
}

DELEGATE_SIGNAL(TMasterConnector, void(const TMasterHandshakeResult& result), MasterConnected, *Impl);
DELEGATE_SIGNAL(TMasterConnector, void(), MasterDisconnected, *Impl);
DELEGATE_SIGNAL(TMasterConnector, void(TOperationPtr operation), UserTransactionAborted, *Impl)
DELEGATE_SIGNAL(TMasterConnector, void(TOperationPtr operation), SchedulerTransactionAborted, *Impl)

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

