#pragma once

#include "operation_controller.h"

#include <yt/server/cell_scheduler/public.h>

#include <yt/server/scheduler/message_queue.h>

#include <yt/ytlib/scheduler/job_resources.h>

namespace NYT {
namespace NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

struct TAgentToSchedulerOperationEvent
{
    NScheduler::EAgentToSchedulerOperationEventType EventType;
    TOperationId OperationId;
    TError Error;
};

struct TAgentToSchedulerJobEvent
{
    NScheduler::EAgentToSchedulerJobEventType EventType;
    TJobId JobId;
    TError Error;
    TNullable<EInterruptReason> InterruptReason;
};

////////////////////////////////////////////////////////////////////////////////

class TOperationControllerHost
    : public IOperationControllerHost
{
public:
    TOperationControllerHost(
        TOperation* operation,
        IInvokerPtr cancelableControlInvoker,
        TIntrusivePtr<NScheduler::TMessageQueueOutbox<TAgentToSchedulerOperationEvent>> operationEventsOutbox,
        TIntrusivePtr<NScheduler::TMessageQueueOutbox<TAgentToSchedulerJobEvent>> jobEventsOutbox,
        NCellScheduler::TBootstrap* bootstrap);

    virtual void InterruptJob(const TJobId& jobId, EInterruptReason reason) override;
    virtual void AbortJob(const TJobId& jobId, const TError& error) override;
    virtual void FailJob(const TJobId& jobId) override;
    virtual void ReleaseJobs(const std::vector<TJobId>& jobIds) override;

    virtual TFuture<TOperationSnapshot> DownloadSnapshot() override;
    virtual TFuture<void> RemoveSnapshot() override;

    virtual TFuture<void> FlushOperationNode() override;
    virtual void CreateJobNode(const TCreateJobNodeRequest& request) override;

    virtual TFuture<void> AttachChunkTreesToLivePreview(
        const NTransactionClient::TTransactionId& transactionId,
        const std::vector<NCypressClient::TNodeId>& tableIds,
        const std::vector<NChunkClient::TChunkTreeId>& childIds) override;
    virtual void AddChunkTreesToUnstageList(
        const std::vector<NChunkClient::TChunkId>& chunkTreeIds,
        bool recursive) override;

    virtual const NApi::INativeClientPtr& GetClient() override;
    virtual const NNodeTrackerClient::TNodeDirectoryPtr& GetNodeDirectory() override;
    virtual const NChunkClient::TThrottlerManagerPtr& GetChunkLocationThrottlerManager() override;
    virtual const IInvokerPtr& GetControllerThreadPoolInvoker() override;
    virtual const NEventLog::TEventLogWriterPtr& GetEventLogWriter() override;
    virtual const TCoreDumperPtr& GetCoreDumper() override;
    virtual const NConcurrency::TAsyncSemaphorePtr& GetCoreSemaphore() override;
    virtual const NConcurrency::IThroughputThrottlerPtr& GetJobSpecSliceThrottler() override;

    virtual int GetExecNodeCount() override;
    virtual TExecNodeDescriptorListPtr GetExecNodeDescriptors(const NScheduler::TSchedulingTagFilter& filter) override;

    virtual TInstant GetConnectionTime() override;

    virtual void OnOperationCompleted() override;
    virtual void OnOperationAborted(const TError& error) override;
    virtual void OnOperationFailed(const TError& error) override;
    virtual void OnOperationSuspended(const TError& error) override;

private:
    const TOperationId OperationId_;
    const IInvokerPtr CancelableControlInvoker_;
    const TIntrusivePtr<NScheduler::TMessageQueueOutbox<TAgentToSchedulerOperationEvent>> OperationEventsOutbox_;
    const TIntrusivePtr<NScheduler::TMessageQueueOutbox<TAgentToSchedulerJobEvent>> JobEventsOutbox_;
    NCellScheduler::TBootstrap* const Bootstrap_;
    const TIncarnationId IncarnationId_;
};

DEFINE_REFCOUNTED_TYPE(TOperationControllerHost)

////////////////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT

