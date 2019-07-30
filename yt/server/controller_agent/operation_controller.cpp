#include "operation_controller.h"
#include "helpers.h"
#include "operation.h"
#include "ordered_controller.h"
#include "sort_controller.h"
#include "sorted_controller.h"
#include "unordered_controller.h"
#include "operation_controller_host.h"
#include "vanilla_controller.h"
#include "memory_tag_queue.h"

#include <yt/client/api/transaction.h>

#include <yt/ytlib/object_client/public.h>

#include <yt/ytlib/scheduler/config.h>
#include <yt/ytlib/scheduler/proto/job.pb.h>

#include <yt/core/profiling/timing.h>

#include <yt/core/yson/consumer.h>
#include <yt/core/yson/string.h>

namespace NYT::NControllerAgent {

using namespace NApi;
using namespace NScheduler;
using namespace NObjectClient;
using namespace NProfiling;
using namespace NYson;
using namespace NYTree;
using namespace NYTAlloc;

using NScheduler::NProto::TSchedulerJobResultExt;
using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TControllerTransactionIds* transactionIdsProto, const NControllerAgent::TControllerTransactionIds& transactionIds)
{
    ToProto(transactionIdsProto->mutable_async_id(), transactionIds.AsyncId);
    ToProto(transactionIdsProto->mutable_input_id(), transactionIds.InputId);
    ToProto(transactionIdsProto->mutable_output_id(), transactionIds.OutputId);
    ToProto(transactionIdsProto->mutable_debug_id(), transactionIds.DebugId);
    ToProto(transactionIdsProto->mutable_output_completion_id(), transactionIds.OutputCompletionId);
    ToProto(transactionIdsProto->mutable_debug_completion_id(), transactionIds.DebugCompletionId);
    ToProto(transactionIdsProto->mutable_nested_input_ids(), transactionIds.NestedInputIds);
}

void FromProto(NControllerAgent::TControllerTransactionIds* transactionIds, const NProto::TControllerTransactionIds& transactionIdsProto)
{
    transactionIds->AsyncId = FromProto<TTransactionId>(transactionIdsProto.async_id());
    transactionIds->InputId = FromProto<TTransactionId>(transactionIdsProto.input_id());
    transactionIds->OutputId = FromProto<TTransactionId>(transactionIdsProto.output_id());
    transactionIds->DebugId  = FromProto<TTransactionId>(transactionIdsProto.debug_id());
    transactionIds->OutputCompletionId = FromProto<TTransactionId>(transactionIdsProto.output_completion_id());
    transactionIds->DebugCompletionId = FromProto<TTransactionId>(transactionIdsProto.debug_completion_id());
    transactionIds->NestedInputIds = FromProto<std::vector<TTransactionId>>(transactionIdsProto.nested_input_ids());
}

////////////////////////////////////////////////////////////////////////////////

//! Ensures that operation controllers are being destroyed in a
//! dedicated invoker and releases memory tag when controller is destroyed.
class TOperationControllerWrapper
    : public IOperationController
{
public:
    TOperationControllerWrapper(
        TOperationId id,
        IOperationControllerPtr underlying,
        IInvokerPtr dtorInvoker,
        TMemoryTag memoryTag,
        TMemoryTagQueue* memoryTagQueue)
        : Id_(id)
        , Underlying_(std::move(underlying))
        , DtorInvoker_(std::move(dtorInvoker))
        , MemoryTag_(memoryTag)
        , MemoryTagQueue_(memoryTagQueue)
    { }

    virtual ~TOperationControllerWrapper()
    {
        DtorInvoker_->Invoke(BIND([
                underlying = std::move(Underlying_),
                id = Id_,
                memoryTagQueue = MemoryTagQueue_,
                memoryTag = MemoryTag_] () mutable {
            auto Logger = NLogging::TLogger(ControllerLogger)
                .AddTag("OperationId: %v", id);
            NProfiling::TWallTimer timer;
            YT_LOG_INFO("Started destroying operation controller");
            underlying.Reset();
            YT_LOG_INFO("Finished destroying operation controller (Elapsed: %v)",
                timer.GetElapsedTime());
            memoryTagQueue->ReclaimTag(memoryTag);
        }));
    }

    virtual TOperationControllerInitializeResult InitializeClean() override
    {
        return Underlying_->InitializeClean();
    }

    virtual TOperationControllerInitializeResult InitializeReviving(const TControllerTransactionIds& transactions) override
    {
        return Underlying_->InitializeReviving(transactions);
    }

    virtual TOperationControllerPrepareResult Prepare() override
    {
        return Underlying_->Prepare();
    }

    virtual TOperationControllerMaterializeResult Materialize() override
    {
        return Underlying_->Materialize();
    }

    virtual void Commit() override
    {
        Underlying_->Commit();
    }

    virtual void SaveSnapshot(IOutputStream* stream) override
    {
        Underlying_->SaveSnapshot(stream);
    }

    virtual TOperationControllerReviveResult Revive() override
    {
        return Underlying_->Revive();
    }

    virtual void Abort(EControllerState finalState) override
    {
        Underlying_->Abort(finalState);
    }

    virtual void Cancel() override
    {
        Underlying_->Cancel();
    }

    virtual void Complete() override
    {
        Underlying_->Complete();
    }

    virtual void Dispose() override
    {
        Underlying_->Dispose();
    }

    virtual void UpdateRuntimeParameters(const TOperationRuntimeParametersUpdatePtr& update) override
    {
        Underlying_->UpdateRuntimeParameters(update);
    }

    virtual void OnTransactionsAborted(const std::vector<TTransactionId>& transactionIds) override
    {
        Underlying_->OnTransactionsAborted(transactionIds);
    }

    virtual TCancelableContextPtr GetCancelableContext() const override
    {
        return Underlying_->GetCancelableContext();
    }

    virtual IInvokerPtr GetInvoker(EOperationControllerQueue queue) const override
    {
        return Underlying_->GetInvoker(queue);
    }

    virtual IInvokerPtr GetCancelableInvoker(EOperationControllerQueue queue) const override
    {
        return Underlying_->GetCancelableInvoker(queue);
    }

    virtual TFuture<void> Suspend() override
    {
        return Underlying_->Suspend();
    }

    virtual void Resume() override
    {
        Underlying_->Resume();
    }

    virtual int GetPendingJobCount() const override
    {
        return Underlying_->GetPendingJobCount();
    }

    virtual bool IsRunning() const override
    {
        return Underlying_->IsRunning();
    }

    virtual TJobResources GetNeededResources() const override
    {
        return Underlying_->GetNeededResources();
    }

    virtual void UpdateMinNeededJobResources() override
    {
        Underlying_->UpdateMinNeededJobResources();
    }

    virtual TJobResourcesWithQuotaList GetMinNeededJobResources() const override
    {
        return Underlying_->GetMinNeededJobResources();
    }

    virtual void OnJobStarted(std::unique_ptr<TStartedJobSummary> jobSummary) override
    {
        Underlying_->OnJobStarted(std::move(jobSummary));
    }

    virtual void OnJobCompleted(std::unique_ptr<TCompletedJobSummary> jobSummary) override
    {
        Underlying_->OnJobCompleted(std::move(jobSummary));
    }

    virtual void OnJobFailed(std::unique_ptr<TFailedJobSummary> jobSummary) override
    {
        Underlying_->OnJobFailed(std::move(jobSummary));
    }

    virtual void OnJobAborted(std::unique_ptr<TAbortedJobSummary> jobSummary, bool byScheduler) override
    {
        Underlying_->OnJobAborted(std::move(jobSummary), byScheduler);
    }

    virtual void OnJobRunning(std::unique_ptr<TRunningJobSummary> jobSummary) override
    {
        Underlying_->OnJobRunning(std::move(jobSummary));
    }

    virtual TControllerScheduleJobResultPtr ScheduleJob(
        ISchedulingContext* context,
        const TJobResourcesWithQuota& jobLimits,
        const TString& treeId) override
    {
        return Underlying_->ScheduleJob(context, jobLimits, treeId);
    }

    virtual void UpdateConfig(const TControllerAgentConfigPtr& config) override
    {
        Underlying_->UpdateConfig(config);
    }

    virtual bool ShouldUpdateProgress() const override
    {
        return Underlying_->ShouldUpdateProgress();
    }

    virtual void SetProgressUpdated() override
    {
        Underlying_->SetProgressUpdated();
    }

    virtual bool HasProgress() const override
    {
        return Underlying_->HasProgress();
    }

    virtual TYsonString GetProgress() const override
    {
        return Underlying_->GetProgress();
    }

    virtual TYsonString GetBriefProgress() const override
    {
        return Underlying_->GetBriefProgress();
    }

    virtual TYsonString BuildJobYson(TJobId jobId, bool outputStatistics) const override
    {
        return Underlying_->BuildJobYson(jobId, outputStatistics);
    }

    virtual TSharedRef ExtractJobSpec(TJobId jobId) const override
    {
        return Underlying_->ExtractJobSpec(jobId);
    }

    virtual TOperationJobMetrics PullJobMetricsDelta(bool force) override
    {
        return Underlying_->PullJobMetricsDelta(force);
    }

    virtual TOperationAlertMap GetAlerts() override
    {
        return Underlying_->GetAlerts();
    }

    virtual TOperationInfo BuildOperationInfo() override
    {
        return Underlying_->BuildOperationInfo();
    }

    virtual TYsonString GetSuspiciousJobsYson() const override
    {
        return Underlying_->GetSuspiciousJobsYson();
    }

    virtual TSnapshotCookie OnSnapshotStarted() override
    {
        return Underlying_->OnSnapshotStarted();
    }

    virtual void OnSnapshotCompleted(const TSnapshotCookie& cookie) override
    {
        return Underlying_->OnSnapshotCompleted(cookie);
    }

    virtual IYPathServicePtr GetOrchid() const override
    {
        return Underlying_->GetOrchid();
    }

    virtual TString WriteCoreDump() const override
    {
        return Underlying_->WriteCoreDump();
    }

    virtual void RegisterOutputRows(i64 count, int tableIndex) override
    {
        return Underlying_->RegisterOutputRows(count, tableIndex);
    }

    virtual std::optional<int> GetRowCountLimitTableIndex() override
    {
        return Underlying_->GetRowCountLimitTableIndex();
    }

private:
    const TOperationId Id_;
    const IOperationControllerPtr Underlying_;
    const IInvokerPtr DtorInvoker_;
    const TMemoryTag MemoryTag_;
    TMemoryTagQueue* const MemoryTagQueue_;
};

////////////////////////////////////////////////////////////////////////////////

IOperationControllerPtr CreateControllerForOperation(
    TControllerAgentConfigPtr config,
    TOperation* operation)
{
    IOperationControllerPtr controller;
    auto host = operation->GetHost();
    switch (operation->GetType()) {
        case EOperationType::Map: {
            auto baseSpec = ParseOperationSpec<TMapOperationSpec>(operation->GetSpec());
            controller = baseSpec->Ordered
                ? CreateOrderedMapController(config, host, operation)
                : CreateUnorderedMapController(config, host, operation);
            break;
        }
        case EOperationType::Merge: {
            auto baseSpec = ParseOperationSpec<TMergeOperationSpec>(operation->GetSpec());
            switch (baseSpec->Mode) {
                case EMergeMode::Ordered: {
                    controller = CreateOrderedMergeController(config, host, operation);
                    break;
                }
                case EMergeMode::Sorted: {
                    controller = CreateSortedMergeController(config, host, operation);
                    break;
                }
                case EMergeMode::Unordered: {
                    controller = CreateUnorderedMergeController(config, host, operation);
                    break;
                }
            }
            break;
        }
        case EOperationType::Erase: {
            controller = CreateEraseController(config, host, operation);
            break;
        }
        case EOperationType::Sort: {
            controller = CreateSortController(config, host, operation);
            break;
        }
        case EOperationType::Reduce: {
            controller = CreateAppropriateReduceController(config, host, operation, /* isJoinReduce */ false);
            break;
        }
        case EOperationType::JoinReduce: {
            controller = CreateAppropriateReduceController(config, host, operation, /* isJoinReduce */ true);
            break;
        }
        case EOperationType::MapReduce: {
            controller = CreateMapReduceController(config, host, operation);
            break;
        }
        case EOperationType::RemoteCopy: {
            controller = CreateRemoteCopyController(config, host, operation);
            break;
        }
        case EOperationType::Vanilla: {
            controller = CreateVanillaController(config, host, operation);
            break;
        }
        default:
            YT_ABORT();
    }

    return New<TOperationControllerWrapper>(
        operation->GetId(),
        controller,
        controller->GetInvoker(),
        operation->GetMemoryTag(),
        host->GetMemoryTagQueue());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent

