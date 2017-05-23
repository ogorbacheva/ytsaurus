#include "operation_controller_detail.h"
#include "private.h"
#include "chunk_list_pool.h"
#include "helpers.h"
#include "intermediate_chunk_scraper.h"
#include "job_helpers.h"
#include "job_metrics_updater.h"
#include "master_connector.h"

#include "map_controller.h"
#include "merge_controller.h"
#include "sorted_controller.h"
#include "remote_copy_controller.h"
#include "sort_controller.h"

#include <yt/server/scheduler/helpers.h>
#include <yt/server/scheduler/master_connector.h>

#include <yt/server/misc/job_table_schema.h>

#include <yt/server/scheduler/job_metrics.h>

#include <yt/server/chunk_pools/chunk_pool.h>
#include <yt/server/chunk_pools/public.h>

#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/ytlib/chunk_client/chunk_scraper.h>
#include <yt/ytlib/chunk_client/chunk_teleporter.h>
#include <yt/ytlib/chunk_client/data_slice_descriptor.h>
#include <yt/ytlib/chunk_client/data_statistics.h>
#include <yt/ytlib/chunk_client/data_source.h>
#include <yt/ytlib/chunk_client/helpers.h>
#include <yt/ytlib/chunk_client/input_chunk_slice.h>
#include <yt/ytlib/chunk_client/input_data_slice.h>

#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/core_dump/core_info.pb.h>
#include <yt/ytlib/core_dump/helpers.h>

#include <yt/ytlib/node_tracker_client/node_directory_builder.h>

#include <yt/ytlib/object_client/helpers.h>

#include <yt/ytlib/query_client/query.h>
#include <yt/ytlib/query_client/query_preparer.h>
#include <yt/ytlib/query_client/functions_cache.h>
#include <yt/ytlib/query_client/column_evaluator.h>

#include <yt/ytlib/scheduler/helpers.h>

#include <yt/ytlib/table_client/chunk_meta_extensions.h>
#include <yt/ytlib/table_client/data_slice_fetcher.h>
#include <yt/ytlib/table_client/helpers.h>
#include <yt/ytlib/table_client/schema.h>
#include <yt/ytlib/table_client/table_consumer.h>
#include <yt/ytlib/table_client/row_buffer.h>

#include <yt/ytlib/transaction_client/helpers.h>

#include <yt/ytlib/api/transaction.h>
#include <yt/ytlib/api/native_connection.h>

#include <yt/core/concurrency/action_queue.h>
#include <yt/core/concurrency/throughput_throttler.h>

#include <yt/core/erasure/codec.h>

#include <yt/core/misc/fs.h>
#include <yt/core/misc/collection_helpers.h>
#include <yt/core/misc/numeric_helpers.h>

#include <yt/core/profiling/scoped_timer.h>
#include <yt/core/profiling/profiler.h>

#include <functional>

namespace NYT {
namespace NControllerAgent {

using namespace NChunkPools;
using namespace NCypressClient;
using namespace NTransactionClient;
using namespace NFileClient;
using namespace NChunkClient;
using namespace NObjectClient;
using namespace NYTree;
using namespace NYson;
using namespace NYPath;
using namespace NFormats;
using namespace NJobProxy;
using namespace NJobTrackerClient;
using namespace NNodeTrackerClient;
using namespace NScheduler::NProto;
using namespace NJobTrackerClient::NProto;
using namespace NCoreDump::NProto;
using namespace NConcurrency;
using namespace NApi;
using namespace NRpc;
using namespace NTableClient;
using namespace NQueryClient;
using namespace NProfiling;
using namespace NScheduler;

using NNodeTrackerClient::TNodeId;
using NProfiling::CpuInstantToInstant;
using NProfiling::TCpuInstant;
using NTableClient::NProto::TBoundaryKeysExt;
using NTableClient::TTableReaderOptions;

////////////////////////////////////////////////////////////////////

namespace {

void CommitTransaction(ITransactionPtr& transaction)
{
    if (!transaction) {
        return;
    }
    auto result = WaitFor(transaction->Commit());
    if (!result.IsOK()) {
        transaction->Abort(); // Ignore result.
        THROW_ERROR_EXCEPTION("Transaction %v has failed to commit",
            transaction->GetId())
            << result;
    }
    transaction.Reset();
}

} // namespace

////////////////////////////////////////////////////////////////////

static class TJobHelper
{
public:
    TJobHelper()
    {
        for (auto state : TEnumTraits<EJobState>::GetDomainValues()) {
            for (auto type : TEnumTraits<EJobType>::GetDomainValues()) {
                StatisticsSuffixes_[state][type] = Format("/$/%lv/%lv", state, type);
            }
        }
    }

    const Stroka& GetStatisticsSuffix(EJobState state, EJobType type) const
    {
        return StatisticsSuffixes_[state][type];
    }

private:
    TEnumIndexedVector<TEnumIndexedVector<Stroka, EJobType>, EJobState> StatisticsSuffixes_;

} JobHelper;

////////////////////////////////////////////////////////////////////

void TOperationControllerBase::TLivePreviewTableBase::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, LivePreviewTableId);
}

////////////////////////////////////////////////////////////////////

void TOperationControllerBase::TJobBoundaryKeys::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, MinKey);
    Persist(context, MaxKey);
    Persist(context, ChunkTreeId);
}

////////////////////////////////////////////////////////////////////

bool TOperationControllerBase::TInputTable::IsForeign() const
{
    return Path.GetForeign();
}

bool TOperationControllerBase::TInputTable::IsPrimary() const
{
    return !IsForeign();
}

void TOperationControllerBase::TInputTable::Persist(const TPersistenceContext& context)
{
    TUserObject::Persist(context);

    using NYT::Persist;
    Persist(context, ChunkCount);
    Persist(context, Chunks);
    Persist(context, Schema);
    Persist(context, SchemaMode);
    Persist(context, IsDynamic);
}

////////////////////////////////////////////////////////////////////

bool TOperationControllerBase::TOutputTable::IsBeginUploadCompleted() const
{
    return static_cast<bool>(UploadTransactionId);
}

void TOperationControllerBase::TOutputTable::Persist(const TPersistenceContext& context)
{
    TUserObject::Persist(context);
    TLivePreviewTableBase::Persist(context);

    using NYT::Persist;
    Persist(context, TableUploadOptions);
    Persist(context, Options);
    Persist(context, ChunkPropertiesUpdateNeeded);
    Persist(context, OutputType);
    Persist(context, Type);
    Persist(context, DataStatistics);
    // NB: Scheduler snapshots need not be stable.
    Persist<
        TMultiMapSerializer<
            TDefaultSerializer,
            TDefaultSerializer,
            TUnsortedTag
        >
    >(context, OutputChunkTreeIds);
    Persist(context, BoundaryKeys);
    Persist(context, EffectiveAcl);
    Persist(context, WriterConfig);
}

////////////////////////////////////////////////////////////////////

void TOperationControllerBase::TIntermediateTable::Persist(const TPersistenceContext& context)
{
    TLivePreviewTableBase::Persist(context);
}

////////////////////////////////////////////////////////////////////

void TOperationControllerBase::TUserFile::Persist(const TPersistenceContext& context)
{
    TUserObject::Persist(context);

    using NYT::Persist;
    Persist<TAttributeDictionaryRefSerializer>(context, Attributes);
    Persist(context, Stage);
    Persist(context, FileName);
    Persist(context, ChunkSpecs);
    Persist(context, ChunkCount);
    Persist(context, Type);
    Persist(context, Executable);
    Persist(context, Format);
    Persist(context, Schema);
    Persist(context, IsDynamic);
}

////////////////////////////////////////////////////////////////////

void TOperationControllerBase::TCompletedJob::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, Lost);
    Persist(context, JobId);
    Persist(context, SourceTask);
    Persist(context, OutputCookie);
    Persist(context, DataSize);
    Persist(context, DestinationPool);
    Persist(context, InputCookie);
    Persist(context, NodeDescriptor);
}

////////////////////////////////////////////////////////////////////

TOperationControllerBase::TJoblet::TJoblet()
    : JobIndex(-1)
    , StartRowIndex(-1)
    , OutputCookie(-1)
{ }

TOperationControllerBase::TJoblet::TJoblet(TOperationControllerBase* controller, TTaskPtr task, int jobIndex)
    : Task(std::move(task))
    , JobIndex(jobIndex)
    , StartRowIndex(-1)
    , OutputCookie(IChunkPoolOutput::NullCookie)
    , JobMetricsUpdater_(controller->CreateJobMetricsUpdater())
{ }

TOperationControllerBase::TJoblet::~TJoblet() = default;

void TOperationControllerBase::TJobInfoBase::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, JobId);
    Persist(context, JobType);
    Persist(context, NodeDescriptor);
    Persist(context, StartTime);
    Persist(context, FinishTime);
    Persist(context, Account);
    Persist(context, Suspicious);
    Persist(context, LastActivityTime);
    Persist(context, BriefStatistics);
    Persist(context, Progress);
}

void TOperationControllerBase::TJoblet::Persist(const TPersistenceContext& context)
{
    // NB: Every joblet is aborted after snapshot is loaded.
    // Here we only serialize a subset of members required for ReinstallJob to work
    // properly.
    using NYT::Persist;
    Persist(context, Task);
    Persist(context, InputStripeList);
    Persist(context, OutputCookie);

    TJobInfoBase::Persist(context);
}

void TOperationControllerBase::TFinishedJobInfo::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, Summary);
    Persist(context, InputPaths);

    TJobInfoBase::Persist(context);
}

void TOperationControllerBase::TJoblet::SendJobMetrics(const TStatistics& jobStatistics, bool flush)
{
    // NOTE: after snapshot is loaded JobMetricsUpdater_ can be missing.
    if (JobMetricsUpdater_) {
        const auto timestamp = jobStatistics.GetTimestamp().Get(CpuInstantToInstant(GetCpuInstant()));
        const auto jobMetrics = TJobMetrics::FromJobTrackerStatistics(jobStatistics);
        JobMetricsUpdater_->Update(timestamp, jobMetrics);
        if (flush) {
            JobMetricsUpdater_->Flush();
        }
    }
}

////////////////////////////////////////////////////////////////////

void TOperationControllerBase::TTaskGroup::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, MinNeededResources);
    // NB: Scheduler snapshots need not be stable.
    Persist<
        TSetSerializer<
            TDefaultSerializer,
            TUnsortedTag
        >
    >(context, NonLocalTasks);
    Persist<
        TMultiMapSerializer<
            TDefaultSerializer,
            TDefaultSerializer,
            TUnsortedTag
        >
    >(context, CandidateTasks);
    Persist<
        TMultiMapSerializer<
            TDefaultSerializer,
            TDefaultSerializer,
            TUnsortedTag
        >
    >(context, DelayedTasks);
    Persist<
        TMapSerializer<
            TDefaultSerializer,
            TSetSerializer<
                TDefaultSerializer,
                TUnsortedTag
            >,
            TUnsortedTag
        >
    >(context, NodeIdToTasks);
}

////////////////////////////////////////////////////////////////////

void TOperationControllerBase::TStripeDescriptor::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, Stripe);
    Persist(context, Cookie);
    Persist(context, Task);
}

////////////////////////////////////////////////////////////////////

void TOperationControllerBase::TInputChunkDescriptor::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, InputStripes);
    Persist(context, InputChunks);
    Persist(context, State);
}

////////////////////////////////////////////////////////////////////

TOperationControllerBase::TTask::TTask()
    : CachedPendingJobCount(-1)
    , CachedTotalJobCount(-1)
    , DemandSanityCheckDeadline(0)
    , CompletedFired(false)
    , Logger(OperationLogger)
{ }

TOperationControllerBase::TTask::TTask(TOperationControllerBase* controller)
    : Controller(controller)
    , CachedPendingJobCount(0)
    , CachedTotalJobCount(0)
    , DemandSanityCheckDeadline(0)
    , CompletedFired(false)
    , Logger(OperationLogger)
{ }

void TOperationControllerBase::TTask::Initialize()
{
    Logger = Controller->Logger;
    Logger.AddTag("Task: %v", GetId());
}

int TOperationControllerBase::TTask::GetPendingJobCount() const
{
    return GetChunkPoolOutput()->GetPendingJobCount();
}

int TOperationControllerBase::TTask::GetPendingJobCountDelta()
{
    int oldValue = CachedPendingJobCount;
    int newValue = GetPendingJobCount();
    CachedPendingJobCount = newValue;
    return newValue - oldValue;
}

int TOperationControllerBase::TTask::GetTotalJobCount() const
{
    return GetChunkPoolOutput()->GetTotalJobCount();
}

int TOperationControllerBase::TTask::GetTotalJobCountDelta()
{
    int oldValue = CachedTotalJobCount;
    int newValue = GetTotalJobCount();
    CachedTotalJobCount = newValue;
    return newValue - oldValue;
}

TNullable<i64> TOperationControllerBase::TTask::GetMaximumUsedTmpfsSize() const
{
    return MaximumUsedTmfpsSize;
}

const TProgressCounter& TOperationControllerBase::TTask::GetJobCounter() const
{
    return GetChunkPoolOutput()->GetJobCounter();
}

TJobResources TOperationControllerBase::TTask::GetTotalNeededResourcesDelta()
{
    auto oldValue = CachedTotalNeededResources;
    auto newValue = GetTotalNeededResources();
    CachedTotalNeededResources = newValue;
    newValue -= oldValue;
    return newValue;
}

TJobResources TOperationControllerBase::TTask::GetTotalNeededResources() const
{
    i64 count = GetPendingJobCount();
    // NB: Don't call GetMinNeededResources if there are no pending jobs.
    return count == 0 ? ZeroJobResources() : GetMinNeededResources() * count;
}

bool TOperationControllerBase::TTask::IsIntermediateOutput() const
{
    return false;
}

bool TOperationControllerBase::TTask::IsStderrTableEnabled() const
{
    // We write stderr if corresponding options were specified and only for user-type jobs.
    // For example we don't write stderr for sort stage in mapreduce operation
    // even if stderr table were specified.
    return Controller->GetStderrTablePath() && GetUserJobSpec();
}

bool TOperationControllerBase::TTask::IsCoreTableEnabled() const
{
    // Same as above.
    return Controller->GetCoreTablePath() && GetUserJobSpec();
}

i64 TOperationControllerBase::TTask::GetLocality(TNodeId nodeId) const
{
    return HasInputLocality()
        ? GetChunkPoolOutput()->GetLocality(nodeId)
        : 0;
}

bool TOperationControllerBase::TTask::HasInputLocality() const
{
    return true;
}

void TOperationControllerBase::TTask::AddInput(TChunkStripePtr stripe)
{
    Controller->RegisterInputStripe(stripe, this);
    if (HasInputLocality()) {
        Controller->AddTaskLocalityHint(this, stripe);
    }
    AddPendingHint();
}

void TOperationControllerBase::TTask::AddInput(const std::vector<TChunkStripePtr>& stripes)
{
    for (auto stripe : stripes) {
        if (stripe) {
            AddInput(stripe);
        }
    }
}

void TOperationControllerBase::TTask::FinishInput()
{
    LOG_DEBUG("Task input finished");

    GetChunkPoolInput()->Finish();
    AddPendingHint();
    CheckCompleted();
}

void TOperationControllerBase::TTask::CheckCompleted()
{
    if (!CompletedFired && IsCompleted()) {
        CompletedFired = true;
        OnTaskCompleted();
    }
}

TUserJobSpecPtr TOperationControllerBase::TTask::GetUserJobSpec() const
{
    return nullptr;
}

void TOperationControllerBase::TTask::ScheduleJob(
    ISchedulingContext* context,
    const TJobResources& jobLimits,
    TScheduleJobResult* scheduleJobResult)
{
    if (!CanScheduleJob(context, jobLimits)) {
        scheduleJobResult->RecordFail(EScheduleJobFailReason::TaskRefusal);
        return;
    }

    bool intermediateOutput = IsIntermediateOutput();
    int jobIndex = Controller->JobIndexGenerator.Next();
    auto joblet = New<TJoblet>(Controller, this, jobIndex);

    const auto& nodeResourceLimits = context->ResourceLimits();
    auto nodeId = context->GetNodeDescriptor().Id;
    const auto& address = context->GetNodeDescriptor().Address;

    auto* chunkPoolOutput = GetChunkPoolOutput();
    auto localityNodeId = HasInputLocality() ? nodeId : InvalidNodeId;
    joblet->OutputCookie = chunkPoolOutput->Extract(localityNodeId);
    if (joblet->OutputCookie == IChunkPoolOutput::NullCookie) {
        LOG_DEBUG("Job input is empty");
        scheduleJobResult->RecordFail(EScheduleJobFailReason::EmptyInput);
        return;
    }

    joblet->InputStripeList = chunkPoolOutput->GetStripeList(joblet->OutputCookie);

    int sliceCount = joblet->InputStripeList->TotalChunkCount;
    const auto& jobSpecSliceThrottler = context->GetJobSpecSliceThrottler();
    if (sliceCount > Controller->Config->HeavyJobSpecSliceCountThreshold) {
        if (!jobSpecSliceThrottler->TryAcquire(sliceCount)) {
            LOG_DEBUG("Job spec throttling is active (SliceCount: %v)",
                sliceCount);
            chunkPoolOutput->Aborted(joblet->OutputCookie, EAbortReason::SchedulingJobSpecThrottling);
            scheduleJobResult->RecordFail(EScheduleJobFailReason::JobSpecThrottling);
            return;
        }
    } else {
        jobSpecSliceThrottler->Acquire(sliceCount);
    }

    auto estimatedResourceUsage = GetNeededResources(joblet);
    auto neededResources = ApplyMemoryReserve(estimatedResourceUsage);

    joblet->EstimatedResourceUsage = estimatedResourceUsage;
    joblet->ResourceLimits = neededResources;

    // Check the usage against the limits. This is the last chance to give up.
    if (!Dominates(jobLimits, neededResources)) {
        LOG_DEBUG("Job actual resource demand is not met (Limits: %v, Demand: %v)",
            FormatResources(jobLimits),
            FormatResources(neededResources));
        CheckResourceDemandSanity(nodeResourceLimits, neededResources);
        chunkPoolOutput->Aborted(joblet->OutputCookie, EAbortReason::SchedulingOther);
        // Seems like cached min needed resources are too optimistic.
        ResetCachedMinNeededResources();
        scheduleJobResult->RecordFail(EScheduleJobFailReason::NotEnoughResources);
        return;
    }

    // Async part.
    auto controller = MakeStrong(Controller); // hold the controller
    auto jobSpecBuilder = BIND([=, this_ = MakeStrong(this)] (TJobSpec* jobSpec) {
        BuildJobSpec(joblet, jobSpec);
        jobSpec->set_version(GetJobSpecVersion());
        controller->CustomizeJobSpec(joblet, jobSpec);

        auto* schedulerJobSpecExt = jobSpec->MutableExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
        if (controller->Spec->JobProxyMemoryOvercommitLimit) {
            schedulerJobSpecExt->set_job_proxy_memory_overcommit_limit(*controller->Spec->JobProxyMemoryOvercommitLimit);
        }
        schedulerJobSpecExt->set_job_proxy_ref_counted_tracker_log_period(ToProto(controller->Spec->JobProxyRefCountedTrackerLogPeriod));

        schedulerJobSpecExt->set_enable_sort_verification(controller->Spec->EnableSortVerification);

        schedulerJobSpecExt->set_abort_job_if_account_limit_exceeded(controller->Spec->SuspendOperationIfAccountLimitExceeded);

        // Adjust sizes if approximation flag is set.
        if (joblet->InputStripeList->IsApproximate) {
            schedulerJobSpecExt->set_input_uncompressed_data_size(static_cast<i64>(
                schedulerJobSpecExt->input_uncompressed_data_size() *
                ApproximateSizesBoostFactor));
            schedulerJobSpecExt->set_input_row_count(static_cast<i64>(
                schedulerJobSpecExt->input_row_count() *
                ApproximateSizesBoostFactor));
        }

        if (schedulerJobSpecExt->input_uncompressed_data_size() > controller->Spec->MaxDataSizePerJob) {
            controller->OnOperationFailed(TError(
                "Maximum allowed data size per job violated: %v > %v",
                schedulerJobSpecExt->input_uncompressed_data_size(),
                controller->Spec->MaxDataSizePerJob));
        }
    });

    auto jobType = GetJobType();
    joblet->JobId = context->GenerateJobId();
    auto restarted = LostJobCookieMap.find(joblet->OutputCookie) != LostJobCookieMap.end();
    joblet->Account = Controller->Spec->JobNodeAccount;
    scheduleJobResult->JobStartRequest.Emplace(
        joblet->JobId,
        jobType,
        neededResources,
        Controller->IsJobInterruptible(),
        jobSpecBuilder);

    joblet->Restarted = restarted;
    joblet->JobType = jobType;
    joblet->NodeDescriptor = context->GetNodeDescriptor();
    joblet->JobProxyMemoryReserveFactor = Controller->GetJobProxyMemoryDigest(jobType)->GetQuantile(Controller->Config->JobProxyMemoryReserveQuantile);
    auto userJobSpec = GetUserJobSpec();
    if (userJobSpec) {
        joblet->UserJobMemoryReserveFactor = Controller->GetUserJobMemoryDigest(GetJobType())->GetQuantile(Controller->Config->UserJobMemoryReserveQuantile);
    }

    LOG_DEBUG(
        "Job scheduled (JobId: %v, OperationId: %v, JobType: %v, Address: %v, JobIndex: %v, OutputCookie: %v, SliceCount: %v (%v local), "
        "Approximate: %v, DataSize: %v (%v local), RowCount: %v, Restarted: %v, EstimatedResourceUsage: %v, JobProxyMemoryReserveFactor: %v, "
        "UserJobMemoryReserveFactor: %v, ResourceLimits: %v)",
        joblet->JobId,
        Controller->OperationId,
        jobType,
        address,
        jobIndex,
        joblet->OutputCookie,
        joblet->InputStripeList->TotalChunkCount,
        joblet->InputStripeList->LocalChunkCount,
        joblet->InputStripeList->IsApproximate,
        joblet->InputStripeList->TotalDataSize,
        joblet->InputStripeList->LocalDataSize,
        joblet->InputStripeList->TotalRowCount,
        restarted,
        FormatResources(estimatedResourceUsage),
        joblet->JobProxyMemoryReserveFactor,
        joblet->UserJobMemoryReserveFactor,
        FormatResources(neededResources));

    // Prepare chunk lists.
    if (intermediateOutput) {
        joblet->ChunkListIds.push_back(Controller->ExtractChunkList(Controller->IntermediateOutputCellTag));
    } else {
        for (const auto& table : Controller->OutputTables) {
            joblet->ChunkListIds.push_back(Controller->ExtractChunkList(table.CellTag));
        }
    }

    if (Controller->StderrTable && IsStderrTableEnabled()) {
        joblet->StderrTableChunkListId = Controller->ExtractChunkList(Controller->StderrTable->CellTag);
    }

    if (Controller->CoreTable && IsCoreTableEnabled()) {
        joblet->CoreTableChunkListId = Controller->ExtractChunkList(Controller->CoreTable->CellTag);
    }

    // Sync part.
    PrepareJoblet(joblet);
    Controller->CustomizeJoblet(joblet);

    Controller->RegisterJoblet(joblet);
    Controller->AddValueToEstimatedHistogram(joblet);

    OnJobStarted(joblet);

    if (Controller->JobSplitter_) {
        Controller->JobSplitter_->OnJobStarted(joblet->JobId, joblet->InputStripeList);
    }
}

bool TOperationControllerBase::TTask::IsPending() const
{
    return GetChunkPoolOutput()->GetPendingJobCount() > 0;
}

bool TOperationControllerBase::TTask::IsCompleted() const
{
    return IsActive() && GetChunkPoolOutput()->IsCompleted();
}

bool TOperationControllerBase::TTask::IsActive() const
{
    return true;
}

i64 TOperationControllerBase::TTask::GetTotalDataSize() const
{
    return GetChunkPoolOutput()->GetTotalDataSize();
}

i64 TOperationControllerBase::TTask::GetCompletedDataSize() const
{
    return GetChunkPoolOutput()->GetCompletedDataSize();
}

i64 TOperationControllerBase::TTask::GetPendingDataSize() const
{
    return GetChunkPoolOutput()->GetPendingDataSize();
}

void TOperationControllerBase::TTask::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;

    // COMPAT(babenko)
    if (context.IsLoad() && context.GetVersion() < 200009) {
        Load<TNullable<TInstant>>(context.LoadContext());
    }

    Persist(context, Controller);

    Persist(context, CachedPendingJobCount);
    Persist(context, CachedTotalJobCount);

    Persist(context, CachedTotalNeededResources);
    Persist(context, CachedMinNeededResources);

    Persist(context, CompletedFired);

    Persist(context, LostJobCookieMap);
}

void TOperationControllerBase::TTask::PrepareJoblet(TJobletPtr /* joblet */)
{ }

void TOperationControllerBase::TTask::OnJobStarted(TJobletPtr joblet)
{ }

void TOperationControllerBase::TTask::OnJobCompleted(TJobletPtr joblet, const TCompletedJobSummary& jobSummary)
{
    YCHECK(jobSummary.Statistics);
    const auto& statistics = *jobSummary.Statistics;

    if (!jobSummary.Abandoned) {
        auto outputStatisticsMap = GetOutputDataStatistics(statistics);
        for (int index = 0; index < static_cast<int>(joblet->ChunkListIds.size()); ++index) {
            YCHECK(outputStatisticsMap.find(index) != outputStatisticsMap.end());
            auto outputStatistics = outputStatisticsMap[index];
            if (outputStatistics.chunk_count() == 0) {
                Controller->ChunkListPool->Reinstall(joblet->ChunkListIds[index]);
                joblet->ChunkListIds[index] = NullChunkListId;
            }
        }

        auto inputStatistics = GetTotalInputDataStatistics(statistics);
        auto outputStatistics = GetTotalOutputDataStatistics(statistics);
        // It's impossible to check row count preservation on interrupted job.
        if (Controller->IsRowCountPreserved() && jobSummary.InterruptReason == EInterruptReason::None) {
            LOG_ERROR_IF(inputStatistics.row_count() != outputStatistics.row_count(),
                "Input/output row count mismatch in completed job (Input: %v, Output: %v, Task: %v)",
                inputStatistics.row_count(),
                outputStatistics.row_count(),
                GetId());
            YCHECK(inputStatistics.row_count() == outputStatistics.row_count());
        }
    } else {
        auto& chunkListIds = joblet->ChunkListIds;
        Controller->ChunkListPool->Release(chunkListIds);
        std::fill(chunkListIds.begin(), chunkListIds.end(), NullChunkListId);
    }
    GetChunkPoolOutput()->Completed(joblet->OutputCookie, jobSummary);

    Controller->RegisterStderr(joblet, jobSummary);
    Controller->RegisterCores(joblet, jobSummary);

    UpdateMaximumUsedTmpfsSize(statistics);
}

void TOperationControllerBase::TTask::ReinstallJob(TJobletPtr joblet, std::function<void()> releaseOutputCookie)
{
    Controller->RemoveValueFromEstimatedHistogram(joblet);
    Controller->ReleaseChunkLists(joblet->ChunkListIds);

    auto list = HasInputLocality()
        ? GetChunkPoolOutput()->GetStripeList(joblet->OutputCookie)
        : nullptr;

    releaseOutputCookie();

    if (HasInputLocality()) {
        for (const auto& stripe : list->Stripes) {
            Controller->AddTaskLocalityHint(this, stripe);
        }
    }
    AddPendingHint();
}

void TOperationControllerBase::TTask::OnJobFailed(TJobletPtr joblet, const TFailedJobSummary& jobSummary)
{
    Controller->RegisterStderr(joblet, jobSummary);
    Controller->RegisterCores(joblet, jobSummary);

    YCHECK(jobSummary.Statistics);
    UpdateMaximumUsedTmpfsSize(*jobSummary.Statistics);

    ReinstallJob(joblet, BIND([=] {GetChunkPoolOutput()->Failed(joblet->OutputCookie);}));
}

void TOperationControllerBase::TTask::OnJobAborted(TJobletPtr joblet, const TAbortedJobSummary& jobSummary)
{
    if (joblet->StderrTableChunkListId) {
        Controller->ReleaseChunkLists({joblet->StderrTableChunkListId});
    }
    if (joblet->CoreTableChunkListId) {
        Controller->ReleaseChunkLists({joblet->CoreTableChunkListId});
    }

    ReinstallJob(joblet, BIND([=] {GetChunkPoolOutput()->Aborted(joblet->OutputCookie, jobSummary.AbortReason);}));
}

void TOperationControllerBase::TTask::OnJobLost(TCompletedJobPtr completedJob)
{
    YCHECK(LostJobCookieMap.insert(std::make_pair(
        completedJob->OutputCookie,
        completedJob->InputCookie)).second);
}

void TOperationControllerBase::TTask::OnTaskCompleted()
{
    LOG_DEBUG("Task completed");
}

bool TOperationControllerBase::TTask::CanScheduleJob(
    ISchedulingContext* /*context*/,
    const TJobResources& /*jobLimits*/)
{
    return true;
}

void TOperationControllerBase::TTask::DoCheckResourceDemandSanity(
    const TJobResources& neededResources)
{
    if (Controller->ShouldSkipSanityCheck()) {
        return;
    }

    if (!Dominates(*Controller->CachedMaxAvailableExecNodeResources_, neededResources)) {
        // It seems nobody can satisfy the demand.
        Controller->OnOperationFailed(
            TError("No online node can satisfy the resource demand")
                << TErrorAttribute("task", GetId())
                << TErrorAttribute("needed_resources", neededResources));
    }
}

void TOperationControllerBase::TTask::CheckResourceDemandSanity(
    const TJobResources& nodeResourceLimits,
    const TJobResources& neededResources)
{
    // The task is requesting more than some node is willing to provide it.
    // Maybe it's OK and we should wait for some time.
    // Or maybe it's not and the task is requesting something no one is able to provide.

    // First check if this very node has enough resources (including those currently
    // allocated by other jobs).
    if (Dominates(nodeResourceLimits, neededResources)) {
        return;
    }

    // Schedule check in controller thread.
    Controller->GetCancelableInvoker()->Invoke(BIND(
        &TTask::DoCheckResourceDemandSanity,
        MakeWeak(this),
        neededResources));
}

void TOperationControllerBase::TTask::AddPendingHint()
{
    Controller->AddTaskPendingHint(this);
}

void TOperationControllerBase::TTask::AddLocalityHint(TNodeId nodeId)
{
    Controller->AddTaskLocalityHint(this, nodeId);
}

std::unique_ptr<TNodeDirectoryBuilder> TOperationControllerBase::TTask::MakeNodeDirectoryBuilder(
    TSchedulerJobSpecExt* schedulerJobSpec)
{
    return Controller->OperationType == EOperationType::RemoteCopy
        ? std::make_unique<TNodeDirectoryBuilder>(
            Controller->InputNodeDirectory,
            schedulerJobSpec->mutable_input_node_directory())
        : nullptr;
}

void TOperationControllerBase::TTask::AddSequentialInputSpec(
    TJobSpec* jobSpec,
    TJobletPtr joblet)
{
    auto* schedulerJobSpecExt = jobSpec->MutableExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
    auto directoryBuilder = MakeNodeDirectoryBuilder(schedulerJobSpecExt);
    auto* inputSpec = schedulerJobSpecExt->add_input_table_specs();
    const auto& list = joblet->InputStripeList;
    for (const auto& stripe : list->Stripes) {
        AddChunksToInputSpec(directoryBuilder.get(), inputSpec, stripe);
    }
    UpdateInputSpecTotals(jobSpec, joblet);
}

void TOperationControllerBase::TTask::AddParallelInputSpec(
    TJobSpec* jobSpec,
    TJobletPtr joblet)
{
    auto* schedulerJobSpecExt = jobSpec->MutableExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
    auto directoryBuilder = MakeNodeDirectoryBuilder(schedulerJobSpecExt);
    const auto& list = joblet->InputStripeList;
    for (const auto& stripe : list->Stripes) {
        auto* inputSpec = stripe->Foreign
            ? schedulerJobSpecExt->add_foreign_input_table_specs()
            : schedulerJobSpecExt->add_input_table_specs();
        AddChunksToInputSpec(directoryBuilder.get(), inputSpec, stripe);
    }
    UpdateInputSpecTotals(jobSpec, joblet);
}

void TOperationControllerBase::TTask::AddChunksToInputSpec(
    TNodeDirectoryBuilder* directoryBuilder,
    TTableInputSpec* inputSpec,
    TChunkStripePtr stripe)
{
    for (const auto& dataSlice : stripe->DataSlices) {
        inputSpec->add_chunk_spec_count_per_data_slice(dataSlice->ChunkSlices.size());
        for (const auto& chunkSlice : dataSlice->ChunkSlices) {
            auto newChunkSpec = inputSpec->add_chunk_specs();
            ToProto(newChunkSpec, chunkSlice, dataSlice->Type);
            if (dataSlice->Tag) {
                newChunkSpec->set_data_slice_tag(*dataSlice->Tag);
            }

            if (directoryBuilder) {
                auto replicas = chunkSlice->GetInputChunk()->GetReplicaList();
                directoryBuilder->Add(replicas);
            }
        }
    }

    if (inputSpec->chunk_specs_size() > 0) {
        // Make spec incompatible with older nodes.
        ToProto(inputSpec->add_data_slice_descriptors(), GetIncompatibleDataSliceDescriptor());
    }
}

void TOperationControllerBase::TTask::UpdateInputSpecTotals(
    TJobSpec* jobSpec,
    TJobletPtr joblet)
{
    const auto& list = joblet->InputStripeList;
    auto* schedulerJobSpecExt = jobSpec->MutableExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
    schedulerJobSpecExt->set_input_uncompressed_data_size(
        schedulerJobSpecExt->input_uncompressed_data_size() +
        list->TotalDataSize);
    schedulerJobSpecExt->set_input_row_count(
        schedulerJobSpecExt->input_row_count() +
        list->TotalRowCount);
}

void TOperationControllerBase::TTask::AddFinalOutputSpecs(
    TJobSpec* jobSpec,
    TJobletPtr joblet)
{
    YCHECK(joblet->ChunkListIds.size() == Controller->OutputTables.size());
    auto* schedulerJobSpecExt = jobSpec->MutableExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
    for (int index = 0; index < Controller->OutputTables.size(); ++index) {
        const auto& table = Controller->OutputTables[index];
        auto* outputSpec = schedulerJobSpecExt->add_output_table_specs();
        outputSpec->set_table_writer_options(ConvertToYsonString(table.Options).GetData());
        if (table.WriterConfig) {
            outputSpec->set_table_writer_config(table.WriterConfig.GetData());
        }
        outputSpec->set_timestamp(table.Timestamp);
        ToProto(outputSpec->mutable_table_schema(), table.TableUploadOptions.TableSchema);
        ToProto(outputSpec->mutable_chunk_list_id(), joblet->ChunkListIds[index]);
    }
}

void TOperationControllerBase::TTask::AddIntermediateOutputSpec(
    TJobSpec* jobSpec,
    TJobletPtr joblet,
    const TKeyColumns& keyColumns)
{
    YCHECK(joblet->ChunkListIds.size() == 1);
    auto* schedulerJobSpecExt = jobSpec->MutableExtension(TSchedulerJobSpecExt::scheduler_job_spec_ext);
    auto* outputSpec = schedulerJobSpecExt->add_output_table_specs();

    auto options = New<NTableClient::TTableWriterOptions>();
    options->Account = Controller->Spec->IntermediateDataAccount;
    options->ChunksVital = false;
    options->ChunksMovable = false;
    options->ReplicationFactor = Controller->Spec->IntermediateDataReplicationFactor;
    options->MediumName = Controller->Spec->IntermediateDataMediumName;
    options->CompressionCodec = Controller->Spec->IntermediateCompressionCodec;
    // Distribute intermediate chunks uniformly across storage locations.
    options->PlacementId = Controller->OperationId;

    outputSpec->set_table_writer_options(ConvertToYsonString(options).GetData());

    ToProto(outputSpec->mutable_table_schema(), TTableSchema::FromKeyColumns(keyColumns));
    ToProto(outputSpec->mutable_chunk_list_id(), joblet->ChunkListIds[0]);
}

void TOperationControllerBase::TTask::ResetCachedMinNeededResources()
{
    CachedMinNeededResources.Reset();
}

TJobResources TOperationControllerBase::TTask::ApplyMemoryReserve(const TExtendedJobResources& jobResources) const
{
    TJobResources result;
    result.SetCpu(jobResources.GetCpu());
    result.SetUserSlots(jobResources.GetUserSlots());
    i64 memory = jobResources.GetFootprintMemory();
    memory += jobResources.GetJobProxyMemory() * Controller->GetJobProxyMemoryDigest(GetJobType())->GetQuantile(Controller->Config->JobProxyMemoryReserveQuantile);
    if (GetUserJobSpec()) {
        memory += jobResources.GetUserJobMemory() * Controller->GetUserJobMemoryDigest(GetJobType())->GetQuantile(Controller->Config->UserJobMemoryReserveQuantile);
    } else {
        YCHECK(jobResources.GetUserJobMemory() == 0);
    }
    result.SetMemory(memory);
    result.SetNetwork(jobResources.GetNetwork());
    return result;
}

void TOperationControllerBase::TTask::UpdateMaximumUsedTmpfsSize(const NJobTrackerClient::TStatistics& statistics) {
    auto maxUsedTmpfsSize = FindNumericValue(
        statistics,
        "/user_job/max_tmpfs_size");

    if (!maxUsedTmpfsSize) {
        return;
    }

    if (!MaximumUsedTmfpsSize || *MaximumUsedTmfpsSize < *maxUsedTmpfsSize) {
        MaximumUsedTmfpsSize = *maxUsedTmpfsSize;
    }
}

void TOperationControllerBase::TTask::AddFootprintAndUserJobResources(TExtendedJobResources& jobResources) const
{
    jobResources.SetFootprintMemory(GetFootprintMemorySize());
    auto userJobSpec = GetUserJobSpec();
    if (userJobSpec) {
        jobResources.SetUserJobMemory(userJobSpec->MemoryLimit);
    }
}

TJobResources TOperationControllerBase::TTask::GetMinNeededResources() const
{
    if (!CachedMinNeededResources) {
        YCHECK(GetPendingJobCount() > 0);
        CachedMinNeededResources = GetMinNeededResourcesHeavy();
    }
    auto result = ApplyMemoryReserve(*CachedMinNeededResources);
    if (result.GetUserSlots() > 0 && result.GetMemory() == 0) {
        LOG_WARNING("Found min needed resources of task with non-zero user slots and zero memory");
    }
    return result;
}

void TOperationControllerBase::TTask::RegisterIntermediate(
    TJobletPtr joblet,
    TChunkStripePtr stripe,
    TTaskPtr destinationTask,
    bool attachToLivePreview)
{
    RegisterIntermediate(
        joblet,
        stripe,
        destinationTask->GetChunkPoolInput(),
        attachToLivePreview);

    if (destinationTask->HasInputLocality()) {
        Controller->AddTaskLocalityHint(destinationTask, stripe);
    }
    destinationTask->AddPendingHint();
}

void TOperationControllerBase::TTask::RegisterIntermediate(
    TJobletPtr joblet,
    TChunkStripePtr stripe,
    IChunkPoolInput* destinationPool,
    bool attachToLivePreview)
{
    IChunkPoolInput::TCookie inputCookie;

    auto lostIt = LostJobCookieMap.find(joblet->OutputCookie);
    if (lostIt == LostJobCookieMap.end()) {
        inputCookie = destinationPool->Add(stripe);
    } else {
        inputCookie = lostIt->second;
        destinationPool->Resume(inputCookie, stripe);
        LostJobCookieMap.erase(lostIt);
    }

    // Store recovery info.
    auto completedJob = New<TCompletedJob>(
        joblet->JobId,
        this,
        joblet->OutputCookie,
        joblet->InputStripeList->TotalDataSize,
        destinationPool,
        inputCookie,
        joblet->NodeDescriptor);

    Controller->RegisterIntermediate(
        joblet,
        completedJob,
        stripe,
        attachToLivePreview);
}

TChunkStripePtr TOperationControllerBase::TTask::BuildIntermediateChunkStripe(
    google::protobuf::RepeatedPtrField<NChunkClient::NProto::TChunkSpec>* chunkSpecs)
{
    auto stripe = New<TChunkStripe>();

    i64 currentTableRowIndex = 0;
    for (int index = 0; index < chunkSpecs->size(); ++index) {
        auto inputChunk = New<TInputChunk>(std::move(*chunkSpecs->Mutable(index)));
        // NB(max42): Having correct table row indices on intermediate data is important for
        // some chunk pools. This affects the correctness of sort operation with sorted
        // merge phase over several intermediate chunks.
        inputChunk->SetTableRowIndex(currentTableRowIndex);
        currentTableRowIndex += inputChunk->GetRowCount();
        auto chunkSlice = CreateInputChunkSlice(std::move(inputChunk));
        auto dataSlice = CreateUnversionedInputDataSlice(std::move(chunkSlice));
        // NB(max42): This heavily relies on the property of intermediate data being deterministic
        // (i.e. it may be reproduced with exactly the same content divided into chunks with exactly
        // the same boundary keys when the job output is lost).
        dataSlice->Tag = index;
        stripe->DataSlices.emplace_back(std::move(dataSlice));
    }
    return stripe;
}

void TOperationControllerBase::TTask::RegisterOutput(
    TJobletPtr joblet,
    int key,
    const TCompletedJobSummary& jobSummary)
{
    Controller->RegisterOutput(joblet, key, jobSummary);
}

////////////////////////////////////////////////////////////////////

TOperationControllerBase::TOperationControllerBase(
    TSchedulerConfigPtr config,
    TOperationSpecBasePtr spec,
    TOperationOptionsPtr options,
    IOperationHost* host,
    TOperation* operation)
    : Config(config)
    , Host(host)
    , MasterConnector(Host->GetMasterConnector())
    , OperationId(operation->GetId())
    , OperationType(operation->GetType())
    , StartTime(operation->GetStartTime())
    , AuthenticatedUser(operation->GetAuthenticatedUser())
    , AuthenticatedMasterClient(CreateClient())
    , AuthenticatedInputMasterClient(AuthenticatedMasterClient)
    , AuthenticatedOutputMasterClient(AuthenticatedMasterClient)
    , Logger(OperationLogger)
    , CancelableContext(New<TCancelableContext>())
    , Invoker(Host->CreateOperationControllerInvoker())
    , SuspendableInvoker(CreateSuspendableInvoker(Invoker))
    , CancelableInvoker(CancelableContext->CreateInvoker(SuspendableInvoker))
    , JobCounter(0)
    , RowBuffer(New<TRowBuffer>(TRowBufferTag(), Config->ControllerRowBufferChunkSize))
    , SecureVault(operation->GetSecureVault())
    , Owners(operation->GetOwners())
    , Spec(spec)
    , Options(options)
    , CachedNeededResources(ZeroJobResources())
    , ScheduleJobStatistics_(New<TScheduleJobStatistics>())
    , CheckTimeLimitExecutor(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::CheckTimeLimit, MakeWeak(this)),
        Config->OperationTimeLimitCheckPeriod))
    , ExecNodesCheckExecutor(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::CheckAvailableExecNodes, MakeWeak(this)),
        Config->AvailableExecNodesCheckPeriod))
    , AnalyzeOperationProgressExecutor(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::AnalyzeOperationProgess, MakeWeak(this)),
        Config->OperationProgressAnalysisPeriod))
    , MinNeededResourcesSanityCheckExecutor(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::CheckMinNeededResourcesSanity, MakeWeak(this)),
        Config->ResourceDemandSanityCheckPeriod))
    , MaxAvailableExecNodeResourcesUpdateExecutor(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::UpdateCachedMaxAvailableExecNodeResources, MakeWeak(this)),
        Config->MaxAvailableExecNodeResourcesUpdatePeriod))
    , EventLogValueConsumer_(Host->CreateLogConsumer())
    , EventLogTableConsumer_(new TTableConsumer(EventLogValueConsumer_.get()))
    , CodicilData_(MakeOperationCodicilString(OperationId))
    , ProgressBuildExecutor_(New<TPeriodicExecutor>(
        GetCancelableInvoker(),
        BIND(&TThis::BuildAndSaveProgress, MakeWeak(this)),
        Config->OperationBuildProgressPeriod))
{
    Logger.AddTag("OperationId: %v", OperationId);

    // Attach user transaction if any. Don't ping it.
    TTransactionAttachOptions userAttachOptions;
    userAttachOptions.Ping = false;
    userAttachOptions.PingAncestors = false;

    UserTransactionId = operation->GetUserTransactionId();
    UserTransaction = UserTransactionId
        ? Host->GetMasterClient()->AttachTransaction(UserTransactionId, userAttachOptions)
        : nullptr;
}

void TOperationControllerBase::InitializeConnections()
{ }

void TOperationControllerBase::InitializeReviving(TControllerTransactionsPtr controllerTransactions)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    LOG_INFO("Initializing operation for revive");

    InitializeConnections();

    std::atomic<bool> cleanStart = {false};

    // Check transactions.
    {
        std::vector<std::pair<ITransactionPtr, TFuture<void>>> asyncCheckResults;

        auto checkTransaction = [&] (ITransactionPtr transaction) {
            if (cleanStart) {
                return;
            }

            if (!transaction) {
                cleanStart = true;
                LOG_INFO("Operation transaction is missing, will use clean start");
                return;
            }

            asyncCheckResults.push_back(std::make_pair(transaction, transaction->Ping()));
        };

        // NB: Async transaction is not checked.
        checkTransaction(controllerTransactions->Input);
        checkTransaction(controllerTransactions->Output);
        checkTransaction(controllerTransactions->DebugOutput);

        for (auto pair : asyncCheckResults) {
            const auto& transaction = pair.first;
            const auto& asyncCheckResult = pair.second;
            auto error = WaitFor(asyncCheckResult);
            if (!error.IsOK()) {
                cleanStart = true;
                LOG_INFO(error,
                    "Error renewing operation transaction %v, will use clean start",
                    transaction->GetId());
            }
        }
    }

    // Downloading snapshot.
    if (!cleanStart) {
        auto snapshotOrError = WaitFor(MasterConnector->DownloadSnapshot(OperationId));
        if (!snapshotOrError.IsOK()) {
            LOG_INFO(snapshotOrError, "Failed to download snapshot, will use clean start");
            cleanStart = true;
        } else {
            LOG_INFO("Snapshot succesfully downloaded");
            Snapshot = snapshotOrError.Value();
        }
    }

    // Abort transactions if needed.
    {
        std::vector<TFuture<void>> asyncResults;

        auto scheduleAbort = [&] (ITransactionPtr transaction) {
            if (transaction) {
                asyncResults.push_back(transaction->Abort());
            }
        };

        // NB: Async and Completion transactions are always aborted.
        scheduleAbort(controllerTransactions->Async);
        scheduleAbort(controllerTransactions->Completion);

        if (cleanStart) {
            LOG_INFO("Aborting operation transactions");
            // NB: Don't touch user transaction.
            scheduleAbort(controllerTransactions->Input);
            scheduleAbort(controllerTransactions->Output);
            scheduleAbort(controllerTransactions->DebugOutput);
        } else {
            LOG_INFO("Reusing operation transactions");
            InputTransaction = controllerTransactions->Input;
            OutputTransaction = controllerTransactions->Output;
            DebugOutputTransaction = controllerTransactions->DebugOutput;

            StartAsyncSchedulerTransaction();

            AreTransactionsActive = true;
        }

        WaitFor(Combine(asyncResults))
            .ThrowOnError();
    }


    if (cleanStart) {
        LOG_INFO("Using clean start instead of revive");

        Snapshot = TOperationSnapshot();
        auto error = WaitFor(MasterConnector->RemoveSnapshot(OperationId));
        if (!error.IsOK()) {
            LOG_WARNING(error, "Failed to remove snapshot");
        }

        InitializeTransactions();
        InitializeStructures();
    }

    MasterConnector->RegisterOperation(OperationId, MakeStrong(this));

    LOG_INFO("Operation initialized");
}


void TOperationControllerBase::Initialize()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    LOG_INFO("Initializing operation (Title: %v)",
        Spec->Title);

    auto initializeAction = BIND([this_ = MakeStrong(this), this] () {
        InitializeConnections();
        InitializeTransactions();
        InitializeStructures();
    });

    auto initializeFuture = initializeAction
        .AsyncVia(Host->GetControlInvoker())
        .Run()
        .WithTimeout(Config->OperationInitializationTimeout);

    WaitFor(initializeFuture)
        .ThrowOnError();

    MasterConnector->RegisterOperation(OperationId, MakeStrong(this));

    LOG_INFO("Operation initialized");
}

TOperationControllerInitializeResult TOperationControllerBase::GetInitializeResult() const
{
    TOperationControllerInitializeResult result;
    result.BriefSpec = BuildYsonStringFluently<EYsonType::MapFragment>()
        .Do(BIND(&TOperationControllerBase::BuildBriefSpec, MakeStrong(this)))
        .Finish();
    return result;
}

void TOperationControllerBase::InitializeStructures()
{
    InputNodeDirectory = New<NNodeTrackerClient::TNodeDirectory>();

    for (const auto& path : GetInputTablePaths()) {
        TInputTable table;
        table.Path = path;
        InputTables.push_back(table);
    }

    for (const auto& path : GetOutputTablePaths()) {
        TOutputTable table;
        table.Path = path;

        auto rowCountLimit = path.GetRowCountLimit();
        if (rowCountLimit) {
            if (RowCountLimitTableIndex) {
                THROW_ERROR_EXCEPTION("Only one output table with row_count_limit is supported");
            }
            RowCountLimitTableIndex = OutputTables.size();
            RowCountLimit = rowCountLimit.Get();
        }

        OutputTables.push_back(table);
    }

    if (auto stderrTablePath = GetStderrTablePath()) {
        StderrTable.Emplace();
        StderrTable->Path = *stderrTablePath;
        StderrTable->OutputType = EOutputTableType::Stderr;
    }

    if (auto coreTablePath = GetCoreTablePath()) {
        CoreTable.Emplace();
        CoreTable->Path = *coreTablePath;
        CoreTable->OutputType = EOutputTableType::Core;
    }

    InitUpdatingTables();

    for (const auto& pair : GetFilePaths()) {
        TUserFile file;
        file.Path = pair.first;
        file.Stage = pair.second;
        Files.push_back(file);
    }

    if (InputTables.size() > Config->MaxInputTableCount) {
        THROW_ERROR_EXCEPTION(
            "Too many input tables: maximum allowed %v, actual %v",
            Config->MaxInputTableCount,
            InputTables.size());
    }

    DoInitialize();
}

void TOperationControllerBase::InitUpdatingTables()
{
    UpdatingTables.clear();

    for (auto& table : OutputTables) {
        UpdatingTables.push_back(&table);
    }

    if (StderrTable) {
        UpdatingTables.push_back(StderrTable.GetPtr());
    }

    if (CoreTable) {
        UpdatingTables.push_back(CoreTable.GetPtr());
    }
}

void TOperationControllerBase::DoInitialize()
{ }

void TOperationControllerBase::SafePrepare()
{
    YCHECK(!(Config->EnableFailControllerSpecOption && Spec->FailController));

    PrepareInputTables();

    // Process input tables.
    {
        LockInputTables();
        GetInputTablesAttributes();
    }

    PrepareInputQuery();

    // Process files.
    {
        LockUserFiles();
        GetUserFilesAttributes();
    }

    // Process output and stderr tables.
    {
        GetUserObjectBasicAttributes<TOutputTable>(
            AuthenticatedOutputMasterClient,
            OutputTables,
            OutputTransaction->GetId(),
            Logger,
            EPermission::Write);

        GetUserObjectBasicAttributes<TOutputTable>(
            AuthenticatedMasterClient,
            StderrTable,
            DebugOutputTransaction->GetId(),
            Logger,
            EPermission::Write);

        GetUserObjectBasicAttributes<TOutputTable>(
            AuthenticatedMasterClient,
            CoreTable,
            DebugOutputTransaction->GetId(),
            Logger,
            EPermission::Write);

        yhash_set<TObjectId> updatingTableIds;
        for (const auto* table : UpdatingTables) {
            const auto& path = table->Path.GetPath();
            if (table->Type != EObjectType::Table) {
                THROW_ERROR_EXCEPTION("Object %v has invalid type: expected %Qlv, actual %Qlv",
                    path,
                    EObjectType::Table,
                    table->Type);
            }
            const bool insertedNew = updatingTableIds.insert(table->ObjectId).second;
            if (!insertedNew) {
                THROW_ERROR_EXCEPTION("Output table %v is specified multiple times",
                    path);
            }
        }

        GetOutputTablesSchema();
        PrepareOutputTables();

        LockOutputTablesAndGetAttributes();
    }
}

void TOperationControllerBase::SafeMaterialize()
{
    try {
        FetchInputTables();
        FetchUserFiles();

        PickIntermediateDataCell();
        InitChunkListPool();

        CreateLivePreviewTables();

        CollectTotals();

        CustomPrepare();

        InitializeHistograms();

        LOG_INFO("Tasks prepared (RowBufferCapacity: %v)", RowBuffer->GetCapacity());

        if (InputChunkMap.empty() || IsCompleted()) {
            // Possible reasons:
            // - All input chunks are unavailable && Strategy == Skip
            // - Merge decided to teleport all input chunks
            // - Anything else?
            LOG_INFO("No jobs needed");
            OnOperationCompleted(false /* interrupted */);
            return;
        } else {
            YCHECK(UnavailableInputChunkCount == 0);
            for (const auto& pair : InputChunkMap) {
                const auto& chunkDescriptor = pair.second;
                if (chunkDescriptor.State == EInputChunkState::Waiting) {
                    ++UnavailableInputChunkCount;
                }
            }

            if (UnavailableInputChunkCount > 0) {
                LOG_INFO("Found unavailable input chunks during materialization (UnavailableInputChunkCount: %v)",
                    UnavailableInputChunkCount);
            }
        }

        AddAllTaskPendingHints();

        if (Config->TestingOptions->EnableSnapshotCycleAfterMaterialization) {
            TStringStream stringStream;
            SaveSnapshot(&stringStream);
            TOperationSnapshot snapshot;
            snapshot.Version = GetCurrentSnapshotVersion();
            snapshot.Data = TSharedRef::FromString(stringStream.Str());
            DoLoadSnapshot(snapshot);
        }

        // Input chunk scraper initialization should be the last step to avoid races,
        // because input chunk scraper works in control thread.
        InitInputChunkScraper();
        InitIntermediateChunkScraper();

        CheckTimeLimitExecutor->Start();
        ProgressBuildExecutor_->Start();
        ExecNodesCheckExecutor->Start();
        AnalyzeOperationProgressExecutor->Start();
        MinNeededResourcesSanityCheckExecutor->Start();
        MaxAvailableExecNodeResourcesUpdateExecutor->Start();

        auto jobSplitterConfig = GetJobSplitterConfig();
        if (jobSplitterConfig) {
            JobSplitter_ = CreateJobSplitter(jobSplitterConfig, OperationId);
        }

        State = EControllerState::Running;
    } catch (const std::exception& ex) {
        auto wrappedError = TError("Materialization failed") << ex;
        LOG_ERROR(wrappedError);
        OnOperationFailed(wrappedError);
        return;
    }

    LOG_INFO("Materialization finished");
}

void TOperationControllerBase::SaveSnapshot(TOutputStream* output)
{
    VERIFY_THREAD_AFFINITY_ANY();

    TSaveContext context;
    context.SetVersion(GetCurrentSnapshotVersion());
    context.SetOutput(output);

    Save(context, this);
}

void TOperationControllerBase::SafeRevive()
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    if (!Snapshot.Data) {
        Prepare();
        return;
    }

    DoLoadSnapshot(Snapshot);
    Snapshot = TOperationSnapshot();

    RevivedFromSnapshot = true;

    InitChunkListPool();

    CreateLivePreviewTables();

    AbortAllJoblets();

    AddAllTaskPendingHints();

    // Input chunk scraper initialization should be the last step to avoid races.
    InitInputChunkScraper();
    InitIntermediateChunkScraper();

    ReinstallLivePreview();

    // To prevent operation failure on startup if available nodes are missing.
    AvaialableNodesLastSeenTime_ = GetCpuInstant();

    CheckTimeLimitExecutor->Start();
    ProgressBuildExecutor_->Start();
    ExecNodesCheckExecutor->Start();
    AnalyzeOperationProgressExecutor->Start();
    MinNeededResourcesSanityCheckExecutor->Start();
    MaxAvailableExecNodeResourcesUpdateExecutor->Start();

    auto jobSplitterConfig = GetJobSplitterConfig();
    if (jobSplitterConfig) {
        JobSplitter_ = CreateJobSplitter(jobSplitterConfig, OperationId);
    }

    State = EControllerState::Running;
}

void TOperationControllerBase::InitializeTransactions()
{
    StartAsyncSchedulerTransaction();
    StartInputTransaction(UserTransactionId);
    StartOutputTransaction(UserTransactionId);
    StartDebugOutputTransaction();
    AreTransactionsActive = true;
}

ITransactionPtr TOperationControllerBase::StartTransaction(
    ETransactionType type,
    INativeClientPtr client,
    const TTransactionId& parentTransactionId)
{
    LOG_INFO("Starting transaction (Type: %v)",
        type);

    TTransactionStartOptions options;
    options.AutoAbort = false;
    options.PingAncestors = false;
    auto attributes = CreateEphemeralAttributes();
    attributes->Set(
        "title",
        Format("Scheduler %Qlv transaction for operation %v",
            type,
            OperationId));
    attributes->Set("operation_id", OperationId);
    if (Spec->Title) {
        attributes->Set("operation_title", Spec->Title);
    }
    options.Attributes = std::move(attributes);
    options.ParentId = parentTransactionId;
    options.Timeout = Config->OperationTransactionTimeout;

    auto transactionOrError = WaitFor(
        client->StartTransaction(NTransactionClient::ETransactionType::Master, options));
    THROW_ERROR_EXCEPTION_IF_FAILED(
        transactionOrError,
        "Error starting %Qlv transaction",
        type);
    auto transaction = transactionOrError.Value();

    LOG_INFO("Transaction started (Type: %v, TransactionId: %v)",
        type,
        transaction->GetId());

    return transaction;
}

void TOperationControllerBase::StartAsyncSchedulerTransaction()
{
    AsyncSchedulerTransaction = StartTransaction(
        ETransactionType::Async,
        AuthenticatedMasterClient);
}

void TOperationControllerBase::StartInputTransaction(const TTransactionId& parentTransactionId)
{
    InputTransaction = StartTransaction(
        ETransactionType::Input,
        AuthenticatedInputMasterClient,
        parentTransactionId);
}

void TOperationControllerBase::StartOutputTransaction(const TTransactionId& parentTransactionId)
{
    OutputTransaction = StartTransaction(
        ETransactionType::Output,
        AuthenticatedOutputMasterClient,
        parentTransactionId);
}

void TOperationControllerBase::StartDebugOutputTransaction()
{
    DebugOutputTransaction = StartTransaction(
        ETransactionType::DebugOutput,
        AuthenticatedMasterClient);
}

void TOperationControllerBase::PickIntermediateDataCell()
{
    auto connection = AuthenticatedOutputMasterClient->GetNativeConnection();
    const auto& secondaryCellTags = connection->GetSecondaryMasterCellTags();
    IntermediateOutputCellTag = secondaryCellTags.empty()
        ? connection->GetPrimaryMasterCellTag()
        : secondaryCellTags[rand() % secondaryCellTags.size()];
}

void TOperationControllerBase::InitChunkListPool()
{
    ChunkListPool = New<TChunkListPool>(
        Config,
        AuthenticatedOutputMasterClient,
        CancelableInvoker,
        OperationId,
        OutputTransaction->GetId());

    CellTagToOutputRequiredChunkList.clear();
    for (const auto* table : UpdatingTables) {
        ++CellTagToOutputRequiredChunkList[table->CellTag];
    }

    CellTagToIntermediateRequiredChunkList.clear();
    ++CellTagToIntermediateRequiredChunkList[IntermediateOutputCellTag];
    if (StderrTable) {
        ++CellTagToIntermediateRequiredChunkList[StderrTable->CellTag];
    }
    if (CoreTable) {
        ++CellTagToIntermediateRequiredChunkList[CoreTable->CellTag];
    }
}

void TOperationControllerBase::InitInputChunkScraper()
{
    yhash_set<TChunkId> chunkIds;
    for (const auto& pair : InputChunkMap) {
        chunkIds.insert(pair.first);
    }

    YCHECK(!InputChunkScraper);
    InputChunkScraper = New<TChunkScraper>(
        Config->ChunkScraper,
        CancelableInvoker,
        Host->GetChunkLocationThrottlerManager(),
        AuthenticatedInputMasterClient,
        InputNodeDirectory,
        std::move(chunkIds),
        BIND(&TThis::OnInputChunkLocated, MakeWeak(this)),
        Logger);

    if (UnavailableInputChunkCount > 0) {
        LOG_INFO("Waiting for %v unavailable input chunks", UnavailableInputChunkCount);
        InputChunkScraper->Start();
    }
}

void TOperationControllerBase::InitIntermediateChunkScraper()
{
    IntermediateChunkScraper = New<TIntermediateChunkScraper>(
        Config->ChunkScraper,
        CancelableInvoker,
        Host->GetChunkLocationThrottlerManager(),
        AuthenticatedInputMasterClient,
        InputNodeDirectory,
        [weakThis = MakeWeak(this)] () {
            if (auto this_ = weakThis.Lock()) {
                return this_->GetAliveIntermediateChunks();
            } else {
                return yhash_set<TChunkId>();
            }
        },
        BIND(&TThis::OnIntermediateChunkLocated, MakeWeak(this)),
        Logger);
}

yhash_set<TChunkId> TOperationControllerBase::GetAliveIntermediateChunks() const
{
    yhash_set<TChunkId> intermediateChunks;

    for (const auto& pair : ChunkOriginMap) {
        if (!pair.second->Lost) {
            intermediateChunks.insert(pair.first);
        }
    }

    return intermediateChunks;
}

void TOperationControllerBase::ReinstallLivePreview()
{
    if (IsOutputLivePreviewSupported()) {
        for (const auto& table : OutputTables) {
            std::vector<TChunkTreeId> childIds;
            childIds.reserve(table.OutputChunkTreeIds.size());
            for (const auto& pair : table.OutputChunkTreeIds) {
                childIds.push_back(pair.second);
            }
            MasterConnector->AttachToLivePreview(
                OperationId,
                AsyncSchedulerTransaction->GetId(),
                table.LivePreviewTableId,
                childIds);
        }
    }

    if (IsIntermediateLivePreviewSupported()) {
        std::vector<TChunkTreeId> childIds;
        childIds.reserve(ChunkOriginMap.size());
        for (const auto& pair : ChunkOriginMap) {
            if (!pair.second->Lost) {
                childIds.push_back(pair.first);
            }
        }
        MasterConnector->AttachToLivePreview(
            OperationId,
            AsyncSchedulerTransaction->GetId(),
            IntermediateTable.LivePreviewTableId,
            childIds);
    }
}

void TOperationControllerBase::AbortAllJoblets()
{
    for (const auto& pair : JobletMap) {
        auto joblet = pair.second;
        JobCounter.Aborted(1, EAbortReason::Scheduler);
        joblet->Task->OnJobAborted(joblet, TAbortedJobSummary(pair.first, EAbortReason::Scheduler));
    }
    JobletMap.clear();
}

void TOperationControllerBase::DoLoadSnapshot(const TOperationSnapshot& snapshot)
{
    LOG_INFO("Started loading snapshot (Size: %v, Version: %v)",
        snapshot.Data.Size(),
        snapshot.Version);

    TMemoryInput input(snapshot.Data.Begin(), snapshot.Data.Size());

    TLoadContext context;
    context.SetInput(&input);
    context.SetRowBuffer(RowBuffer);
    context.SetVersion(snapshot.Version);

    NPhoenix::TSerializer::InplaceLoad(context, this);

    LOG_INFO("Finished loading snapshot");
}

void TOperationControllerBase::StartCompletionTransaction()
{
    CompletionTransaction = StartTransaction(
        ETransactionType::Completion,
        AuthenticatedOutputMasterClient,
        OutputTransaction->GetId());

    // Set transaction id to cypress.
    {
        const auto& client = Host->GetMasterClient();
        auto channel = client->GetMasterChannelOrThrow(EMasterChannelKind::Leader);
        TObjectServiceProxy proxy(channel);

        auto path = GetOperationPath(OperationId) + "/@completion_transaction_id";
        auto req = TYPathProxy::Set(path);
        req->set_value(ConvertToYsonString(CompletionTransaction->GetId()).GetData());
        WaitFor(proxy.Execute(req))
            .ThrowOnError();
    }
}

void TOperationControllerBase::CommitCompletionTransaction()
{
    // Set committed flag.
    {
        const auto& client = Host->GetMasterClient();
        auto channel = client->GetMasterChannelOrThrow(EMasterChannelKind::Leader);
        TObjectServiceProxy proxy(channel);

        auto path = GetOperationPath(OperationId) + "/@committed";
        auto req = TYPathProxy::Set(path);
        SetTransactionId(req, CompletionTransaction->GetId());
        req->set_value(ConvertToYsonString(true).GetData());
        WaitFor(proxy.Execute(req))
            .ThrowOnError();
    }

    WaitFor(CompletionTransaction->Commit())
        .ThrowOnError();
    CompletionTransaction.Reset();

    CommitFinished = true;
}

void TOperationControllerBase::SleepInStage(EDelayInsideOperationCommitStage desiredStage)
{
    auto delay = Spec->TestingOperationOptions
        ? Spec->TestingOperationOptions->DelayInsideOperationCommit
        : TDuration();
    auto stage = Spec->TestingOperationOptions
        ? Spec->TestingOperationOptions->DelayInsideOperationCommitStage
        : EDelayInsideOperationCommitStage::Stage1;

    if (delay && stage == desiredStage) {
        WaitFor(TDelayedExecutor::MakeDelayed(delay));
    }
}

void TOperationControllerBase::SafeCommit()
{
    StartCompletionTransaction();

    SleepInStage(EDelayInsideOperationCommitStage::Stage1);
    BeginUploadOutputTables(UpdatingTables);
    SleepInStage(EDelayInsideOperationCommitStage::Stage2);
    TeleportOutputChunks();
    SleepInStage(EDelayInsideOperationCommitStage::Stage3);
    AttachOutputChunks(UpdatingTables);
    SleepInStage(EDelayInsideOperationCommitStage::Stage4);
    EndUploadOutputTables(UpdatingTables);
    SleepInStage(EDelayInsideOperationCommitStage::Stage5);

    CustomCommit();

    CommitCompletionTransaction();
    SleepInStage(EDelayInsideOperationCommitStage::Stage6);
    CommitTransactions();

    MasterConnector->UnregisterOperation(OperationId);

    LOG_INFO("Results committed");
}

void TOperationControllerBase::CommitTransactions()
{
    LOG_INFO("Committing scheduler transactions");

    AreTransactionsActive = false;

    CommitTransaction(InputTransaction);
    CommitTransaction(OutputTransaction);

    SleepInStage(EDelayInsideOperationCommitStage::Stage7);

    CommitTransaction(DebugOutputTransaction);

    LOG_INFO("Scheduler transactions committed");

    // NB: Never commit async transaction since it's used for writing Live Preview tables.
    AsyncSchedulerTransaction->Abort();
}

void TOperationControllerBase::TeleportOutputChunks()
{
    auto teleporter = New<TChunkTeleporter>(
        Config,
        AuthenticatedOutputMasterClient,
        CancelableInvoker,
        CompletionTransaction->GetId(),
        Logger);

    for (auto& table : OutputTables) {
        for (const auto& pair : table.OutputChunkTreeIds) {
            const auto& id = pair.second;
            if (TypeFromId(id) == EObjectType::ChunkList)
                continue;
            table.ChunkPropertiesUpdateNeeded = true;
            teleporter->RegisterChunk(id, table.CellTag);
        }
    }

    WaitFor(teleporter->Run())
        .ThrowOnError();
}

void TOperationControllerBase::AttachOutputChunks(const std::vector<TOutputTable*>& tableList)
{
    for (auto* table : tableList) {
        auto objectIdPath = FromObjectId(table->ObjectId);
        const auto& path = table->Path.GetPath();

        LOG_INFO("Attaching output chunks (Path: %v)",
            path);

        auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(
            EMasterChannelKind::Leader,
            table->CellTag);
        TChunkServiceProxy proxy(channel);

        // Split large outputs into separate requests.
        TChunkServiceProxy::TReqExecuteBatch::TAttachChunkTreesSubrequest* req = nullptr;
        TChunkServiceProxy::TReqExecuteBatchPtr batchReq;

        auto flushCurrentReq = [&] (bool requestStatistics) {
            if (req) {
                req->set_request_statistics(requestStatistics);

                auto batchRspOrError = WaitFor(batchReq->Invoke());
                THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error attaching chunks to output table %v",
                    path);

                const auto& batchRsp = batchRspOrError.Value();
                const auto& rsp = batchRsp->attach_chunk_trees_subresponses(0);
                if (requestStatistics) {
                    table->DataStatistics = rsp.statistics();
                }
            }

            req = nullptr;
            batchReq.Reset();
        };

        auto addChunkTree = [&] (const TChunkTreeId& chunkTreeId) {
            if (req && req->child_ids_size() >= Config->MaxChildrenPerAttachRequest) {
                // NB: No need for a statistics for an intermediate request.
                flushCurrentReq(false);
            }

            if (!req) {
                batchReq = proxy.ExecuteBatch();
                GenerateMutationId(batchReq);
                batchReq->set_suppress_upstream_sync(true);
                req = batchReq->add_attach_chunk_trees_subrequests();
                ToProto(req->mutable_parent_id(), table->OutputChunkListId);
            }

            ToProto(req->add_child_ids(), chunkTreeId);
        };

        if (table->TableUploadOptions.TableSchema.IsSorted() && ShouldVerifySortedOutput()) {
            // Sorted output generated by user operation requires rearranging.
            LOG_DEBUG("Sorting %v boundary key pairs %v", table->BoundaryKeys.size(), path);
            std::sort(
                table->BoundaryKeys.begin(),
                table->BoundaryKeys.end(),
                [&] (const TJobBoundaryKeys& lhs, const TJobBoundaryKeys& rhs) -> bool {
                    auto minKeyResult = CompareRows(lhs.MinKey, rhs.MinKey);
                    if (minKeyResult != 0) {
                        return minKeyResult < 0;
                    }
                    return lhs.MaxKey < rhs.MaxKey;
                });

            for (auto current = table->BoundaryKeys.begin(); current != table->BoundaryKeys.end(); ++current) {
                auto next = current + 1;
                if (next != table->BoundaryKeys.end()) {
                    int cmp = CompareRows(next->MinKey, current->MaxKey);

                    if (cmp < 0) {
                        THROW_ERROR_EXCEPTION("Output table %v is not sorted: job outputs have overlapping key ranges",
                            table->Path.GetPath())
                            << TErrorAttribute("current_range_max_key", current->MaxKey)
                            << TErrorAttribute("next_range_min_key", next->MinKey);
                    }

                    if (cmp == 0 && table->Options->ValidateUniqueKeys) {
                        THROW_ERROR_EXCEPTION("Output table %v contains duplicate keys: job outputs have overlapping key ranges",
                            table->Path.GetPath())
                            << TErrorAttribute("current_range_max_key", current->MaxKey)
                            << TErrorAttribute("next_range_min_key", next->MinKey);
                    }
                }

                if (current->ChunkTreeId) {
                    // Chunk tree may be absent if no data was written in the job.
                    addChunkTree(current->ChunkTreeId);
                }
            }
        } else {
            for (const auto& pair : table->OutputChunkTreeIds) {
                addChunkTree(pair.second);
            }
        }

        // NB: Don't forget to ask for the statistics in the last request.
        flushCurrentReq(true);

        LOG_INFO("Output chunks attached (Path: %v, Statistics: %v)",
            path,
            table->DataStatistics);
    }
}

void TOperationControllerBase::CustomCommit()
{ }

void TOperationControllerBase::EndUploadOutputTables(const std::vector<TOutputTable*>& tableList)
{
    auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Leader);
    TObjectServiceProxy proxy(channel);

    auto batchReq = proxy.ExecuteBatch();

    for (const auto* table : tableList) {
        auto objectIdPath = FromObjectId(table->ObjectId);
        const auto& path = table->Path.GetPath();

        LOG_INFO("Finishing upload to output table (Path: %v, Schema: %v)",
            path,
            table->TableUploadOptions.TableSchema);

        {
            auto req = TTableYPathProxy::EndUpload(objectIdPath);
            *req->mutable_statistics() = table->DataStatistics;
            req->set_chunk_properties_update_needed(table->ChunkPropertiesUpdateNeeded);
            ToProto(req->mutable_table_schema(), table->TableUploadOptions.TableSchema);
            req->set_schema_mode(static_cast<int>(table->TableUploadOptions.SchemaMode));
            req->set_optimize_for(static_cast<int>(table->TableUploadOptions.OptimizeFor));
            req->set_compression_codec(static_cast<int>(table->TableUploadOptions.CompressionCodec));
            req->set_erasure_codec(static_cast<int>(table->TableUploadOptions.ErasureCodec));

            SetTransactionId(req, table->UploadTransactionId);
            GenerateMutationId(req);
            batchReq->AddRequest(req, "end_upload");
        }
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error finishing upload to output tables");
}

void TOperationControllerBase::SafeOnJobStarted(const TJobId& jobId, TInstant startTime)
{
    LOG_DEBUG("Job started (JobId: %v)", jobId);

    auto joblet = GetJoblet(jobId);
    joblet->StartTime = startTime;
    joblet->LastActivityTime = startTime;

    LogEventFluently(ELogEventType::JobStarted)
        .Item("job_id").Value(jobId)
        .Item("operation_id").Value(OperationId)
        .Item("resource_limits").Value(joblet->ResourceLimits)
        .Item("node_address").Value(joblet->NodeDescriptor.Address)
        .Item("job_type").Value(joblet->JobType);
}

void TOperationControllerBase::UpdateMemoryDigests(TJobletPtr joblet, const TStatistics& statistics, bool resourceOverdraft)
{
    auto jobType = joblet->JobType;
    bool taskUpdateNeeded = false;

    auto userJobMaxMemoryUsage = FindNumericValue(statistics, "/user_job/max_memory");
    if (userJobMaxMemoryUsage) {
        auto* digest = GetUserJobMemoryDigest(jobType);
        double actualFactor = static_cast<double>(*userJobMaxMemoryUsage) / joblet->EstimatedResourceUsage.GetUserJobMemory();
        if (resourceOverdraft) {
            // During resource overdraft actual max memory values may be outdated,
            // since statistics are updated periodically. To ensure that digest converge to large enough
            // values we introduce additional factor.
            actualFactor = std::max(actualFactor, joblet->UserJobMemoryReserveFactor * Config->ResourceOverdraftFactor);
        }
        LOG_TRACE("Adding sample to the job proxy memory digest (JobType: %v, Sample: %v, JobId: %v)",
            jobType,
            actualFactor,
            joblet->JobId);
        digest->AddSample(actualFactor);
        taskUpdateNeeded = true;
    }

    auto jobProxyMaxMemoryUsage = FindNumericValue(statistics, "/job_proxy/max_memory");
    if (jobProxyMaxMemoryUsage) {
        auto* digest = GetJobProxyMemoryDigest(jobType);
        double actualFactor = static_cast<double>(*jobProxyMaxMemoryUsage) /
            (joblet->EstimatedResourceUsage.GetJobProxyMemory() + joblet->EstimatedResourceUsage.GetFootprintMemory());
        if (resourceOverdraft) {
            actualFactor = std::max(actualFactor, joblet->JobProxyMemoryReserveFactor * Config->ResourceOverdraftFactor);
        }
        LOG_TRACE("Adding sample to the user job memory digest (JobType: %v, Sample: %v, JobId: %v)",
            jobType,
            actualFactor,
            joblet->JobId);
        digest->AddSample(actualFactor);
        taskUpdateNeeded = true;
    }

    if (taskUpdateNeeded) {
        UpdateAllTasksIfNeeded();
    }
}

void TOperationControllerBase::InitializeHistograms()
{
    if (IsInputDataSizeHistogramSupported()) {
        EstimatedInputDataSizeHistogram_ = CreateHistogram();
        InputDataSizeHistogram_ = CreateHistogram();
    }
}

void TOperationControllerBase::AddValueToEstimatedHistogram(TJobletPtr joblet)
{
    if (EstimatedInputDataSizeHistogram_) {
        EstimatedInputDataSizeHistogram_->AddValue(joblet->InputStripeList->TotalDataSize);
    }
}

void TOperationControllerBase::RemoveValueFromEstimatedHistogram(TJobletPtr joblet)
{
    if (EstimatedInputDataSizeHistogram_) {
        EstimatedInputDataSizeHistogram_->RemoveValue(joblet->InputStripeList->TotalDataSize);
    }
}

void TOperationControllerBase::UpdateActualHistogram(const TStatistics& statistics)
{
    if (InputDataSizeHistogram_) {
        auto dataSize = FindNumericValue(statistics, "/data/input/uncompressed_data_size");
        if (dataSize && *dataSize > 0) {
            InputDataSizeHistogram_->AddValue(*dataSize);
        }
    }
}

void TOperationControllerBase::SafeOnJobCompleted(std::unique_ptr<TCompletedJobSummary> jobSummary)
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    auto jobId = jobSummary->Id;

    const auto& result = jobSummary->Result;

    const auto& schedulerResultExt = result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);

    // Validate all node ids of the output chunks and populate the local node directory.
    // In case any id is not known, abort the job.
    const auto& globalNodeDirectory = Host->GetNodeDirectory();
    for (const auto& chunkSpec : schedulerResultExt.output_chunk_specs()) {
        auto replicas = FromProto<TChunkReplicaList>(chunkSpec.replicas());
        for (auto replica : replicas) {
            auto nodeId = replica.GetNodeId();
            if (InputNodeDirectory->FindDescriptor(nodeId)) {
                continue;
            }

            const auto* descriptor = globalNodeDirectory->FindDescriptor(nodeId);
            if (!descriptor) {
                LOG_DEBUG("Job is considered aborted since its output contains unresolved node id "
                    "(JobId: %v, NodeId: %v)",
                    jobId,
                    nodeId);
                auto abortedJobSummary = std::make_unique<TAbortedJobSummary>(*jobSummary, EAbortReason::Other);
                OnJobAborted(std::move(abortedJobSummary));
                return;
            }

            InputNodeDirectory->AddDescriptor(nodeId, *descriptor);
        }
    }

    if (jobSummary->InterruptReason != EInterruptReason::None) {
        jobSummary->UnreadInputDataSlices = ExtractInputDataSlices(*jobSummary);
    }

    JobCounter.Completed(1, jobSummary->InterruptReason);

    auto joblet = GetJoblet(jobId);

    ParseStatistics(jobSummary.get(), joblet->StatisticsYson);

    const auto& statistics = *jobSummary->Statistics;

    UpdateMemoryDigests(joblet, statistics);
    UpdateActualHistogram(statistics);

    FinalizeJoblet(joblet, jobSummary.get());
    LogFinishedJobFluently(ELogEventType::JobCompleted, joblet, *jobSummary);

    UpdateJobStatistics(joblet, *jobSummary);
    joblet->SendJobMetrics(statistics, true);

    if (jobSummary->InterruptReason != EInterruptReason::None) {
        jobSummary->SplitJobCount = EstimateSplitJobCount(*jobSummary);
        LOG_DEBUG("Job interrupted (JobId: %v, InterruptReason: %v, UnreadDataSliceCount: %v, SplitJobCount: %v)",
            jobSummary->Id,
            jobSummary->InterruptReason,
            jobSummary->UnreadInputDataSlices.size(),
            jobSummary->SplitJobCount);
    }
    joblet->Task->OnJobCompleted(joblet, *jobSummary);
    if (JobSplitter_) {
        JobSplitter_->OnJobCompleted(*jobSummary);
    }

    // Statistics job state saved from jobSummary before moving jobSummary to ProcessFinishedJobResult.
    auto statisticsState = GetStatisticsJobState(joblet, jobSummary->State);

    ProcessFinishedJobResult(std::move(jobSummary), /* requestJobNodeCreation */ false);

    RemoveJoblet(jobId);

    UpdateTask(joblet->Task);

    if (IsCompleted()) {
        OnOperationCompleted(/* interrupted */ false);
        return;
    }

    auto statisticsSuffix = JobHelper.GetStatisticsSuffix(statisticsState, joblet->JobType);

    if (RowCountLimitTableIndex) {
        switch (joblet->JobType) {
            case EJobType::Map:
            case EJobType::OrderedMap:
            case EJobType::SortedReduce:
            case EJobType::JoinReduce:
            case EJobType::PartitionReduce: {
                auto path = Format("/data/output/%v/row_count%v", *RowCountLimitTableIndex, statisticsSuffix);
                i64 count = GetNumericValue(JobStatistics, path);
                if (count >= RowCountLimit) {
                    OnOperationCompleted(true /* interrupted */);
                }
                break;
            }
            default:
                break;
        }
    }

    CheckFailedJobsStatusReceived();
}

void TOperationControllerBase::SafeOnJobFailed(std::unique_ptr<TFailedJobSummary> jobSummary)
{
    auto jobId = jobSummary->Id;
    const auto& result = jobSummary->Result;

    auto error = FromProto<TError>(result.error());

    JobCounter.Failed(1);

    auto joblet = GetJoblet(jobId);

    ParseStatistics(jobSummary.get(), joblet->StatisticsYson);

    FinalizeJoblet(joblet, jobSummary.get());
    LogFinishedJobFluently(ELogEventType::JobFailed, joblet, *jobSummary)
        .Item("error").Value(error);

    joblet->SendJobMetrics(*jobSummary->Statistics, true);
    UpdateJobStatistics(joblet, *jobSummary);

    joblet->Task->OnJobFailed(joblet, *jobSummary);
    if (JobSplitter_) {
        JobSplitter_->OnJobFailed(*jobSummary);
    }

    ProcessFinishedJobResult(std::move(jobSummary), /* requestJobNodeCreation */ true);

    RemoveJoblet(jobId);

    if (error.Attributes().Get<bool>("fatal", false)) {
        auto wrappedError = TError("Job failed with fatal error") << error;
        OnOperationFailed(wrappedError);
        return;
    }

    int failedJobCount = JobCounter.GetFailed();
    int maxFailedJobCount = Spec->MaxFailedJobCount;
    if (failedJobCount >= maxFailedJobCount) {
        OnOperationFailed(TError("Failed jobs limit exceeded")
            << TErrorAttribute("max_failed_job_count", maxFailedJobCount));
    }

    CheckFailedJobsStatusReceived();
}

void TOperationControllerBase::SafeOnJobAborted(std::unique_ptr<TAbortedJobSummary> jobSummary)
{
    auto jobId = jobSummary->Id;
    auto abortReason = jobSummary->AbortReason;

    JobCounter.Aborted(1, abortReason);

    auto joblet = GetJoblet(jobId);

    ParseStatistics(jobSummary.get(), joblet->StatisticsYson);
    const auto& statistics = *jobSummary->Statistics;

    if (abortReason == EAbortReason::ResourceOverdraft) {
        UpdateMemoryDigests(joblet, statistics, true /* resourceOverdraft */);
    }

    if (jobSummary->ShouldLog) {
        FinalizeJoblet(joblet, jobSummary.get());
        LogFinishedJobFluently(ELogEventType::JobAborted, joblet, *jobSummary)
            .Item("reason").Value(abortReason);

        UpdateJobStatistics(joblet, *jobSummary);
    }
    joblet->SendJobMetrics(statistics, true);

    if (abortReason == EAbortReason::FailedChunks) {
        const auto& result = jobSummary->Result;
        const auto& schedulerResultExt = result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);
        for (const auto& chunkId : schedulerResultExt.failed_chunk_ids()) {
            OnChunkFailed(FromProto<TChunkId>(chunkId));
        }
    }

    joblet->Task->OnJobAborted(joblet, *jobSummary);

    if (JobSplitter_) {
        JobSplitter_->OnJobAborted(*jobSummary);
    }

    bool requestJobNodeCreation = (abortReason == EAbortReason::UserRequest);
    ProcessFinishedJobResult(std::move(jobSummary), requestJobNodeCreation);

    RemoveJoblet(jobId);

    if (abortReason == EAbortReason::AccountLimitExceeded) {
        Host->OnOperationSuspended(OperationId, TError("Account limit exceeded"));
    }

    CheckFailedJobsStatusReceived();
}

void TOperationControllerBase::SafeOnJobRunning(std::unique_ptr<TRunningJobSummary> jobSummary)
{
    const auto& jobId = jobSummary->Id;
    auto joblet = GetJoblet(jobSummary->Id);

    joblet->Progress = jobSummary->Progress;

    if (jobSummary->StatisticsYson) {
        joblet->StatisticsYson = jobSummary->StatisticsYson;
        ParseStatistics(jobSummary.get());

        joblet->SendJobMetrics(*jobSummary->Statistics, false);

        if (JobSplitter_) {
            JobSplitter_->OnJobRunning(*jobSummary);
            if (GetPendingJobCount() == 0 && JobSplitter_->IsJobSplittable(jobId)) {
                auto jobHost = Host->GetJobHost(jobId);
                LOG_DEBUG("Job is ready to be split (JobId: %v)", jobId);
                jobHost->InterruptJob(EInterruptReason::JobSplit);
            }
        }

        auto asyncResult = BIND(&BuildBriefStatistics, Passed(std::move(jobSummary)))
            .AsyncVia(Host->GetStatisticsAnalyzerInvoker())
            .Run();

        // Resulting future is dropped intentionally.
        asyncResult.Subscribe(BIND(
            &TOperationControllerBase::AnalyzeBriefStatistics,
            MakeStrong(this),
            joblet,
            Config->SuspiciousInactivityTimeout,
            Config->SuspiciousCpuUsageThreshold,
            Config->SuspiciousInputPipeIdleTimeFraction)
            .Via(GetInvoker()));
    }
}

void TOperationControllerBase::FinalizeJoblet(
    const TJobletPtr& joblet,
    TJobSummary* jobSummary)
{
    YCHECK(jobSummary->Statistics);
    YCHECK(jobSummary->FinishTime);

    auto& statistics = *jobSummary->Statistics;
    joblet->FinishTime = *(jobSummary->FinishTime);

    {
        auto duration = joblet->FinishTime - joblet->StartTime;
        statistics.AddSample("/time/total", duration.MilliSeconds());
    }

    if (jobSummary->PrepareDuration) {
        statistics.AddSample("/time/prepare", jobSummary->PrepareDuration->MilliSeconds());
    }
    if (jobSummary->DownloadDuration) {
        statistics.AddSample("/time/artifacts_download", jobSummary->DownloadDuration->MilliSeconds());
    }
    if (jobSummary->ExecDuration) {
        statistics.AddSample("/time/exec", jobSummary->ExecDuration->MilliSeconds());
    }

    statistics.AddSample("/job_proxy/memory_reserve_factor_x10000", static_cast<int>(1e4 * joblet->JobProxyMemoryReserveFactor));
}

void TOperationControllerBase::BuildJobAttributes(
    const TJobInfoPtr& job,
    EJobState state,
    bool outputStatistics,
    NYson::IYsonConsumer* consumer) const
{
    static const auto EmptyMapYson = TYsonString("{}");

    BuildYsonMapFluently(consumer)
        .Item("job_type").Value(FormatEnum(job->JobType))
        .Item("state").Value(state)
        .Item("address").Value(job->NodeDescriptor.Address)
        .Item("start_time").Value(job->StartTime)
        .Item("account").Value(job->Account)
        .Item("progress").Value(job->Progress)
        .Item("brief_statistics")
            .Value(job->BriefStatistics)
        .DoIf(outputStatistics, [&] (TFluentMap fluent) {
            fluent.Item("statistics")
                .Value(job->StatisticsYson ? job->StatisticsYson : EmptyMapYson);
        })
        .Item("suspicious").Value(job->Suspicious);
}

void TOperationControllerBase::BuildFinishedJobAttributes(
    const TFinishedJobInfoPtr& job,
    bool outputStatistics,
    NYson::IYsonConsumer* consumer) const
{
    BuildJobAttributes(job, job->Summary.State, outputStatistics, consumer);

    const auto& summary = job->Summary;
    BuildYsonMapFluently(consumer)
        .Item("finish_time").Value(job->FinishTime)
        .DoIf(summary.State == EJobState::Failed, [&] (TFluentMap fluent) {
            auto error = FromProto<TError>(summary.Result.error());
            fluent.Item("error").Value(error);
        })
        .DoIf(summary.Result.HasExtension(TSchedulerJobResultExt::scheduler_job_result_ext),
            [&] (TFluentMap fluent)
        {
            const auto& schedulerResultExt = summary.Result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);
            fluent.Item("core_infos").Value(schedulerResultExt.core_infos());
        })
        .DoIf(static_cast<bool>(job->InputPaths), [&] (TFluentMap fluent) {
            fluent.Item("input_paths").Value(job->InputPaths);
        });
}


TFluentLogEvent TOperationControllerBase::LogFinishedJobFluently(
    ELogEventType eventType,
    const TJobletPtr& joblet,
    const TJobSummary& jobSummary)
{
    return LogEventFluently(eventType)
        .Item("job_id").Value(joblet->JobId)
        .Item("operation_id").Value(OperationId)
        .Item("start_time").Value(joblet->StartTime)
        .Item("finish_time").Value(joblet->FinishTime)
        .Item("resource_limits").Value(joblet->ResourceLimits)
        .Item("statistics").Value(jobSummary.Statistics)
        .Item("node_address").Value(joblet->NodeDescriptor.Address)
        .Item("job_type").Value(joblet->JobType);
}

IYsonConsumer* TOperationControllerBase::GetEventLogConsumer()
{
    VERIFY_THREAD_AFFINITY_ANY();

    return EventLogTableConsumer_.get();
}

void TOperationControllerBase::OnChunkFailed(const TChunkId& chunkId)
{
    if (chunkId == NullChunkId) {
        LOG_WARNING("Incompatible unavailable chunk found; deprecated node version");
        return;
    }

    auto it = InputChunkMap.find(chunkId);
    if (it == InputChunkMap.end()) {
        LOG_DEBUG("Intermediate chunk has failed (ChunkId: %v)", chunkId);
        if (!OnIntermediateChunkUnavailable(chunkId)) {
            return;
        }

        IntermediateChunkScraper->Start();
    } else {
        LOG_DEBUG("Input chunk has failed (ChunkId: %v)", chunkId);
        OnInputChunkUnavailable(chunkId, &it->second);
    }
}

void TOperationControllerBase::SafeOnIntermediateChunkLocated(const TChunkId& chunkId, const TChunkReplicaList& replicas)
{
    // Intermediate chunks are always replicated.
    if (IsUnavailable(replicas, NErasure::ECodec::None)) {
        OnIntermediateChunkUnavailable(chunkId);
    }
}

void TOperationControllerBase::SafeOnInputChunkLocated(const TChunkId& chunkId, const TChunkReplicaList& replicas)
{
    auto it = InputChunkMap.find(chunkId);
    YCHECK(it != InputChunkMap.end());

    auto& descriptor = it->second;
    YCHECK(!descriptor.InputChunks.empty());
    auto& chunkSpec = descriptor.InputChunks.front();
    auto codecId = NErasure::ECodec(chunkSpec->GetErasureCodec());

    if (IsUnavailable(replicas, codecId, IsParityReplicasFetchEnabled())) {
        OnInputChunkUnavailable(chunkId, &descriptor);
    } else {
        OnInputChunkAvailable(chunkId, replicas, &descriptor);
    }
}

void TOperationControllerBase::OnInputChunkAvailable(
    const TChunkId& chunkId,
    const TChunkReplicaList& replicas,
    TInputChunkDescriptor* descriptor)
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    if (descriptor->State != EInputChunkState::Waiting) {
        return;
    }

    LOG_TRACE("Input chunk is available (ChunkId: %v)", chunkId);

    --UnavailableInputChunkCount;
    YCHECK(UnavailableInputChunkCount >= 0);

    if (UnavailableInputChunkCount == 0) {
        InputChunkScraper->Stop();
    }

    // Update replicas in place for all input chunks with current chunkId.
    for (auto& chunkSpec : descriptor->InputChunks) {
        chunkSpec->SetReplicaList(replicas);
    }

    descriptor->State = EInputChunkState::Active;

    for (const auto& inputStripe : descriptor->InputStripes) {
        --inputStripe.Stripe->WaitingChunkCount;
        if (inputStripe.Stripe->WaitingChunkCount > 0)
            continue;

        auto task = inputStripe.Task;
        task->GetChunkPoolInput()->Resume(inputStripe.Cookie, inputStripe.Stripe);
        if (task->HasInputLocality()) {
            AddTaskLocalityHint(task, inputStripe.Stripe);
        }
        AddTaskPendingHint(task);
    }
}

void TOperationControllerBase::OnInputChunkUnavailable(const TChunkId& chunkId, TInputChunkDescriptor* descriptor)
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    if (descriptor->State != EInputChunkState::Active) {
        return;
    }

    ++ChunkLocatedCallCount;
    if (ChunkLocatedCallCount >= Config->ChunkScraper->MaxChunksPerRequest) {
        ChunkLocatedCallCount = 0;
        LOG_DEBUG("Located another batch of chunks (Count: %v, UnavailableInputChunkCount: %v)",
            Config->ChunkScraper->MaxChunksPerRequest,
            UnavailableInputChunkCount);
    }

    LOG_TRACE("Input chunk is unavailable (ChunkId: %v)", chunkId);

    ++UnavailableInputChunkCount;

    switch (Spec->UnavailableChunkTactics) {
        case EUnavailableChunkAction::Fail:
            OnOperationFailed(TError("Input chunk %v is unavailable",
                chunkId));
            break;

        case EUnavailableChunkAction::Skip: {
            descriptor->State = EInputChunkState::Skipped;
            for (const auto& inputStripe : descriptor->InputStripes) {
                inputStripe.Task->GetChunkPoolInput()->Suspend(inputStripe.Cookie);

                inputStripe.Stripe->DataSlices.erase(
                    std::remove_if(
                        inputStripe.Stripe->DataSlices.begin(),
                        inputStripe.Stripe->DataSlices.end(),
                        [&] (TInputDataSlicePtr slice) {
                            try {
                                return chunkId == slice->GetSingleUnversionedChunkOrThrow()->ChunkId();
                            } catch (const std::exception& ex) {
                                //FIXME(savrus) allow data slices to be unavailable.
                                THROW_ERROR_EXCEPTION("Dynamic table chunk became unavailable") << ex;
                            }
                        }),
                    inputStripe.Stripe->DataSlices.end());

                // Reinstall patched stripe.
                inputStripe.Task->GetChunkPoolInput()->Resume(inputStripe.Cookie, inputStripe.Stripe);
                AddTaskPendingHint(inputStripe.Task);
            }
            InputChunkScraper->Start();
            break;
        }

        case EUnavailableChunkAction::Wait: {
            descriptor->State = EInputChunkState::Waiting;
            for (const auto& inputStripe : descriptor->InputStripes) {
                if (inputStripe.Stripe->WaitingChunkCount == 0) {
                    inputStripe.Task->GetChunkPoolInput()->Suspend(inputStripe.Cookie);
                }
                ++inputStripe.Stripe->WaitingChunkCount;
            }
            InputChunkScraper->Start();
            break;
        }

        default:
            Y_UNREACHABLE();
    }
}

bool TOperationControllerBase::OnIntermediateChunkUnavailable(const TChunkId& chunkId)
{
    auto it = ChunkOriginMap.find(chunkId);
    YCHECK(it != ChunkOriginMap.end());
    auto completedJob = it->second;
    if (completedJob->Lost)
        return false;

    LOG_DEBUG("Job is lost (Address: %v, JobId: %v, SourceTask: %v, OutputCookie: %v, InputCookie: %v)",
        completedJob->NodeDescriptor.Address,
        completedJob->JobId,
        completedJob->SourceTask->GetId(),
        completedJob->OutputCookie,
        completedJob->InputCookie);

    JobCounter.Lost(1);
    completedJob->Lost = true;
    completedJob->DestinationPool->Suspend(completedJob->InputCookie);
    completedJob->SourceTask->GetChunkPoolOutput()->Lost(completedJob->OutputCookie);
    completedJob->SourceTask->OnJobLost(completedJob);
    AddTaskPendingHint(completedJob->SourceTask);
    return true;
}

bool TOperationControllerBase::AreForeignTablesSupported() const
{
    return false;
}

bool TOperationControllerBase::IsOutputLivePreviewSupported() const
{
    return false;
}

bool TOperationControllerBase::IsIntermediateLivePreviewSupported() const
{
    return false;
}

void TOperationControllerBase::OnTransactionAborted(const TTransactionId& transactionId)
{
    if (transactionId == UserTransactionId) {
        Host->OnUserTransactionAborted(OperationId);
    } else {
        {
            // Check that transactionId is presented in controller.
            bool found = false;
            for (const auto& transaction : GetTransactions()) {
                if (transaction->GetId() == transactionId) {
                    found = true;
                    break;
                }
            }
            YCHECK(found);
        }

        OnOperationFailed(
            TError("Controller transaction %v has expired or was aborted",
                transactionId),
            /* flush */ false);
    }
}

std::vector<ITransactionPtr> TOperationControllerBase::GetTransactions()
{
    if (AreTransactionsActive) {
        std::vector<ITransactionPtr> transactions;
        for (auto transaction : {
                // NB: User transaction must be returned first to correctly detect that operation aborted due to user transaction abort.
                UserTransaction,
                AsyncSchedulerTransaction,
                InputTransaction,
                OutputTransaction,
                CompletionTransaction,
                DebugOutputTransaction
            })
        {
            if (transaction) {
                transactions.push_back(transaction);
            }
        }
        return transactions;
    } else {
        return {};
    }
}

bool TOperationControllerBase::IsInputDataSizeHistogramSupported() const
{
    return false;
}

void TOperationControllerBase::SafeAbort()
{
    LOG_INFO("Aborting operation controller");

    // NB: Error ignored since we cannot do anything with it.
    WaitFor(MasterConnector->FlushOperationNode(OperationId));

    AreTransactionsActive = false;

    // Skip committing anything if operation controller already tried to commit results.
    if (!CommitFinished) {
        try {
            if (StderrTable) {
                BeginUploadOutputTables({StderrTable.GetPtr()});
                AttachOutputChunks({StderrTable.GetPtr()});
                EndUploadOutputTables({StderrTable.GetPtr()});
            }

            if (CoreTable) {
                BeginUploadOutputTables({CoreTable.GetPtr()});
                AttachOutputChunks({CoreTable.GetPtr()});
                EndUploadOutputTables({CoreTable.GetPtr()});
            }

            CommitTransaction(DebugOutputTransaction);
        } catch (const std::exception& ex) {
            // Bad luck we can't commit transaction.
            // Such a pity can happen for example if somebody aborted our transaction manualy.
            LOG_ERROR(ex, "Failed to commit debug output transaction");
        }
    }

    std::vector<TFuture<void>> abortTransactionFutures;

    auto abortTransaction = [&] (ITransactionPtr transaction) {
        if (transaction) {
            abortTransactionFutures.push_back(transaction->Abort());
        }
    };

    abortTransaction(InputTransaction);
    abortTransaction(OutputTransaction);
    abortTransaction(AsyncSchedulerTransaction);

    WaitFor(Combine(abortTransactionFutures))
        .ThrowOnError();

    State = EControllerState::Finished;

    CancelableContext->Cancel();

    MasterConnector->UnregisterOperation(OperationId);

    LOG_INFO("Operation controller aborted");
}

void TOperationControllerBase::SafeForget()
{
    LOG_INFO("Forgetting operation");

    CancelableContext->Cancel();

    Forgotten = true;

    MasterConnector->UnregisterOperation(OperationId);

    LOG_INFO("Operation forgotten");
}

void TOperationControllerBase::SafeComplete()
{
    BIND(&TOperationControllerBase::OnOperationCompleted, MakeStrong(this), true /* interrupted */)
        .Via(GetCancelableInvoker())
        .Run();
}

void TOperationControllerBase::CheckTimeLimit()
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    auto timeLimit = GetTimeLimit();
    if (timeLimit) {
        if (TInstant::Now() - StartTime > *timeLimit) {
            OnOperationTimeLimitExceeded();
        }
    }
}

void TOperationControllerBase::CheckAvailableExecNodes()
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    if (ShouldSkipSanityCheck()) {
        return;
    }

    if (GetExecNodeDescriptors().empty()) {
        auto timeout = DurationToCpuDuration(Spec->AvailableNodesMissingTimeout);
        if (AvaialableNodesLastSeenTime_ + timeout < GetCpuInstant()) {
            OnOperationFailed(TError("No online nodes match operation scheduling tag filter")
                << TErrorAttribute("operation_id", OperationId)
                << TErrorAttribute("scheduling_tag_filter", Spec->SchedulingTagFilter));
        }
    } else {
        AvaialableNodesLastSeenTime_ = GetCpuInstant();
    }
}

void TOperationControllerBase::AnalyzeTmpfsUsage() const
{
    if (!Config->EnableTmpfs) {
        return;
    }

    yhash_map<EJobType, i64> maximumUsedTmfpsSizePerJobType;
    yhash_map<EJobType, TUserJobSpecPtr> userJobSpecPerJobType;

    for (const auto& task : Tasks) {
        const auto& userJobSpecPtr = task->GetUserJobSpec();
        if (!userJobSpecPtr || !userJobSpecPtr->TmpfsPath || !userJobSpecPtr->TmpfsSize) {
            continue;
        }

        auto maxUsedTmpfsSize = task->GetMaximumUsedTmpfsSize();
        if (!maxUsedTmpfsSize) {
            continue;
        }

        auto jobType = task->GetJobType();

        auto it = maximumUsedTmfpsSizePerJobType.find(jobType);
        if (it == maximumUsedTmfpsSizePerJobType.end()) {
            maximumUsedTmfpsSizePerJobType[jobType] = *maxUsedTmpfsSize;
        } else {
            it->second = std::max(it->second, *maxUsedTmpfsSize);
        }

        userJobSpecPerJobType.insert(std::make_pair(jobType, userJobSpecPtr));
    }

    std::vector<TError> innerErrors;

    double minUnusedSpaceRatio = 1.0 - Config->OperationAlertsConfig->TmpfsAlertMaxUnusedSpaceRatio;

    for (const auto& pair : maximumUsedTmfpsSizePerJobType) {
        const auto& userJobSpecPtr = userJobSpecPerJobType[pair.first];
        auto maxUsedTmpfsSize = pair.second;

        bool minUnusedSpaceThresholdOvercome = userJobSpecPtr->TmpfsSize.Get() - maxUsedTmpfsSize >
            Config->OperationAlertsConfig->TmpfsAlertMinUnusedSpaceThreshold;
        bool minUnusedSpaceRatioViolated = maxUsedTmpfsSize <
            minUnusedSpaceRatio * userJobSpecPtr->TmpfsSize.Get();

        if (minUnusedSpaceThresholdOvercome && minUnusedSpaceRatioViolated) {
            auto error = TError(
                "Jobs of type %Qlv use less than %.1f%% of requested tmpfs size",
                pair.first, minUnusedSpaceRatio * 100.0);
            innerErrors.push_back(error
                << TErrorAttribute("max_used_tmpfs_size", maxUsedTmpfsSize)
                << TErrorAttribute("tmpfs_size", *userJobSpecPtr->TmpfsSize));
        }
    }

    TError error;
    if (!innerErrors.empty()) {
        error = TError(
            "Operation has jobs that use less than %.1f%% of requested tmpfs size; "
            "consider specifying tmpfs size closer to actual usage",
            minUnusedSpaceRatio * 100.0) << innerErrors;
    }

    Host->SetOperationAlert(
        OperationId,
        EOperationAlertType::UnusedTmpfsSpace,
        error);
}

void TOperationControllerBase::AnalyzeInputStatistics() const
{
    TError error;
    if (UnavailableInputChunkCount > 0) {
        error = TError(
            "Some input chunks are not available; "
            "the relevant parts of computation will be suspended");
    }

    Host->SetOperationAlert(OperationId, EOperationAlertType::LostInputChunks, error);
}

void TOperationControllerBase::AnalyzeIntermediateJobsStatistics() const
{
    TError error;
    if (JobCounter.GetLost() > 0) {
        error = TError(
            "Some intermediate outputs were lost and will be regenerated; "
            "operation will take longer than usual");
    }

    Host->SetOperationAlert(OperationId, EOperationAlertType::LostIntermediateChunks, error);
}

void TOperationControllerBase::AnalyzePartitionHistogram() const
{ }

void TOperationControllerBase::AnalyzeAbortedJobs() const
{
    auto aggregateTimeForJobState = [&] (EJobState state) {
        i64 sum = 0;
        for (auto type : TEnumTraits<EJobType>::GetDomainValues()) {
            auto value = FindNumericValue(
                JobStatistics,
                Format("/time/total/$/%lv/%lv", state, type));

            if (value) {
                sum += *value;
            }
        }

        return sum;
    };

    i64 completedJobsTime = aggregateTimeForJobState(EJobState::Completed);
    i64 abortedJobsTime = aggregateTimeForJobState(EJobState::Aborted);
    double abortedJobsTimeRatio = 1.0;
    if (completedJobsTime > 0) {
        abortedJobsTimeRatio = 1.0 * abortedJobsTime / completedJobsTime;
    }

    TError error;
    if (abortedJobsTime > Config->OperationAlertsConfig->AbortedJobsAlertMaxAbortedTime &&
        abortedJobsTimeRatio > Config->OperationAlertsConfig->AbortedJobsAlertMaxAbortedTimeRatio)
    {
        error = TError(
            "Aborted jobs time ratio is too high, scheduling is likely to be inefficient; "
            "consider increasing job count to make individual jobs smaller")
                << TErrorAttribute("aborted_jobs_time_ratio", abortedJobsTimeRatio);
    }

    Host->SetOperationAlert(OperationId, EOperationAlertType::LongAbortedJobs, error);
}

void TOperationControllerBase::AnalyzeJobsIOUsage() const
{
    std::vector<TError> innerErrors;

    for (auto jobType : TEnumTraits<EJobType>::GetDomainValues()) {
        auto value = FindNumericValue(
            JobStatistics,
            "/user_job/woodpecker/$/completed/" + FormatEnum(jobType));

        if (value && *value > 0) {
            innerErrors.emplace_back("Detected excessive disk IO in %Qlv jobs", jobType);
        }
    }

    TError error;
    if (!innerErrors.empty()) {
        error = TError("Detected excessive disk IO in jobs; consider optimizing disk usage")
            << innerErrors;
    }

    Host->SetOperationAlert(OperationId, EOperationAlertType::ExcessiveDiskUsage, error);
}

void TOperationControllerBase::AnalyzeJobsDuration() const
{
    if (OperationType == EOperationType::RemoteCopy || OperationType == EOperationType::Erase) {
        return;
    }

    auto operationDuration = TInstant::Now() - StartTime;

    std::vector<TError> innerErrors;

    for (auto jobType : GetSupportedJobTypesForJobsDurationAnalyzer()) {
        auto completedJobsSummary = FindSummary(
            JobStatistics,
            "/time/total/$/completed/" + FormatEnum(jobType));

        if (!completedJobsSummary) {
            continue;
        }

        auto maxJobDuration = TDuration::MilliSeconds(completedJobsSummary->GetMax());
        auto completedJobCount = completedJobsSummary->GetCount();

        if (completedJobCount > Config->OperationAlertsConfig->ShortJobsAlertMinJobCount &&
            operationDuration > maxJobDuration * 2 &&
            maxJobDuration < Config->OperationAlertsConfig->ShortJobsAlertMinJobDuration)
        {
            auto error = TError(
                "Duration of %Qlv jobs is less than %v seconds, try increasing %v in operation spec",
                jobType,
                Config->OperationAlertsConfig->ShortJobsAlertMinJobDuration.Seconds(),
                GetDataSizeParameterNameForJob(jobType))
                    << TErrorAttribute("max_job_duration", maxJobDuration);

            innerErrors.push_back(error);
        }
    }

    TError error;
    if (!innerErrors.empty()) {
        error = TError("Operation has jobs with duration is less than %v seconds, "
                       "that leads to large overhead costs for scheduling",
                       Config->OperationAlertsConfig->ShortJobsAlertMinJobDuration)
            << innerErrors;
    }

    Host->SetOperationAlert(OperationId, EOperationAlertType::ShortJobsDuration, error);
}

void TOperationControllerBase::AnalyzeScheduleJobStatistics() const
{
    auto jobSpecThrottlerActivationCount = ScheduleJobStatistics_->Failed[EScheduleJobFailReason::JobSpecThrottling];
    auto activationCountThreshold = Config->OperationAlertsConfig->JobSpecThrottlingAlertActivationCountThreshold;

    TError error;
    if (jobSpecThrottlerActivationCount > activationCountThreshold) {
        error = TError(
            "Excessive job spec throttling is detected. Usage ratio of operation can be "
            "significatly less than fair share ratio")
                << TErrorAttribute("job_spec_throttler_activation_count", jobSpecThrottlerActivationCount);
    }
    Host->SetOperationAlert(OperationId, EOperationAlertType::ExcessiveJobSpecThrottling, error);
}

void TOperationControllerBase::AnalyzeOperationProgess() const
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    AnalyzeTmpfsUsage();
    AnalyzeInputStatistics();
    AnalyzeIntermediateJobsStatistics();
    AnalyzePartitionHistogram();
    AnalyzeAbortedJobs();
    AnalyzeJobsIOUsage();
    AnalyzeJobsDuration();
    AnalyzeScheduleJobStatistics();
}

void TOperationControllerBase::UpdateCachedMaxAvailableExecNodeResources()
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    const auto& nodeDescriptors = GetExecNodeDescriptors();

    TJobResources maxAvailableResources;
    for (const auto& descriptor : nodeDescriptors) {
        maxAvailableResources = Max(maxAvailableResources, descriptor.ResourceLimits);
    }

    CachedMaxAvailableExecNodeResources_ = maxAvailableResources;
}

void TOperationControllerBase::CheckMinNeededResourcesSanity()
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    if (ShouldSkipSanityCheck()) {
        return;
    }

    for (const auto& task : Tasks) {
        if (task->GetPendingJobCount() == 0) {
            continue;
        }

        const auto& neededResources = task->GetMinNeededResources();
        if (!Dominates(*CachedMaxAvailableExecNodeResources_, neededResources)) {
            OnOperationFailed(
                TError("No online node can satisfy the resource demand")
                    << TErrorAttribute("task_id", task->GetId())
                    << TErrorAttribute("needed_resources", neededResources)
                    << TErrorAttribute("max_available_resources", *CachedMaxAvailableExecNodeResources_));
        }
    }
}

TScheduleJobResultPtr TOperationControllerBase::SafeScheduleJob(
    ISchedulingContextPtr context,
    const TJobResources& jobLimits)
{
    if (Spec->TestingOperationOptions) {
        if (Spec->TestingOperationOptions->SchedulingDelayType == ESchedulingDelayType::Async) {
            WaitFor(TDelayedExecutor::MakeDelayed(Spec->TestingOperationOptions->SchedulingDelay));
        } else {
            Sleep(Spec->TestingOperationOptions->SchedulingDelay);
        }
    }

    // SafeScheduleJob must be synchronous; context switches are prohibited.
    TContextSwitchGuard contextSwitchGuard([] { Y_UNREACHABLE(); });

    TScopedTimer timer;
    auto scheduleJobResult = New<TScheduleJobResult>();
    DoScheduleJob(context.Get(), jobLimits, scheduleJobResult.Get());
    if (scheduleJobResult->JobStartRequest) {
        JobCounter.Start(1);
    }
    scheduleJobResult->Duration = timer.GetElapsed();

    ScheduleJobStatistics_->RecordJobResult(scheduleJobResult);

    auto now = NProfiling::GetCpuInstant();
    if (now > ScheduleJobStatisticsLogDeadline_) {
        LOG_DEBUG("Schedule job statistics (Count: %v, TotalDuration: %v, FailureReasons: %v)",
            ScheduleJobStatistics_->Count,
            ScheduleJobStatistics_->Duration,
            ScheduleJobStatistics_->Failed);
        ScheduleJobStatisticsLogDeadline_ = now + NProfiling::DurationToCpuDuration(Config->ScheduleJobStatisticsLogBackoff);
    }

    return scheduleJobResult;
}

void TOperationControllerBase::UpdateConfig(TSchedulerConfigPtr config)
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    Config = config;
}

void TOperationControllerBase::CustomizeJoblet(TJobletPtr /* joblet */)
{ }

void TOperationControllerBase::CustomizeJobSpec(TJobletPtr /* joblet */, TJobSpec* /* jobSpec */)
{ }

void TOperationControllerBase::RegisterTask(TTaskPtr task)
{
    Tasks.push_back(std::move(task));
}

void TOperationControllerBase::RegisterTaskGroup(TTaskGroupPtr group)
{
    TaskGroups.push_back(std::move(group));
}

void TOperationControllerBase::UpdateTask(TTaskPtr task)
{
    int oldPendingJobCount = CachedPendingJobCount;
    int newPendingJobCount = CachedPendingJobCount + task->GetPendingJobCountDelta();
    CachedPendingJobCount = newPendingJobCount;

    int oldTotalJobCount = JobCounter.GetTotal();
    JobCounter.Increment(task->GetTotalJobCountDelta());
    int newTotalJobCount = JobCounter.GetTotal();

    IncreaseNeededResources(task->GetTotalNeededResourcesDelta());

    LOG_DEBUG_IF(
        newPendingJobCount != oldPendingJobCount || newTotalJobCount != oldTotalJobCount,
        "Task updated (Task: %v, PendingJobCount: %v -> %v, TotalJobCount: %v -> %v, NeededResources: %v)",
        task->GetId(),
        oldPendingJobCount,
        newPendingJobCount,
        oldTotalJobCount,
        newTotalJobCount,
        FormatResources(CachedNeededResources));

    i64 outputTablesTimesJobsCount = OutputTables.size() * newTotalJobCount;
    if (outputTablesTimesJobsCount > Config->MaxOutputTablesTimesJobsCount) {
        OnOperationFailed(TError(
                "Maximum allowed number of output tables times job count violated: %v > %v",
                outputTablesTimesJobsCount,
                Config->MaxOutputTablesTimesJobsCount)
            << TErrorAttribute("output_table_count", OutputTables.size())
            << TErrorAttribute("job_count", newTotalJobCount));
    }

    task->CheckCompleted();
}

void TOperationControllerBase::UpdateAllTasks()
{
    for (const auto& task: Tasks) {
        UpdateTask(task);
    }
}

void TOperationControllerBase::UpdateAllTasksIfNeeded()
{
    auto now = NProfiling::GetCpuInstant();
    if (now < TaskUpdateDeadline_) {
        return;
    }
    UpdateAllTasks();
    TaskUpdateDeadline_ = now + NProfiling::DurationToCpuDuration(Config->TaskUpdatePeriod);
}

void TOperationControllerBase::MoveTaskToCandidates(
    TTaskPtr task,
    std::multimap<i64, TTaskPtr>& candidateTasks)
{
    const auto& neededResources = task->GetMinNeededResources();
    i64 minMemory = neededResources.GetMemory();
    candidateTasks.insert(std::make_pair(minMemory, task));
    LOG_DEBUG("Task moved to candidates (Task: %v, MinMemory: %v)",
        task->GetId(),
        minMemory / (1024 * 1024));

}

void TOperationControllerBase::AddTaskPendingHint(TTaskPtr task)
{
    if (task->GetPendingJobCount() > 0) {
        auto group = task->GetGroup();
        if (group->NonLocalTasks.insert(task).second) {
            LOG_DEBUG("Task pending hint added (Task: %v)", task->GetId());
            MoveTaskToCandidates(task, group->CandidateTasks);
        }
    }
    UpdateTask(task);
}

void TOperationControllerBase::AddAllTaskPendingHints()
{
    for (const auto& task : Tasks) {
        AddTaskPendingHint(task);
    }
}

void TOperationControllerBase::DoAddTaskLocalityHint(TTaskPtr task, TNodeId nodeId)
{
    auto group = task->GetGroup();
    if (group->NodeIdToTasks[nodeId].insert(task).second) {
        LOG_TRACE("Task locality hint added (Task: %v, Address: %v)",
            task->GetId(),
            InputNodeDirectory->GetDescriptor(nodeId).GetDefaultAddress());
    }
}

void TOperationControllerBase::AddTaskLocalityHint(TTaskPtr task, TNodeId nodeId)
{
    DoAddTaskLocalityHint(task, nodeId);
    UpdateTask(task);
}

void TOperationControllerBase::AddTaskLocalityHint(TTaskPtr task, TChunkStripePtr stripe)
{
    for (const auto& dataSlice : stripe->DataSlices) {
        for (const auto& chunkSlice : dataSlice->ChunkSlices) {
            for (auto replica : chunkSlice->GetInputChunk()->GetReplicaList()) {
                auto locality = chunkSlice->GetLocality(replica.GetReplicaIndex());
                if (locality > 0) {
                    DoAddTaskLocalityHint(task, replica.GetNodeId());
                }
            }
        }
    }
    UpdateTask(task);
}

void TOperationControllerBase::ResetTaskLocalityDelays()
{
    LOG_DEBUG("Task locality delays are reset");
    for (auto group : TaskGroups) {
        for (const auto& pair : group->DelayedTasks) {
            auto task = pair.second;
            if (task->GetPendingJobCount() > 0) {
                MoveTaskToCandidates(task, group->CandidateTasks);
            } else {
                LOG_DEBUG("Task pending hint removed (Task: %v)",
                    task->GetId());
                YCHECK(group->NonLocalTasks.erase(task) == 1);
            }
        }
        group->DelayedTasks.clear();
    }
}

bool TOperationControllerBase::CheckJobLimits(
    TTaskPtr task,
    const TJobResources& jobLimits,
    const TJobResources& nodeResourceLimits)
{
    auto neededResources = task->GetMinNeededResources();
    if (Dominates(jobLimits, neededResources)) {
        return true;
    }
    task->CheckResourceDemandSanity(nodeResourceLimits, neededResources);
    return false;
}

void TOperationControllerBase::DoScheduleJob(
    ISchedulingContext* context,
    const TJobResources& jobLimits,
    TScheduleJobResult* scheduleJobResult)
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    if (!IsRunning()) {
        LOG_TRACE("Operation is not running, scheduling request ignored");
        scheduleJobResult->RecordFail(EScheduleJobFailReason::OperationNotRunning);
    } else if (GetPendingJobCount() == 0) {
        LOG_TRACE("No pending jobs left, scheduling request ignored");
        scheduleJobResult->RecordFail(EScheduleJobFailReason::NoPendingJobs);
    } else {
        DoScheduleLocalJob(context, jobLimits, scheduleJobResult);
        if (!scheduleJobResult->JobStartRequest) {
            DoScheduleNonLocalJob(context, jobLimits, scheduleJobResult);
        }
    }
}

void TOperationControllerBase::DoScheduleLocalJob(
    ISchedulingContext* context,
    const TJobResources& jobLimits,
    TScheduleJobResult* scheduleJobResult)
{
    const auto& nodeResourceLimits = context->ResourceLimits();
    const auto& address = context->GetNodeDescriptor().Address;
    auto nodeId = context->GetNodeDescriptor().Id;

    for (const auto& group : TaskGroups) {
        if (scheduleJobResult->IsScheduleStopNeeded()) {
            return;
        }
        if (!Dominates(jobLimits, group->MinNeededResources)) {
            scheduleJobResult->RecordFail(EScheduleJobFailReason::NotEnoughResources);
            continue;
        }

        auto localTasksIt = group->NodeIdToTasks.find(nodeId);
        if (localTasksIt == group->NodeIdToTasks.end()) {
            continue;
        }

        i64 bestLocality = 0;
        TTaskPtr bestTask;

        auto& localTasks = localTasksIt->second;
        auto it = localTasks.begin();
        while (it != localTasks.end()) {
            auto jt = it++;
            auto task = *jt;

            // Make sure that the task has positive locality.
            // Remove pending hint if not.
            auto locality = task->GetLocality(nodeId);
            if (locality <= 0) {
                localTasks.erase(jt);
                LOG_TRACE("Task locality hint removed (Task: %v, Address: %v)",
                    task->GetId(),
                    address);
                continue;
            }

            if (locality <= bestLocality) {
                continue;
            }

            if (task->GetPendingJobCount() == 0) {
                UpdateTask(task);
                continue;
            }

            if (!CheckJobLimits(task, jobLimits, nodeResourceLimits)) {
                scheduleJobResult->RecordFail(EScheduleJobFailReason::NotEnoughResources);
                continue;
            }

            bestLocality = locality;
            bestTask = task;
        }

        if (!IsRunning()) {
            scheduleJobResult->RecordFail(EScheduleJobFailReason::OperationNotRunning);
            break;
        }

        if (bestTask) {
            LOG_DEBUG(
                "Attempting to schedule a local job (Task: %v, Address: %v, Locality: %v, JobLimits: %v, "
                "PendingDataSize: %v, PendingJobCount: %v)",
                bestTask->GetId(),
                address,
                bestLocality,
                FormatResources(jobLimits),
                bestTask->GetPendingDataSize(),
                bestTask->GetPendingJobCount());

            if (!HasEnoughChunkLists(bestTask->IsIntermediateOutput(), bestTask->IsStderrTableEnabled(), bestTask->IsCoreTableEnabled())) {
                LOG_DEBUG("Job chunk list demand is not met");
                scheduleJobResult->RecordFail(EScheduleJobFailReason::NotEnoughChunkLists);
                break;
            }

            bestTask->ScheduleJob(context, jobLimits, scheduleJobResult);
            if (scheduleJobResult->JobStartRequest) {
                UpdateTask(bestTask);
                break;
            }
            if (scheduleJobResult->IsScheduleStopNeeded()) {
                return;
            }
        } else {
            // NB: This is one of the possible reasons, hopefully the most probable.
            scheduleJobResult->RecordFail(EScheduleJobFailReason::NoLocalJobs);
        }
    }
}

void TOperationControllerBase::DoScheduleNonLocalJob(
    ISchedulingContext* context,
    const TJobResources& jobLimits,
    TScheduleJobResult* scheduleJobResult)
{
    auto now = NProfiling::CpuInstantToInstant(context->GetNow());
    const auto& nodeResourceLimits = context->ResourceLimits();
    const auto& address = context->GetNodeDescriptor().Address;

    for (const auto& group : TaskGroups) {
        if (scheduleJobResult->IsScheduleStopNeeded()) {
            return;
        }
        if (!Dominates(jobLimits, group->MinNeededResources)) {
            scheduleJobResult->RecordFail(EScheduleJobFailReason::NotEnoughResources);
            continue;
        }

        auto& nonLocalTasks = group->NonLocalTasks;
        auto& candidateTasks = group->CandidateTasks;
        auto& delayedTasks = group->DelayedTasks;

        // Move tasks from delayed to candidates.
        while (!delayedTasks.empty()) {
            auto it = delayedTasks.begin();
            auto deadline = it->first;
            if (now < deadline) {
                break;
            }
            auto task = it->second;
            delayedTasks.erase(it);
            if (task->GetPendingJobCount() == 0) {
                LOG_DEBUG("Task pending hint removed (Task: %v)",
                    task->GetId());
                YCHECK(nonLocalTasks.erase(task) == 1);
                UpdateTask(task);
            } else {
                LOG_DEBUG("Task delay deadline reached (Task: %v)", task->GetId());
                MoveTaskToCandidates(task, candidateTasks);
            }
        }

        // Consider candidates in the order of increasing memory demand.
        {
            int processedTaskCount = 0;
            int noPendingJobsTaskCount = 0;
            auto it = candidateTasks.begin();
            while (it != candidateTasks.end()) {
                ++processedTaskCount;
                auto task = it->second;

                // Make sure that the task is ready to launch jobs.
                // Remove pending hint if not.
                if (task->GetPendingJobCount() == 0) {
                    LOG_DEBUG("Task pending hint removed (Task: %v)", task->GetId());
                    candidateTasks.erase(it++);
                    YCHECK(nonLocalTasks.erase(task) == 1);
                    UpdateTask(task);
                    ++noPendingJobsTaskCount;
                    continue;
                }

                // Check min memory demand for early exit.
                if (task->GetMinNeededResources().GetMemory() > jobLimits.GetMemory()) {
                    scheduleJobResult->RecordFail(EScheduleJobFailReason::NotEnoughResources);
                    break;
                }

                if (!CheckJobLimits(task, jobLimits, nodeResourceLimits)) {
                    ++it;
                    scheduleJobResult->RecordFail(EScheduleJobFailReason::NotEnoughResources);
                    continue;
                }

                if (!task->GetDelayedTime()) {
                    task->SetDelayedTime(now);
                }

                auto deadline = *task->GetDelayedTime() + task->GetLocalityTimeout();
                if (deadline > now) {
                    LOG_DEBUG("Task delayed (Task: %v, Deadline: %v)",
                        task->GetId(),
                        deadline);
                    delayedTasks.insert(std::make_pair(deadline, task));
                    candidateTasks.erase(it++);
                    scheduleJobResult->RecordFail(EScheduleJobFailReason::TaskDelayed);
                    continue;
                }

                if (!IsRunning()) {
                    scheduleJobResult->RecordFail(EScheduleJobFailReason::OperationNotRunning);
                    break;
                }

                LOG_DEBUG(
                    "Attempting to schedule a non-local job (Task: %v, Address: %v, JobLimits: %v, "
                    "PendingDataSize: %v, PendingJobCount: %v)",
                    task->GetId(),
                    address,
                    FormatResources(jobLimits),
                    task->GetPendingDataSize(),
                    task->GetPendingJobCount());

                if (!HasEnoughChunkLists(task->IsIntermediateOutput(), task->IsStderrTableEnabled(), task->IsCoreTableEnabled())) {
                    LOG_DEBUG("Job chunk list demand is not met");
                    scheduleJobResult->RecordFail(EScheduleJobFailReason::NotEnoughChunkLists);
                    break;
                }

                task->ScheduleJob(context, jobLimits, scheduleJobResult);
                if (scheduleJobResult->JobStartRequest) {
                    UpdateTask(task);
                    return;
                }
                if (scheduleJobResult->IsScheduleStopNeeded()) {
                    return;
                }

                // If task failed to schedule job, its min resources might have been updated.
                auto minMemory = task->GetMinNeededResources().GetMemory();
                if (it->first == minMemory) {
                    ++it;
                } else {
                    it = candidateTasks.erase(it);
                    candidateTasks.insert(std::make_pair(minMemory, task));
                }
            }

            if (processedTaskCount == noPendingJobsTaskCount) {
                scheduleJobResult->RecordFail(EScheduleJobFailReason::NoCandidateTasks);
            }

            LOG_DEBUG("Non-local tasks processed (TotalCount: %v, NoPendingJobsCount: %v)",
                processedTaskCount,
                noPendingJobsTaskCount);
        }
    }
}

TCancelableContextPtr TOperationControllerBase::GetCancelableContext() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return CancelableContext;
}

IInvokerPtr TOperationControllerBase::GetCancelableInvoker() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return CancelableInvoker;
}

IInvokerPtr TOperationControllerBase::GetInvoker() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return SuspendableInvoker;
}

TFuture<void> TOperationControllerBase::Suspend()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    return SuspendableInvoker->Suspend();
}

void TOperationControllerBase::Resume()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    SuspendableInvoker->Resume();
}

int TOperationControllerBase::GetPendingJobCount() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    // Avoid accessing the state while not prepared.
    if (!IsPrepared()) {
        return 0;
    }

    // NB: For suspended operations we still report proper pending job count
    // but zero demand.
    if (!IsRunning()) {
        return 0;
    }

    return CachedPendingJobCount;
}

int TOperationControllerBase::GetTotalJobCount() const
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    // Avoid accessing the state while not prepared.
    if (!IsPrepared()) {
        return 0;
    }

    return JobCounter.GetTotal();
}

bool TOperationControllerBase::IsForgotten() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return Forgotten;
}

bool TOperationControllerBase::IsRevivedFromSnapshot() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return RevivedFromSnapshot;
}

void TOperationControllerBase::IncreaseNeededResources(const TJobResources& resourcesDelta)
{
    VERIFY_THREAD_AFFINITY_ANY();

    TWriterGuard guard(CachedNeededResourcesLock);
    CachedNeededResources += resourcesDelta;
}

TJobResources TOperationControllerBase::GetNeededResources() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(CachedNeededResourcesLock);
    return CachedNeededResources;
}

std::vector<TJobResources> TOperationControllerBase::GetMinNeededJobResources() const
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    yhash_map<EJobType, TJobResources> minNeededJobResources;

    for (const auto& task: Tasks) {
        if (task->GetPendingJobCount() == 0) {
            continue;
        }

        auto jobType = task->GetJobType();
        auto resources = task->GetMinNeededResources();

        auto resIt = minNeededJobResources.find(jobType);
        if (resIt == minNeededJobResources.end()) {
            minNeededJobResources[jobType] = resources;
        } else {
            resIt->second = Min(resIt->second, resources);
        }
    }

    std::vector<TJobResources> result;
    for (const auto& pair : minNeededJobResources) {
        result.push_back(pair.second);
        LOG_DEBUG("Aggregated minimal needed resources for jobs (JobType: %lv, MinNeededResources: %v)",
            pair.first,
            FormatResources(pair.second));
    }
    return result;
}

i64 TOperationControllerBase::ComputeUserJobMemoryReserve(EJobType jobType, TUserJobSpecPtr userJobSpec) const
{
    if (userJobSpec) {
        return userJobSpec->MemoryLimit * GetUserJobMemoryDigest(jobType)->GetQuantile(Config->UserJobMemoryReserveQuantile);
    } else {
        return 0;
    }
}

void TOperationControllerBase::OnOperationCompleted(bool interrupted)
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);
    Y_UNUSED(interrupted);

    // This can happen if operation failed during completion in derived class (e.x. SortController).
    if (State.exchange(EControllerState::Finished) == EControllerState::Finished) {
        return;
    }

    BuildAndSaveProgress();

    auto flushResult = WaitFor(MasterConnector->FlushOperationNode(OperationId));
    // We do not want to complete operation if progress flush has failed.
    if (!flushResult.IsOK()) {
        OnOperationFailed(flushResult, /* flush */ false);
    }

    Host->OnOperationCompleted(OperationId);
}

void TOperationControllerBase::OnOperationFailed(const TError& error, bool flush)
{
    VERIFY_THREAD_AFFINITY_ANY();

    // During operation failing job aborting can lead to another operation fail, we don't want to invoke it twice.
    if (State.exchange(EControllerState::Finished) == EControllerState::Finished) {
        return;
    }

    BuildAndSaveProgress();

    if (flush) {
        // NB: Error ignored since we cannot do anything with it.
        WaitFor(MasterConnector->FlushOperationNode(OperationId));
    }

    Host->OnOperationFailed(OperationId, error);
}

TNullable<TDuration> TOperationControllerBase::GetTimeLimit() const
{
    auto timeLimit = Config->OperationTimeLimit;
    if (Spec->TimeLimit) {
        timeLimit = Spec->TimeLimit;
    }
    return timeLimit;
}

TError TOperationControllerBase::GetTimeLimitError() const
{
    return TError("Operation is running for too long, aborted")
        << TErrorAttribute("time_limit", GetTimeLimit());
}


void TOperationControllerBase::OnOperationTimeLimitExceeded()
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    EControllerState expected = EControllerState::Running;
    if (!State.compare_exchange_strong(expected, EControllerState::Failing)) {
        return;
    }

    for (const auto& joblet : JobletMap) {
        auto jobHost = Host->GetJobHost(joblet.first);

        jobHost->FailJob();
    }

    auto error = GetTimeLimitError();
    if (!JobletMap.empty()) {
        TDelayedExecutor::MakeDelayed(Config->OperationControllerFailTimeout)
            .Apply(BIND(&TOperationControllerBase::OnOperationFailed, MakeWeak(this), error, /* flush */ true)
            .Via(CancelableInvoker));
    } else {
        OnOperationFailed(error, /* flush */ true);
    }
}

void TOperationControllerBase::CheckFailedJobsStatusReceived()
{
    if (IsFailing() && JobletMap.empty()) {
        auto error = GetTimeLimitError();
        OnOperationFailed(error, /* flush */ true);
    }
}

void TOperationControllerBase::OnOperationCrashed(const TAssertionFailedException& ex)
{
    OnOperationFailed(TError("Operation controller crashed; please file a ticket at YTADMINREQ and attach a link to this operation")
        << TErrorAttribute("failed_condition", ex.GetExpression())
        << TErrorAttribute("stack_trace", ex.GetStackTrace())
        << TErrorAttribute("core_path", ex.GetCorePath())
        << TErrorAttribute("operation_id", OperationId));
}

EJobState TOperationControllerBase::GetStatisticsJobState(const TJobletPtr& joblet, const EJobState& state)
{
    // NB: Completed restarted job is considered as lost in statistics.
    // Actually we have lost previous incarnation of this job, but it was already considered as completed in statistics.
    return (joblet->Restarted && state == EJobState::Completed)
        ? EJobState::Lost
        : state;
}

void TOperationControllerBase::ProcessFinishedJobResult(std::unique_ptr<TJobSummary> summary, bool requestJobNodeCreation)
{
    auto jobId = summary->Id;

    const auto& schedulerResultExt = summary->Result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);

    auto stderrChunkId = FromProto<TChunkId>(schedulerResultExt.stderr_chunk_id());
    auto failContextChunkId = FromProto<TChunkId>(schedulerResultExt.fail_context_chunk_id());

    auto joblet = GetJoblet(jobId);
    // Job is not actually started.
    if (!joblet->StartTime) {
        return;
    }

    bool shouldCreateJobNode =
        (requestJobNodeCreation && JobNodeCount_ < Config->MaxJobNodesPerOperation) ||
        (stderrChunkId && StderrCount_ < Spec->MaxStderrCount);

    if (!shouldCreateJobNode) {
        return;
    }

    auto inputPaths = BuildInputPathYson(joblet);
    auto finishedJob = New<TFinishedJobInfo>(joblet, std::move(*summary), std::move(inputPaths));
    FinishedJobs_.insert(std::make_pair(jobId, finishedJob));

    auto attributes = BuildYsonStringFluently<EYsonType::MapFragment>()
        .Do([&] (IYsonConsumer* consumer) {
            BuildFinishedJobAttributes(finishedJob, /* outputStatistics */ true, consumer);
        })
        .Finish();

    {
        TCreateJobNodeRequest request;
        request.OperationId = OperationId;
        request.JobId = jobId;
        request.Attributes = attributes;
        request.StderrChunkId = stderrChunkId;
        request.FailContextChunkId = failContextChunkId;

        MasterConnector->CreateJobNode(std::move(request));
    }

    if (stderrChunkId) {
        ++StderrCount_;
    }
    ++JobNodeCount_;
}

bool TOperationControllerBase::IsPrepared() const
{
    return State != EControllerState::Preparing;
}

bool TOperationControllerBase::IsRunning() const
{
    return State == EControllerState::Running;
}

bool TOperationControllerBase::IsFailing() const
{
    return State == EControllerState::Failing;
}

bool TOperationControllerBase::IsFinished() const
{
    return State == EControllerState::Finished;
}

void TOperationControllerBase::CreateLivePreviewTables()
{
    const auto& client = Host->GetMasterClient();
    auto connection = client->GetNativeConnection();

    // NB: use root credentials.
    auto channel = client->GetMasterChannelOrThrow(EMasterChannelKind::Leader);
    TObjectServiceProxy proxy(channel);

    auto batchReq = proxy.ExecuteBatch();

    auto addRequest = [&] (
        const Stroka& path,
        TCellTag cellTag,
        int replicationFactor,
        NCompression::ECodec compressionCodec,
        const Stroka& key,
        const TYsonString& acl,
        TNullable<TTableSchema> schema)
    {
        auto req = TCypressYPathProxy::Create(path);
        req->set_type(static_cast<int>(EObjectType::Table));
        req->set_ignore_existing(true);
        req->set_enable_accounting(false);

        auto attributes = CreateEphemeralAttributes();
        attributes->Set("replication_factor", replicationFactor);
        // Does this affect anything or is this for viewing only? Should we set the 'media' ('primary_medium') property?
        attributes->Set("compression_codec", compressionCodec);
        if (cellTag == connection->GetPrimaryMasterCellTag()) {
            attributes->Set("external", false);
        } else {
            attributes->Set("external_cell_tag", cellTag);
        }
        attributes->Set("acl", acl);
        attributes->Set("inherit_acl", false);
        if (schema) {
            attributes->Set("schema", *schema);
        }
        ToProto(req->mutable_node_attributes(), *attributes);
        GenerateMutationId(req);
        SetTransactionId(req, AsyncSchedulerTransaction->GetId());

        batchReq->AddRequest(req, key);
    };

    if (IsOutputLivePreviewSupported()) {
        LOG_INFO("Creating live preview for output tables");

        for (int index = 0; index < OutputTables.size(); ++index) {
            const auto& table = OutputTables[index];
            auto path = GetLivePreviewOutputPath(OperationId, index);
            addRequest(
                path,
                table.CellTag,
                table.Options->ReplicationFactor,
                table.Options->CompressionCodec,
                "create_output",
                table.EffectiveAcl,
                table.TableUploadOptions.TableSchema);
        }
    }

    if (StderrTable) {
        LOG_INFO("Creating live preview for stderr table");
        auto path = GetLivePreviewStderrTablePath(OperationId);
        addRequest(
            path,
            StderrTable->CellTag,
            StderrTable->Options->ReplicationFactor,
            StderrTable->Options->CompressionCodec,
            "create_stderr",
            StderrTable->EffectiveAcl,
            StderrTable->TableUploadOptions.TableSchema);
    }

    if (IsIntermediateLivePreviewSupported()) {
        LOG_INFO("Creating live preview for intermediate table");

        auto path = GetLivePreviewIntermediatePath(OperationId);
        addRequest(
            path,
            IntermediateOutputCellTag,
            1,
            Spec->IntermediateCompressionCodec,
            "create_intermediate",
            BuildYsonStringFluently()
                .BeginList()
                    .Item().BeginMap()
                        .Item("action").Value("allow")
                        .Item("subjects").BeginList()
                            .Item().Value(AuthenticatedUser)
                            .DoFor(Owners, [] (TFluentList fluent, const Stroka& owner) {
                                fluent.Item().Value(owner);
                            })
                        .EndList()
                        .Item("permissions").BeginList()
                            .Item().Value("read")
                        .EndList()
                    .EndMap()
                    .DoFor(Spec->IntermediateDataAcl->GetChildren(), [] (TFluentList fluent, const INodePtr& node) {
                        fluent.Item().Value(node);
                    })
                    .DoFor(Config->AdditionalIntermediateDataAcl->GetChildren(), [] (TFluentList fluent, const INodePtr& node) {
                        fluent.Item().Value(node);
                    })
                .EndList(),
            Null);
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error creating live preview tables");
    const auto& batchRsp = batchRspOrError.Value();

    auto handleResponse = [&] (TLivePreviewTableBase& table, TCypressYPathProxy::TRspCreatePtr rsp) {
        table.LivePreviewTableId = FromProto<NCypressClient::TNodeId>(rsp->node_id());
    };

    if (IsOutputLivePreviewSupported()) {
        auto rspsOrError = batchRsp->GetResponses<TCypressYPathProxy::TRspCreate>("create_output");
        YCHECK(rspsOrError.size() == OutputTables.size());
        for (int index = 0; index < OutputTables.size(); ++index) {
            handleResponse(OutputTables[index], rspsOrError[index].Value());
        }

        LOG_INFO("Live preview for output tables created");
    }

    if (StderrTable) {
        auto rspOrError = batchRsp->GetResponse<TCypressYPathProxy::TRspCreate>("create_stderr");
        handleResponse(*StderrTable, rspOrError.Value());

        LOG_INFO("Live preview for stderr table created");
    }

    if (IsIntermediateLivePreviewSupported()) {
        auto rspOrError = batchRsp->GetResponse<TCypressYPathProxy::TRspCreate>("create_intermediate");
        handleResponse(IntermediateTable, rspOrError.Value());

        LOG_INFO("Live preview for intermediate table created");
    }
}

void TOperationControllerBase::FetchInputTables()
{
    i64 totalChunkCount = 0;
    i64 totalExtensionSize = 0;

    LOG_INFO("Started fetching input tables");

    for (int tableIndex = 0; tableIndex < static_cast<int>(InputTables.size()); ++tableIndex) {
        auto& table = InputTables[tableIndex];
        auto ranges = table.Path.GetRanges();
        int originalRangeCount = ranges.size();
        if (ranges.empty()) {
            continue;
        }

        if (InputQuery && InputQuery->Query->OriginalSchema.IsSorted()) {
            std::vector<TReadRange> inferredRanges;
            for (const auto& range : table.Path.GetRanges()) {
                auto lower = range.LowerLimit().HasKey() ? range.LowerLimit().GetKey() : MinKey();
                auto upper = range.UpperLimit().HasKey() ? range.UpperLimit().GetKey() : MaxKey();
                auto result = InputQuery->RangeInferrer(TRowRange(lower.Get(), upper.Get()), RowBuffer);
                for (const auto& inferred : result) {
                    auto inferredRange = range;
                    inferredRange.LowerLimit().SetKey(TOwningKey(inferred.first));
                    inferredRange.UpperLimit().SetKey(TOwningKey(inferred.second));
                    inferredRanges.push_back(inferredRange);
                }
            }
            ranges = std::move(inferredRanges);
        }

        if (ranges.size() > Config->MaxRangesOnTable) {
            THROW_ERROR_EXCEPTION(
                "Too many ranges on table: maximum allowed %v, actual %v",
                Config->MaxRangesOnTable,
                ranges.size())
                << TErrorAttribute("table_path", table.Path.GetPath());
        }

        LOG_INFO("Fetching input table (Path: %v, RangeCount: %v, InferredRangeCount: %v)",
            table.Path,
            originalRangeCount,
            ranges.size());

        std::vector<NChunkClient::NProto::TChunkSpec> chunkSpecs;
        FetchChunkSpecs(
            AuthenticatedInputMasterClient,
            InputNodeDirectory,
            table.CellTag,
            table.Path,
            table.ObjectId,
            ranges,
            table.ChunkCount,
            Config->MaxChunksPerFetch,
            Config->MaxChunksPerLocateRequest,
            [&] (TChunkOwnerYPathProxy::TReqFetchPtr req) {
                req->set_fetch_all_meta_extensions(false);
                req->add_extension_tags(TProtoExtensionTag<NChunkClient::NProto::TMiscExt>::Value);
                if (table.IsDynamic || IsBoundaryKeysFetchEnabled()) {
                    req->add_extension_tags(TProtoExtensionTag<TBoundaryKeysExt>::Value);
                }
                req->set_fetch_parity_replicas(IsParityReplicasFetchEnabled());
                SetTransactionId(req, InputTransaction->GetId());
            },
            Logger,
            &chunkSpecs);

        for (const auto& chunkSpec : chunkSpecs) {
            auto inputChunk = New<TInputChunk>(chunkSpec);
            inputChunk->SetTableIndex(tableIndex);
            inputChunk->SetChunkIndex(totalChunkCount++);
            table.Chunks.emplace_back(std::move(inputChunk));
            for (const auto& extension : chunkSpec.chunk_meta().extensions().extensions()) {
                totalExtensionSize += extension.data().size();
            }
            RegisterInputChunk(table.Chunks.back());
        }

        LOG_INFO("Input table fetched (Path: %v, ChunkCount: %v)",
            table.Path,
            table.Chunks.size());
    }

    LOG_INFO("Finished fetching input tables (TotalChunkCount: %v, TotalExtensionSize: %v)",
        totalChunkCount,
        totalExtensionSize);
}

void TOperationControllerBase::RegisterInputChunk(const TInputChunkPtr& inputChunk)
{
    const auto& chunkId = inputChunk->ChunkId();

    // Insert an empty TInputChunkDescriptor if a new chunkId is encountered.
    auto& chunkDescriptor = InputChunkMap[chunkId];
    chunkDescriptor.InputChunks.push_back(inputChunk);

    if (IsUnavailable(inputChunk, IsParityReplicasFetchEnabled())) {
        chunkDescriptor.State = EInputChunkState::Waiting;
    }
}

void TOperationControllerBase::LockInputTables()
{
    //! TODO(ignat): Merge in with lock input files method.
    LOG_INFO("Locking input tables");

    auto channel = AuthenticatedInputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Leader);
    TObjectServiceProxy proxy(channel);

    auto batchReq = proxy.ExecuteBatch();

    for (const auto& table : InputTables) {
        auto req = TTableYPathProxy::Lock(table.Path.GetPath());
        req->set_mode(static_cast<int>(ELockMode::Snapshot));
        SetTransactionId(req, InputTransaction->GetId());
        GenerateMutationId(req);
        batchReq->AddRequest(req);
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error locking input tables");

    const auto& batchRsp = batchRspOrError.Value()->GetResponses<TCypressYPathProxy::TRspLock>();
    for (int index = 0; index < InputTables.size(); ++index) {
        auto& table = InputTables[index];
        const auto& path = table.Path.GetPath();
        const auto& rspOrError = batchRsp[index];
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Failed to lock input table %Qv", path);
        const auto& rsp = rspOrError.Value();
        table.ObjectId = FromProto<TObjectId>(rsp->node_id());
    }
}

void TOperationControllerBase::GetInputTablesAttributes()
{
    LOG_INFO("Getting input tables attributes");

    GetUserObjectBasicAttributes<TInputTable>(
        AuthenticatedInputMasterClient,
        InputTables,
        InputTransaction->GetId(),
        Logger,
        EPermission::Read);

    for (const auto& table : InputTables) {
        if (table.Type != EObjectType::Table) {
            THROW_ERROR_EXCEPTION("Object %v has invalid type: expected %Qlv, actual %Qlv",
                table.Path.GetPath(),
                EObjectType::Table,
                table.Type);
        }
    }

    {
        auto channel = AuthenticatedInputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Follower);
        TObjectServiceProxy proxy(channel);

        auto batchReq = proxy.ExecuteBatch();

        for (const auto& table : InputTables) {
            auto objectIdPath = FromObjectId(table.ObjectId);
            {
                auto req = TTableYPathProxy::Get(objectIdPath + "/@");
                std::vector<Stroka> attributeKeys{
                    "dynamic",
                    "chunk_count",
                    "retained_timestamp",
                    "schema_mode",
                    "schema",
                    "unflushed_timestamp"
                };
                ToProto(req->mutable_attributes()->mutable_keys(), attributeKeys);
                SetTransactionId(req, InputTransaction->GetId());
                batchReq->AddRequest(req, "get_attributes");
            }
        }

        auto batchRspOrError = WaitFor(batchReq->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error getting attributes of input tables");
        const auto& batchRsp = batchRspOrError.Value();

        auto lockInRspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspLock>("lock");
        auto getInAttributesRspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspGet>("get_attributes");
        for (int index = 0; index < InputTables.size(); ++index) {
            auto& table = InputTables[index];
            auto path = table.Path.GetPath();
            {
                const auto& rsp = getInAttributesRspsOrError[index].Value();
                auto attributes = ConvertToAttributes(TYsonString(rsp->value()));

                table.IsDynamic = attributes->Get<bool>("dynamic");
                table.Schema = attributes->Get<TTableSchema>("schema");
                table.SchemaMode = attributes->Get<ETableSchemaMode>("schema_mode");
                table.ChunkCount = attributes->Get<int>("chunk_count");

                // Validate that timestamp is correct.
                ValidateDynamicTableTimestamp(table.Path, table.IsDynamic, table.Schema, *attributes);
            }
            LOG_INFO("Input table locked (Path: %v, Schema: %v, ChunkCount: %v)",
                path,
                table.Schema,
                table.ChunkCount);
        }
    }
}

void TOperationControllerBase::GetOutputTablesSchema()
{
    LOG_INFO("Getting output tables schema");

    {
        auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Follower);
        TObjectServiceProxy proxy(channel);
        auto batchReq = proxy.ExecuteBatch();

        for (const auto* table : UpdatingTables) {
            auto objectIdPath = FromObjectId(table->ObjectId);
            {
                auto req = TTableYPathProxy::Get(objectIdPath + "/@");
                std::vector<Stroka> attributeKeys{
                    "schema_mode",
                    "schema",
                    "optimize_for",
                    "compression_codec",
                    "erasure_codec"
                };
                ToProto(req->mutable_attributes()->mutable_keys(), attributeKeys);
                SetTransactionId(req, GetTransactionIdForOutputTable(*table));
                batchReq->AddRequest(req, "get_attributes");
            }
        }

        auto batchRspOrError = WaitFor(batchReq->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error getting attributes of output tables");
        const auto& batchRsp = batchRspOrError.Value();

        auto getOutAttributesRspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspGet>("get_attributes");
        for (int index = 0; index < UpdatingTables.size(); ++index) {
            auto* table = UpdatingTables[index];
            const auto& path = table->Path;

            const auto& rsp = getOutAttributesRspsOrError[index].Value();
            auto attributes = ConvertToAttributes(TYsonString(rsp->value()));

            table->TableUploadOptions = GetTableUploadOptions(
                path,
                *attributes,
                0); // Here we assume zero row count, we will do additional check later.

            //TODO(savrus) I would like to see commit ts here. But as for now, start ts suffices.
            table->Timestamp = OutputTransaction->GetStartTimestamp();

            LOG_DEBUG("Received output table schema (Path: %v, Schema: %v, SchemaMode: %v, LockMode: %v)",
                path,
                table->TableUploadOptions.TableSchema,
                table->TableUploadOptions.SchemaMode,
                table->TableUploadOptions.LockMode);
        }

        if (StderrTable) {
            StderrTable->TableUploadOptions.TableSchema = GetStderrBlobTableSchema().ToTableSchema();
            StderrTable->TableUploadOptions.SchemaMode = ETableSchemaMode::Strong;
            if (StderrTable->TableUploadOptions.UpdateMode == EUpdateMode::Append) {
                THROW_ERROR_EXCEPTION("Cannot write stderr table in append mode.");
            }
        }

        if (CoreTable) {
            CoreTable->TableUploadOptions.TableSchema = GetCoreBlobTableSchema().ToTableSchema();
            CoreTable->TableUploadOptions.SchemaMode = ETableSchemaMode::Strong;
            if (CoreTable->TableUploadOptions.UpdateMode == EUpdateMode::Append) {
                THROW_ERROR_EXCEPTION("Cannot write core table in append mode.");
            }
        }
    }
}

void TOperationControllerBase::PrepareInputTables()
{
    if (!AreForeignTablesSupported()) {
        for (const auto& table : InputTables) {
            if (table.IsForeign()) {
                THROW_ERROR_EXCEPTION("Foreign tables are not supported in %Qlv operation", OperationType)
                    << TErrorAttribute("foreign_table", table.GetPath());
            }
        }
    }
}

void TOperationControllerBase::PrepareOutputTables()
{ }

void TOperationControllerBase::LockOutputTablesAndGetAttributes()
{
    LOG_INFO("Locking output tables");

    {
        auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Leader);
        TObjectServiceProxy proxy(channel);

        {
            auto batchReq = proxy.ExecuteBatch();
            for (const auto* table : UpdatingTables) {
                auto objectIdPath = FromObjectId(table->ObjectId);
                auto req = TCypressYPathProxy::Lock(objectIdPath);
                SetTransactionId(req, GetTransactionIdForOutputTable(*table));
                GenerateMutationId(req);
                req->set_mode(static_cast<int>(table->TableUploadOptions.LockMode));
                batchReq->AddRequest(req, "lock");
            }
            auto batchRspOrError = WaitFor(batchReq->Invoke());
            THROW_ERROR_EXCEPTION_IF_FAILED(
                GetCumulativeError(batchRspOrError),
                "Error locking output tables");
        }
    }

    LOG_INFO("Getting output tables attributes");

    {
        auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Follower);
        TObjectServiceProxy proxy(channel);
        auto batchReq = proxy.ExecuteBatch();

        for (const auto* table : UpdatingTables) {
            auto objectIdPath = FromObjectId(table->ObjectId);
            {
                auto req = TTableYPathProxy::Get(objectIdPath + "/@");

                std::vector<Stroka> attributeKeys{
                    "account",
                    "chunk_writer",
                    "effective_acl",
                    "primary_medium",
                    "replication_factor",
                    "row_count",
                    "vital"
                };
                ToProto(req->mutable_attributes()->mutable_keys(), attributeKeys);
                SetTransactionId(req, GetTransactionIdForOutputTable(*table));
                batchReq->AddRequest(req, "get_attributes");
            }
        }

        auto batchRspOrError = WaitFor(batchReq->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(
            GetCumulativeError(batchRspOrError),
            "Error getting attributes of output tables");
        const auto& batchRsp = batchRspOrError.Value();

        auto getOutAttributesRspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspGet>("get_attributes");
        for (int index = 0; index < UpdatingTables.size(); ++index) {
            auto* table = UpdatingTables[index];
            const auto& path = table->Path.GetPath();
            {
                const auto& rsp = getOutAttributesRspsOrError[index].Value();
                auto attributes = ConvertToAttributes(TYsonString(rsp->value()));

                if (attributes->Get<i64>("row_count") > 0 &&
                    table->TableUploadOptions.TableSchema.IsSorted() &&
                    table->TableUploadOptions.UpdateMode == EUpdateMode::Append)
                {
                    THROW_ERROR_EXCEPTION("Cannot append sorted data to non-empty output table %v",
                        path);
                }

                if (table->TableUploadOptions.TableSchema.IsSorted()) {
                    table->Options->ValidateSorted = true;
                    table->Options->ValidateUniqueKeys = table->TableUploadOptions.TableSchema.GetUniqueKeys();
                } else {
                    table->Options->ValidateSorted = false;
                }

                table->Options->CompressionCodec = table->TableUploadOptions.CompressionCodec;
                table->Options->ErasureCodec = table->TableUploadOptions.ErasureCodec;
                table->Options->ReplicationFactor = attributes->Get<int>("replication_factor");
                table->Options->MediumName = attributes->Get<Stroka>("primary_medium");
                table->Options->Account = attributes->Get<Stroka>("account");
                table->Options->ChunksVital = attributes->Get<bool>("vital");
                table->Options->OptimizeFor = table->TableUploadOptions.OptimizeFor;
                table->Options->EvaluateComputedColumns = table->TableUploadOptions.TableSchema.HasComputedColumns();

                // Workaround for YT-5827.
                if (table->TableUploadOptions.TableSchema.Columns().empty() &&
                    table->TableUploadOptions.TableSchema.GetStrict())
                {
                    table->Options->OptimizeFor = EOptimizeFor::Lookup;
                }

                table->EffectiveAcl = attributes->GetYson("effective_acl");
                table->WriterConfig = attributes->FindYson("chunk_writer");
            }
            LOG_INFO("Output table locked (Path: %v, Options: %v, UploadTransactionId: %v)",
                path,
                ConvertToYsonString(table->Options, EYsonFormat::Text).GetData(),
                table->UploadTransactionId);
        }
    }
}

void TOperationControllerBase::BeginUploadOutputTables(const std::vector<TOutputTable*>& updatingTables)
{
    LOG_INFO("Beginning upload for output tables");

    {
        auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Leader);
        TObjectServiceProxy proxy(channel);

        {
            auto batchReq = proxy.ExecuteBatch();
            for (const auto* table : updatingTables) {
                auto objectIdPath = FromObjectId(table->ObjectId);
                auto req = TTableYPathProxy::BeginUpload(objectIdPath);
                SetTransactionId(req, GetTransactionIdForOutputTable(*table));
                GenerateMutationId(req);
                req->set_update_mode(static_cast<int>(table->TableUploadOptions.UpdateMode));
                req->set_lock_mode(static_cast<int>(table->TableUploadOptions.LockMode));
                req->set_upload_transaction_title(Format("Upload to %v from operation %v",
                    table->Path.GetPath(),
                    OperationId));
                batchReq->AddRequest(req, "begin_upload");
            }
            auto batchRspOrError = WaitFor(batchReq->Invoke());
            THROW_ERROR_EXCEPTION_IF_FAILED(
                GetCumulativeError(batchRspOrError),
                "Error starting upload transactions for output tables");
            const auto& batchRsp = batchRspOrError.Value();

            auto beginUploadRspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspBeginUpload>("begin_upload");
            for (int index = 0; index < updatingTables.size(); ++index) {
                auto* table = updatingTables[index];
                const auto& rsp = beginUploadRspsOrError[index].Value();
                table->UploadTransactionId = FromProto<TTransactionId>(rsp->upload_transaction_id());
            }
        }
    }

    yhash<TCellTag, std::vector<TOutputTable*>> cellTagToTables;
    for (auto* table : updatingTables) {
        cellTagToTables[table->CellTag].push_back(table);
    }

    for (const auto& pair : cellTagToTables) {
        auto cellTag = pair.first;
        const auto& tables = pair.second;

        LOG_INFO("Getting output tables upload parameters (CellTag: %v)", cellTag);

        auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(
            EMasterChannelKind::Follower,
            cellTag);
        TObjectServiceProxy proxy(channel);

        auto batchReq = proxy.ExecuteBatch();
        for (const auto& table : tables) {
            auto objectIdPath = FromObjectId(table->ObjectId);
            {
                auto req = TTableYPathProxy::GetUploadParams(objectIdPath);
                SetTransactionId(req, table->UploadTransactionId);
                batchReq->AddRequest(req, "get_upload_params");
            }
        }

        auto batchRspOrError = WaitFor(batchReq->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(batchRspOrError, "Error getting upload parameters of output tables");
        const auto& batchRsp = batchRspOrError.Value();

        auto getUploadParamsRspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspGetUploadParams>("get_upload_params");
        for (int index = 0; index < tables.size(); ++index) {
            auto* table = tables[index];
            const auto& path = table->Path.GetPath();
            {
                const auto& rspOrError = getUploadParamsRspsOrError[index];
                THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error getting upload parameters of output table %v",
                    path);

                const auto& rsp = rspOrError.Value();
                table->OutputChunkListId = FromProto<TChunkListId>(rsp->chunk_list_id());

                LOG_INFO("Upload parameters of output table received (Path: %v, ChunkListId: %v)",
                    path,
                    table->OutputChunkListId);
            }
        }
    }
}

void TOperationControllerBase::FetchUserFiles()
{
    for (auto& file : Files) {
        auto objectIdPath = FromObjectId(file.ObjectId);
        const auto& path = file.Path.GetPath();

        LOG_INFO("Fetching user file (Path: %v)",
            path);

        switch (file.Type) {
            case EObjectType::Table:
                FetchChunkSpecs(
                    AuthenticatedInputMasterClient,
                    InputNodeDirectory,
                    file.CellTag,
                    file.Path,
                    file.ObjectId,
                    file.Path.GetRanges(),
                    file.ChunkCount,
                    Config->MaxChunksPerFetch,
                    Config->MaxChunksPerLocateRequest,
                    [&] (TChunkOwnerYPathProxy::TReqFetchPtr req) {
                        req->set_fetch_all_meta_extensions(false);
                        req->add_extension_tags(TProtoExtensionTag<NChunkClient::NProto::TMiscExt>::Value);
                        if (file.IsDynamic || IsBoundaryKeysFetchEnabled()) {
                            req->add_extension_tags(TProtoExtensionTag<TBoundaryKeysExt>::Value);
                        }
                        req->set_fetch_parity_replicas(IsParityReplicasFetchEnabled());
                        SetTransactionId(req, InputTransaction->GetId());
                    },
                    Logger,
                    &file.ChunkSpecs);
                break;

            case EObjectType::File: {
                auto channel = AuthenticatedInputMasterClient->GetMasterChannelOrThrow(
                    EMasterChannelKind::Follower,
                    file.CellTag);
                TObjectServiceProxy proxy(channel);

                auto batchReq = proxy.ExecuteBatch();

                auto req = TChunkOwnerYPathProxy::Fetch(objectIdPath);
                ToProto(req->mutable_ranges(), std::vector<TReadRange>({TReadRange()}));
                req->add_extension_tags(TProtoExtensionTag<NChunkClient::NProto::TMiscExt>::Value);
                SetTransactionId(req, InputTransaction->GetId());
                batchReq->AddRequest(req, "fetch");

                auto batchRspOrError = WaitFor(batchReq->Invoke());
                THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error fetching user file %v",
                     path);
                const auto& batchRsp = batchRspOrError.Value();

                auto rsp = batchRsp->GetResponse<TChunkOwnerYPathProxy::TRspFetch>("fetch").Value();
                ProcessFetchResponse(
                    AuthenticatedInputMasterClient,
                    rsp,
                    file.CellTag,
                    nullptr,
                    Config->MaxChunksPerLocateRequest,
                    Null,
                    Logger,
                    &file.ChunkSpecs);

                break;
            }

            default:
                Y_UNREACHABLE();
        }

        LOG_INFO("User file fetched (Path: %v, FileName: %v)",
            path,
            file.FileName);
    }
}

void TOperationControllerBase::ValidateDynamicTableTimestamp(
    const TRichYPath& path,
    bool dynamic,
    const TTableSchema& schema,
    const IAttributeDictionary& attributes) const
{
    auto nullableRequested = path.GetTimestamp();
    if (nullableRequested && !(dynamic && schema.IsSorted())) {
        THROW_ERROR_EXCEPTION("Invalid attribute %Qv: table %Qv is not sorted dynamic",
            "timestamp",
            path.GetPath());
    }

    auto requested = nullableRequested.Get(AsyncLastCommittedTimestamp);
    if (requested != AsyncLastCommittedTimestamp) {
        auto retained = attributes.Get<TTimestamp>("retained_timestamp");
        auto unflushed = attributes.Get<TTimestamp>("unflushed_timestamp");
        if (requested < retained || requested >= unflushed) {
            THROW_ERROR_EXCEPTION("Requested timestamp is out of range for table %v",
                path.GetPath())
                << TErrorAttribute("requested_timestamp", requested)
                << TErrorAttribute("retained_timestamp", retained)
                << TErrorAttribute("unflushed_timestamp", unflushed);
        }
    }


}

void TOperationControllerBase::LockUserFiles()
{
    LOG_INFO("Locking user files");

    auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Leader);
    TObjectServiceProxy proxy(channel);
    auto batchReq = proxy.ExecuteBatch();

    for (const auto& file : Files) {
        auto req = TCypressYPathProxy::Lock(file.Path.GetPath());
        req->set_mode(static_cast<int>(ELockMode::Snapshot));
        GenerateMutationId(req);
        SetTransactionId(req, InputTransaction->GetId());
        batchReq->AddRequest(req);
    }

    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error locking user files");

    const auto& batchRsp = batchRspOrError.Value()->GetResponses<TCypressYPathProxy::TRspLock>();
    for (int index = 0; index < Files.size(); ++index) {
        auto& file = Files[index];
        const auto& path = file.Path.GetPath();
        const auto& rspOrError = batchRsp[index];
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Failed to lock user file %Qv", path);
        const auto& rsp = rspOrError.Value();
        file.ObjectId = FromProto<TObjectId>(rsp->node_id());
    }
}

void TOperationControllerBase::GetUserFilesAttributes()
{
    LOG_INFO("Getting user files attributes");

    GetUserObjectBasicAttributes<TUserFile>(
        AuthenticatedMasterClient,
        Files,
        InputTransaction->GetId(),
        Logger,
        EPermission::Read);

    for (const auto& file : Files) {
        const auto& path = file.Path.GetPath();
        if (file.Type != EObjectType::Table && file.Type != EObjectType::File) {
            THROW_ERROR_EXCEPTION("Object %v has invalid type: expected %Qlv or %Qlv, actual %Qlv",
                path,
                EObjectType::Table,
                EObjectType::File,
                file.Type);
        }
    }

    {
        auto channel = AuthenticatedOutputMasterClient->GetMasterChannelOrThrow(EMasterChannelKind::Follower);
        TObjectServiceProxy proxy(channel);
        auto batchReq = proxy.ExecuteBatch();

        for (const auto& file : Files) {
            auto objectIdPath = FromObjectId(file.ObjectId);
            {
                auto req = TYPathProxy::Get(objectIdPath + "/@");
                SetTransactionId(req, InputTransaction->GetId());
                std::vector<Stroka> attributeKeys;
                attributeKeys.push_back("file_name");
                switch (file.Type) {
                    case EObjectType::File:
                        attributeKeys.push_back("executable");
                        break;

                    case EObjectType::Table:
                        attributeKeys.push_back("format");
                        attributeKeys.push_back("dynamic");
                        attributeKeys.push_back("schema");
                        attributeKeys.push_back("retained_timestamp");
                        attributeKeys.push_back("unflushed_timestamp");
                        break;

                    default:
                        Y_UNREACHABLE();
                }
                attributeKeys.push_back("key");
                attributeKeys.push_back("chunk_count");
                attributeKeys.push_back("uncompressed_data_size");
                ToProto(req->mutable_attributes()->mutable_keys(), attributeKeys);
                batchReq->AddRequest(req, "get_attributes");
            }

            {
                auto req = TYPathProxy::Get(file.Path.GetPath() + "&/@");
                SetTransactionId(req, InputTransaction->GetId());
                std::vector<Stroka> attributeKeys;
                attributeKeys.push_back("key");
                attributeKeys.push_back("file_name");
                ToProto(req->mutable_attributes()->mutable_keys(), attributeKeys);
                batchReq->AddRequest(req, "get_link_attributes");
            }
        }

        auto batchRspOrError = WaitFor(batchReq->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(batchRspOrError, "Error getting attributes of user files");
        const auto& batchRsp = batchRspOrError.Value();

        TEnumIndexedVector<yhash_set<Stroka>, EOperationStage> userFileNames;
        auto validateUserFileName = [&] (const TUserFile& file) {
            // TODO(babenko): more sanity checks?
            auto path = file.Path.GetPath();
            const auto& fileName = file.FileName;
            if (fileName.empty()) {
                THROW_ERROR_EXCEPTION("Empty user file name for %v",
                    path);
            }

            if (!NFS::GetRealPath(NFS::CombinePaths("sandbox", fileName)).StartsWith(NFS::GetRealPath("sandbox"))) {
                THROW_ERROR_EXCEPTION("User file name cannot reference outside of sandbox directory")
                    << TErrorAttribute("file_name", fileName);
            }


            if (!userFileNames[file.Stage].insert(fileName).second) {
                THROW_ERROR_EXCEPTION("Duplicate user file name %Qv for %v",
                    fileName,
                    path);
            }
        };

        auto getAttributesRspsOrError = batchRsp->GetResponses<TYPathProxy::TRspGetKey>("get_attributes");
        auto getLinkAttributesRspsOrError = batchRsp->GetResponses<TYPathProxy::TRspGetKey>("get_link_attributes");
        for (int index = 0; index < Files.size(); ++index) {
            auto& file = Files[index];
            const auto& path = file.Path.GetPath();

            {
                const auto& rspOrError = getAttributesRspsOrError[index];
                THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error getting attributes of user file %Qv", path);
                const auto& rsp = rspOrError.Value();
                const auto& linkRsp = getLinkAttributesRspsOrError[index];

                file.Attributes = ConvertToAttributes(TYsonString(rsp->value()));
                const auto& attributes = *file.Attributes;

                try {
                    if (linkRsp.IsOK()) {
                        auto linkAttributes = ConvertToAttributes(TYsonString(linkRsp.Value()->value()));
                        file.FileName = linkAttributes->Get<Stroka>("key");
                        file.FileName = linkAttributes->Find<Stroka>("file_name").Get(file.FileName);
                    } else {
                        file.FileName = attributes.Get<Stroka>("key");
                        file.FileName = attributes.Find<Stroka>("file_name").Get(file.FileName);
                    }
                    file.FileName = file.Path.GetFileName().Get(file.FileName);
                } catch (const std::exception& ex) {
                    // NB: Some of the above Gets and Finds may throw due to, e.g., type mismatch.
                    THROW_ERROR_EXCEPTION("Error parsing attributes of user file %v",
                        path) << ex;
                }

                switch (file.Type) {
                    case EObjectType::File:
                        file.Executable = attributes.Find<bool>("executable").Get(file.Executable);
                        file.Executable = file.Path.GetExecutable().Get(file.Executable);
                        break;

                    case EObjectType::Table:
                        file.IsDynamic = attributes.Get<bool>("dynamic");
                        file.Schema = attributes.Get<TTableSchema>("schema");
                        file.Format = attributes.FindYson("format");
                        if (!file.Format) {
                            file.Format = file.Path.GetFormat();
                        }
                        // Validate that format is correct.
                        try {
                            if (!file.Format) {
                                THROW_ERROR_EXCEPTION("Format is missing");
                            }
                            ConvertTo<TFormat>(file.Format);
                        } catch (const std::exception& ex) {
                            THROW_ERROR_EXCEPTION("Failed to parse format of table file %v",
                                file.Path) << ex;
                        }
                        // Validate that timestamp is correct.
                        ValidateDynamicTableTimestamp(file.Path, file.IsDynamic, file.Schema, attributes);

                        break;

                    default:
                        Y_UNREACHABLE();
                }

                i64 fileSize = attributes.Get<i64>("uncompressed_data_size");
                if (fileSize > Config->MaxFileSize) {
                    THROW_ERROR_EXCEPTION(
                        "User file %v exceeds size limit: %v > %v",
                        path,
                        fileSize,
                        Config->MaxFileSize);
                }

                i64 chunkCount = attributes.Get<i64>("chunk_count");
                if (chunkCount > Config->MaxChunksPerFetch) {
                    THROW_ERROR_EXCEPTION(
                        "User file %v exceeds chunk count limit: %v > %v",
                        path,
                        chunkCount,
                        Config->MaxChunksPerFetch);
                }
                file.ChunkCount = chunkCount;

                LOG_INFO("User file locked (Path: %v, Stage: %v, FileName: %v)",
                    path,
                    file.Stage,
                    file.FileName);
            }

            validateUserFileName(file);
        }
    }
}

void TOperationControllerBase::PrepareInputQuery()
{ }

void TOperationControllerBase::ParseInputQuery(
    const Stroka& queryString,
    const TNullable<TTableSchema>& schema)
{
    for (const auto& table : InputTables) {
        if (table.Path.GetColumns()) {
            THROW_ERROR_EXCEPTION("Column filter and QL filter cannot appear in the same operation");
        }
    }

    auto externalCGInfo = New<TExternalCGInfo>();
    auto nodeDirectory = New<NNodeTrackerClient::TNodeDirectory>();
    auto fetchFunctions = [&] (const std::vector<Stroka>& names, const TTypeInferrerMapPtr& typeInferrers) {
        MergeFrom(typeInferrers.Get(), *BuiltinTypeInferrersMap);

        std::vector<Stroka> externalNames;
        for (const auto& name : names) {
            auto found = typeInferrers->find(name);
            if (found == typeInferrers->end()) {
                externalNames.push_back(name);
            }
        }

        if (externalNames.empty()) {
            return;
        }

        if (!Config->UdfRegistryPath) {
            THROW_ERROR_EXCEPTION("External UDF registry is not configured");
        }

        auto descriptors = LookupAllUdfDescriptors(externalNames, Config->UdfRegistryPath.Get(), Host->GetMasterClient());

        AppendUdfDescriptors(typeInferrers, externalCGInfo, externalNames, descriptors);
    };

    auto inferSchema = [&] () {
        std::vector<TTableSchema> schemas;
        for (const auto& table : InputTables) {
            schemas.push_back(table.Schema);
        }
        return InferInputSchema(schemas, false);
    };

    TQueryOptions options;
    options.VerboseLogging = true;
    options.RangeExpansionLimit = Config->MaxRangesOnTable;

    auto query = PrepareJobQuery(
        queryString,
        schema ? *schema : inferSchema(),
        fetchFunctions);
    auto rangeInferrer = CreateRangeInferrer(
        query->WhereClause,
        query->OriginalSchema,
        query->GetKeyColumns(),
        Host->GetMasterClient()->GetNativeConnection()->GetColumnEvaluatorCache(),
        BuiltinRangeExtractorMap,
        options);

    InputQuery.Emplace();
    InputQuery->Query = std::move(query);
    InputQuery->ExternalCGInfo = std::move(externalCGInfo);
    InputQuery->RangeInferrer = std::move(rangeInferrer);
}

void TOperationControllerBase::WriteInputQueryToJobSpec(
    NScheduler::NProto::TSchedulerJobSpecExt* schedulerJobSpecExt)
{
    auto* querySpec = schedulerJobSpecExt->mutable_input_query_spec();
    ToProto(querySpec->mutable_query(), InputQuery->Query);
    ToProto(querySpec->mutable_external_functions(), InputQuery->ExternalCGInfo->Functions);
}

void TOperationControllerBase::CollectTotals()
{
    for (const auto& table : InputTables) {
        for (const auto& inputChunk : table.Chunks) {
            if (IsUnavailable(inputChunk, IsParityReplicasFetchEnabled())) {
                const auto& chunkId = inputChunk->ChunkId();
                if (table.IsDynamic && table.Schema.IsSorted()) {
                    THROW_ERROR_EXCEPTION("Input chunk %v of sorted dynamic table %v is unavailable",
                        chunkId,
                        table.Path.GetPath());
                }

                switch (Spec->UnavailableChunkStrategy) {
                    case EUnavailableChunkAction::Fail:
                        THROW_ERROR_EXCEPTION("Input chunk %v is unavailable",
                            chunkId);

                    case EUnavailableChunkAction::Skip:
                        LOG_TRACE("Skipping unavailable chunk (ChunkId: %v)",
                            chunkId);
                        continue;

                    case EUnavailableChunkAction::Wait:
                        // Do nothing.
                        break;

                    default:
                        Y_UNREACHABLE();
                }
            }

            if (table.IsPrimary()) {
                PrimaryInputDataSize += inputChunk->GetUncompressedDataSize();
            } else {
                ForeignInputDataSize += inputChunk->GetUncompressedDataSize();
            }

            TotalEstimatedInputDataSize += inputChunk->GetUncompressedDataSize();
            TotalEstimatedInputRowCount += inputChunk->GetRowCount();
            TotalEstimatedCompressedDataSize += inputChunk->GetCompressedDataSize();
            TotalEstimatedInputDataWeight += inputChunk->GetDataWeight();
            ++TotalEstimatedInputChunkCount;
        }
    }

    LOG_INFO("Estimated input totals collected (ChunkCount: %v, RowCount: %v, UncompressedDataSize: %v, CompressedDataSize: %v, DataWeight: %v)",
        TotalEstimatedInputChunkCount,
        TotalEstimatedInputRowCount,
        TotalEstimatedInputDataSize,
        TotalEstimatedCompressedDataSize,
        TotalEstimatedInputDataWeight);
}

void TOperationControllerBase::CustomPrepare()
{ }

void TOperationControllerBase::ClearInputChunkBoundaryKeys()
{
    for (auto& pair : InputChunkMap) {
        auto& inputChunkDescriptor = pair.second;
        for (auto chunkSpec : inputChunkDescriptor.InputChunks) {
            // We don't need boundary key ext after preparation phase (for primary tables only).
            if (InputTables[chunkSpec->GetTableIndex()].IsPrimary()) {
                chunkSpec->ReleaseBoundaryKeys();
            }
        }
    }
}

// NB: must preserve order of chunks in the input tables, no shuffling.
std::vector<TInputChunkPtr> TOperationControllerBase::CollectPrimaryChunks(bool versioned) const
{
    std::vector<TInputChunkPtr> result;
    for (const auto& table : InputTables) {
        if (!table.IsForeign() && ((table.IsDynamic && table.Schema.IsSorted()) == versioned)) {
            for (const auto& chunk : table.Chunks) {
                if (!table.IsDynamic && IsUnavailable(chunk, IsParityReplicasFetchEnabled())) {
                    switch (Spec->UnavailableChunkStrategy) {
                        case EUnavailableChunkAction::Skip:
                            continue;

                        case EUnavailableChunkAction::Wait:
                            // Do nothing.
                            break;

                        default:
                            Y_UNREACHABLE();
                    }
                }
                result.push_back(chunk);
            }
        }
    }
    return result;
}

std::vector<TInputChunkPtr> TOperationControllerBase::CollectPrimaryUnversionedChunks() const
{
    return CollectPrimaryChunks(false);
}

std::vector<TInputChunkPtr> TOperationControllerBase::CollectPrimaryVersionedChunks() const
{
    return CollectPrimaryChunks(true);
}

std::pair<i64, i64> TOperationControllerBase::CalculatePrimaryVersionedChunksStatistics() const
{
    i64 dataSize = 0;
    i64 rowCount = 0;
    for (const auto& table : InputTables) {
        if (!table.IsForeign() && table.IsDynamic && table.Schema.IsSorted()) {
            for (const auto& chunk : table.Chunks) {
                dataSize += chunk->GetUncompressedDataSize();
                rowCount += chunk->GetRowCount();
            }
        }
    }
    return std::make_pair(dataSize, rowCount);
}

std::vector<TInputDataSlicePtr> TOperationControllerBase::CollectPrimaryVersionedDataSlices(i64 sliceSize) const
{
    TScrapeChunksCallback scraperCallback;
    if (Spec->UnavailableChunkStrategy == EUnavailableChunkAction::Wait) {
        scraperCallback = CreateScrapeChunksSessionCallback(
            Config->ChunkScraper,
            GetCancelableInvoker(),
            Host->GetChunkLocationThrottlerManager(),
            AuthenticatedInputMasterClient,
            InputNodeDirectory,
            Logger);
    }

    std::vector<TFuture<void>> asyncResults;
    std::vector<TDataSliceFetcherPtr> fetchers;

    for (const auto& table : InputTables) {
        if (!table.IsForeign() && table.IsDynamic && table.Schema.IsSorted()) {
            auto fetcher = New<TDataSliceFetcher>(
                Config->Fetcher,
                sliceSize,
                table.Schema.GetKeyColumns(),
                true,
                InputNodeDirectory,
                GetCancelableInvoker(),
                scraperCallback,
                Host->GetMasterClient(),
                RowBuffer,
                Logger);

            for (const auto& chunk : table.Chunks) {
                fetcher->AddChunk(chunk);
            }

            asyncResults.emplace_back(fetcher->Fetch());
            fetchers.emplace_back(std::move(fetcher));
        }
    }

    WaitFor(Combine(asyncResults))
        .ThrowOnError();

    std::vector<TInputDataSlicePtr> result;
    for (const auto& fetcher : fetchers) {
        for (auto& dataSlice : fetcher->GetDataSlices()) {
            LOG_TRACE("Added dynamic table slice (TablePath: %v, Range: %v..%v, ChunkIds: %v)",
                InputTables[dataSlice->GetTableIndex()].Path.GetPath(),
                dataSlice->LowerLimit(),
                dataSlice->UpperLimit(),
                dataSlice->ChunkSlices);
            result.emplace_back(std::move(dataSlice));
        }
    }

    return result;
}

std::vector<std::deque<TInputDataSlicePtr>> TOperationControllerBase::CollectForeignInputDataSlices(int foreignKeyColumnCount) const
{
    std::vector<std::deque<TInputDataSlicePtr>> result;
    for (const auto& table : InputTables) {
        if (table.IsForeign()) {
            result.push_back(std::deque<TInputDataSlicePtr>());

            if (table.IsDynamic && table.Schema.IsSorted()) {
                std::vector<TInputChunkSlicePtr> chunkSlices;
                chunkSlices.reserve(table.Chunks.size());
                for (const auto& chunkSpec : table.Chunks) {
                    chunkSlices.push_back(CreateInputChunkSlice(
                        chunkSpec,
                        RowBuffer->Capture(chunkSpec->BoundaryKeys()->MinKey.Get()),
                        GetKeySuccessor(chunkSpec->BoundaryKeys()->MaxKey.Get(), RowBuffer)));
                }

                auto dataSlices = CombineVersionedChunkSlices(chunkSlices);
                for (const auto& dataSlice : dataSlices) {
                    if (IsUnavailable(dataSlice, IsParityReplicasFetchEnabled())) {
                        switch (Spec->UnavailableChunkStrategy) {
                            case EUnavailableChunkAction::Skip:
                                continue;

                            case EUnavailableChunkAction::Wait:
                                // Do nothing.
                                break;

                            default:
                                Y_UNREACHABLE();
                        }
                    }
                    result.back().push_back(dataSlice);
                }
            } else {
                for (const auto& inputChunk : table.Chunks) {
                    if (IsUnavailable(inputChunk, IsParityReplicasFetchEnabled())) {
                        switch (Spec->UnavailableChunkStrategy) {
                            case EUnavailableChunkAction::Skip:
                                continue;

                            case EUnavailableChunkAction::Wait:
                                // Do nothing.
                                break;

                            default:
                                Y_UNREACHABLE();
                        }
                    }
                    result.back().push_back(CreateUnversionedInputDataSlice(CreateInputChunkSlice(
                        inputChunk,
                        GetKeyPrefix(inputChunk->BoundaryKeys()->MinKey.Get(), foreignKeyColumnCount, RowBuffer),
                        GetKeyPrefixSuccessor(inputChunk->BoundaryKeys()->MaxKey.Get(), foreignKeyColumnCount, RowBuffer))));
                }
            }
        }
    }
    return result;
}

bool TOperationControllerBase::InputHasDynamicTables() const
{
    for (const auto& table : InputTables) {
        if (table.IsDynamic) {
            return true;
        }
    }
    return false;
}

bool TOperationControllerBase::InputHasVersionedTables() const
{
    for (const auto& table : InputTables) {
        if (table.IsDynamic && table.Schema.IsSorted()) {
            return true;
        }
    }
    return false;
}

bool TOperationControllerBase::InputHasReadLimits() const
{
    for (const auto& table : InputTables) {
        if (table.Path.HasNontrivialRanges()) {
            return true;
        }
    }
    return false;
}

void TOperationControllerBase::SliceUnversionedChunks(
    const std::vector<TInputChunkPtr>& unversionedChunks,
    const IJobSizeConstraintsPtr& jobSizeConstraints,
    std::vector<TChunkStripePtr>* result) const
{
    auto appendStripes = [&] (const std::vector<TInputChunkSlicePtr>& slices) {
        for (const auto& slice : slices) {
            result->push_back(New<TChunkStripe>(CreateUnversionedInputDataSlice(slice)));
        }
    };

    for (const auto& chunkSpec : unversionedChunks) {
        int oldSize = result->size();

        bool hasNontrivialLimits = !chunkSpec->IsCompleteChunk();

        auto codecId = NErasure::ECodec(chunkSpec->GetErasureCodec());
        if (hasNontrivialLimits || codecId == NErasure::ECodec::None) {
            auto slices = SliceChunkByRowIndexes(
                chunkSpec,
                jobSizeConstraints->GetInputSliceDataSize(),
                jobSizeConstraints->GetInputSliceRowCount());

            appendStripes(slices);
        } else {
            for (const auto& slice : CreateErasureInputChunkSlices(chunkSpec, codecId)) {
                auto slices = slice->SliceEvenly(
                    jobSizeConstraints->GetInputSliceDataSize(),
                    jobSizeConstraints->GetInputSliceRowCount());

                appendStripes(slices);
            }
        }

        LOG_TRACE("Slicing chunk (ChunkId: %v, SliceCount: %v)",
            chunkSpec->ChunkId(),
            result->size() - oldSize);
    }
}

void TOperationControllerBase::SlicePrimaryUnversionedChunks(
    const IJobSizeConstraintsPtr& jobSizeConstraints,
    std::vector<TChunkStripePtr>* result) const
{
    SliceUnversionedChunks(CollectPrimaryUnversionedChunks(), jobSizeConstraints, result);
}

void TOperationControllerBase::SlicePrimaryVersionedChunks(
    const IJobSizeConstraintsPtr& jobSizeConstraints,
    std::vector<TChunkStripePtr>* result) const
{
    for (const auto& dataSlice : CollectPrimaryVersionedDataSlices(jobSizeConstraints->GetInputSliceDataSize())) {
        result->push_back(New<TChunkStripe>(dataSlice));
    }
}

bool TOperationControllerBase::IsJobInterruptible() const
{
    return false;
}

void TOperationControllerBase::ReinstallUnreadInputDataSlices(
    const std::vector<NChunkClient::TInputDataSlicePtr>& inputDataSlices)
{
    Y_UNREACHABLE();
}

std::vector<TInputDataSlicePtr> TOperationControllerBase::ExtractInputDataSlices(const TCompletedJobSummary& jobSummary) const
{
    std::vector<TInputDataSlicePtr> dataSliceList;

    const auto& result = jobSummary.Result;
    const auto& schedulerResultExt = result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);

    std::vector<TDataSliceDescriptor> dataSliceDescriptors;
    if (schedulerResultExt.unread_chunk_specs_size() > 0) {
        FromProto(
            &dataSliceDescriptors,
            schedulerResultExt.unread_chunk_specs(),
            schedulerResultExt.chunk_spec_count_per_data_slice());
    } else if (schedulerResultExt.unread_input_data_slice_descriptors_size() > 0) {
        // COMPAT(psushin).
        dataSliceDescriptors = FromProto<std::vector<TDataSliceDescriptor>>(
            schedulerResultExt.unread_input_data_slice_descriptors());
    }

    for (const auto& dataSliceDescriptor : dataSliceDescriptors) {
        std::vector<TInputChunkSlicePtr> chunkSliceList;
        chunkSliceList.reserve(dataSliceDescriptor.ChunkSpecs.size());
        for (const auto& protoChunkSpec : dataSliceDescriptor.ChunkSpecs) {
            auto chunkId = FromProto<TChunkId>(protoChunkSpec.chunk_id());
            auto it = InputChunkMap.find(chunkId);
            YCHECK(it != InputChunkMap.end());
            const auto& inputChunks = it->second.InputChunks;
            auto chunkIt = std::find_if(
                inputChunks.begin(),
                inputChunks.end(),
                [&] (const TInputChunkPtr& inputChunk) -> bool {
                    return inputChunk->GetChunkIndex() == protoChunkSpec.chunk_index();
                });
            YCHECK(chunkIt != inputChunks.end());
            auto chunkSlice = New<TInputChunkSlice>(*chunkIt, RowBuffer, protoChunkSpec);
            chunkSliceList.emplace_back(std::move(chunkSlice));
        }
        if (InputTables[dataSliceDescriptor.GetDataSourceIndex()].IsDynamic) {
            dataSliceList.emplace_back(CreateVersionedInputDataSlice(chunkSliceList));
        } else {
            YCHECK(chunkSliceList.size() == 1);
            dataSliceList.emplace_back(CreateUnversionedInputDataSlice(chunkSliceList[0]));
        }
        dataSliceList.back()->Tag = dataSliceDescriptor.GetTag();
    }
    return dataSliceList;
}

int TOperationControllerBase::EstimateSplitJobCount(const TCompletedJobSummary& jobSummary)
{
    const auto& inputDataSlices = jobSummary.UnreadInputDataSlices;
    int jobCount = 1;

    if (JobSplitter_) {
        i64 unreadRowCount = GetCumulativeRowCount(inputDataSlices);
        jobCount = JobSplitter_->EstimateJobCount(jobSummary, unreadRowCount);
    }
    return jobCount;
}

TKeyColumns TOperationControllerBase::CheckInputTablesSorted(
    const TKeyColumns& keyColumns,
    std::function<bool(const TInputTable& table)> inputTableFilter)
{
    YCHECK(!InputTables.empty());

    for (const auto& table : InputTables) {
        if (inputTableFilter(table) && !table.Schema.IsSorted()) {
            THROW_ERROR_EXCEPTION("Input table %v is not sorted",
                table.Path.GetPath());
        }
    }

    auto validateColumnFilter = [] (const TInputTable& table, const TKeyColumns& keyColumns) {
        auto columns = table.Path.GetColumns();
        if (!columns) {
            return;
        }

        auto columnSet = yhash_set<Stroka>(columns->begin(), columns->end());
        for (const auto& keyColumn : keyColumns) {
            if (columnSet.find(keyColumn) == columnSet.end()) {
                THROW_ERROR_EXCEPTION("Column filter for input table %v doesn't include key column %Qv",
                    table.Path.GetPath(),
                    keyColumn);
            }
        }
    };

    if (!keyColumns.empty()) {
        for (const auto& table : InputTables) {
            if (!inputTableFilter(table)) {
                continue;
            }

            if (!CheckKeyColumnsCompatible(table.Schema.GetKeyColumns(), keyColumns)) {
                THROW_ERROR_EXCEPTION("Input table %v is sorted by columns %v that are not compatible "
                    "with the requested columns %v",
                    table.Path.GetPath(),
                    table.Schema.GetKeyColumns(),
                    keyColumns);
            }
            validateColumnFilter(table, keyColumns);
        }
        return keyColumns;
    } else {
        for (const auto& referenceTable : InputTables) {
            if (inputTableFilter(referenceTable)) {
                for (const auto& table : InputTables) {
                    if (!inputTableFilter(table)) {
                        continue;
                    }

                    if (table.Schema.GetKeyColumns() != referenceTable.Schema.GetKeyColumns()) {
                        THROW_ERROR_EXCEPTION("Key columns do not match: input table %v is sorted by columns %v "
                            "while input table %v is sorted by columns %v",
                            table.Path.GetPath(),
                            table.Schema.GetKeyColumns(),
                            referenceTable.Path.GetPath(),
                            referenceTable.Schema.GetKeyColumns());
                    }
                    validateColumnFilter(table, referenceTable.Schema.GetKeyColumns());
                }
                return referenceTable.Schema.GetKeyColumns();
            }
        }
    }
    Y_UNREACHABLE();
}

bool TOperationControllerBase::CheckKeyColumnsCompatible(
    const TKeyColumns& fullColumns,
    const TKeyColumns& prefixColumns)
{
    if (fullColumns.size() < prefixColumns.size()) {
        return false;
    }

    for (int index = 0; index < prefixColumns.size(); ++index) {
        if (fullColumns[index] != prefixColumns[index]) {
            return false;
        }
    }

    return true;
}


bool TOperationControllerBase::ShouldVerifySortedOutput() const
{
    return true;
}

bool TOperationControllerBase::IsParityReplicasFetchEnabled() const
{
    return false;
}

bool TOperationControllerBase::IsBoundaryKeysFetchEnabled() const
{
    return false;
}

void TOperationControllerBase::RegisterOutput(
    const TChunkTreeId& chunkTreeId,
    int key,
    int tableIndex,
    TOutputTable& table)
{
    if (!chunkTreeId) {
        return;
    }

    table.OutputChunkTreeIds.insert(std::make_pair(key, chunkTreeId));

    if (IsOutputLivePreviewSupported()) {
        MasterConnector->AttachToLivePreview(
            OperationId,
            AsyncSchedulerTransaction->GetId(),
            table.LivePreviewTableId,
            {chunkTreeId});
    }

    LOG_DEBUG("Output chunk tree registered (Table: %v, ChunkTreeId: %v, Key: %v)",
        tableIndex,
        chunkTreeId,
        key);
}

void TOperationControllerBase::RegisterStderr(TJobletPtr joblet, const TJobSummary& jobSummary)
{
    if (!joblet->StderrTableChunkListId) {
        return;
    }

    YCHECK(StderrTable);

    const auto& chunkListId = joblet->StderrTableChunkListId;
    const auto& result = jobSummary.Result;

    if (!result.HasExtension(TSchedulerJobResultExt::scheduler_job_result_ext)) {
        return;
    }
    const auto& schedulerResultExt = result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);

    YCHECK(schedulerResultExt.has_stderr_table_boundary_keys());

    StderrTable->OutputChunkTreeIds.emplace(0, chunkListId);
    const auto& boundaryKeys = schedulerResultExt.stderr_table_boundary_keys();
    RegisterBoundaryKeys(boundaryKeys, chunkListId, StderrTable.GetPtr());

    MasterConnector->AttachToLivePreview(
        OperationId,
        AsyncSchedulerTransaction->GetId(),
        StderrTable->LivePreviewTableId,
        {chunkListId});
    LOG_DEBUG("Stderr chunk tree registered (ChunkListId: %v)",
        chunkListId);
}

void TOperationControllerBase::RegisterCores(TJobletPtr joblet, const TJobSummary& jobSummary)
{
    if (!joblet->CoreTableChunkListId) {
        return;
    }

    YCHECK(CoreTable);

    const auto& chunkListId = joblet->CoreTableChunkListId;
    const auto& result = jobSummary.Result;

    if (!result.HasExtension(TSchedulerJobResultExt::scheduler_job_result_ext)) {
        return;
    }
    const auto& schedulerResultExt = result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);

    for (const auto& coreInfo : schedulerResultExt.core_infos()) {
        LOG_DEBUG("Core file (JobId: %v, ProcessId: %v, ExecutableName: %v, Size: %v, Error: %v)",
            joblet->JobId,
            coreInfo.process_id(),
            coreInfo.executable_name(),
            coreInfo.size(),
            coreInfo.has_error() ? FromProto<TError>(coreInfo.error()) : TError());
    }

    const auto& boundaryKeys = schedulerResultExt.core_table_boundary_keys();
    RegisterBoundaryKeys(boundaryKeys, chunkListId, CoreTable.GetPtr());
}

void TOperationControllerBase::RegisterBoundaryKeys(
    const TOutputResult& boundaryKeys,
    const TChunkTreeId& chunkTreeId,
    TOutputTable* outputTable)
{
    if (boundaryKeys.empty()) {
        return;
    }

    YCHECK(boundaryKeys.sorted());
    YCHECK(!outputTable->Options->ValidateUniqueKeys || boundaryKeys.unique_keys());

    auto trimAndCaptureKey = [&] (const TOwningKey& key) {
        int limit = outputTable->TableUploadOptions.TableSchema.GetKeyColumnCount();
        if (key.GetCount() > limit) {
            // NB: This can happen for a teleported chunk from a table with a wider key in sorted (but not unique_keys) mode.
            YCHECK(!outputTable->Options->ValidateUniqueKeys);
            return RowBuffer->Capture(key.Begin(), limit);
        } else {
            return RowBuffer->Capture(key.Begin(), key.GetCount());
        }
    };

    outputTable->BoundaryKeys.push_back(TJobBoundaryKeys{
        trimAndCaptureKey(FromProto<TOwningKey>(boundaryKeys.min())),
        trimAndCaptureKey(FromProto<TOwningKey>(boundaryKeys.max())),
        chunkTreeId
    });
}

const TTransactionId& TOperationControllerBase::GetTransactionIdForOutputTable(const TOutputTable& table)
{
    if (table.OutputType == EOutputTableType::Output) {
        if (CompletionTransaction) {
            return CompletionTransaction->GetId();
        } else {
            return OutputTransaction->GetId();
        }
    } else {
        YCHECK(table.OutputType == EOutputTableType::Stderr || table.OutputType == EOutputTableType::Core);
        return DebugOutputTransaction->GetId();
    }
}

void TOperationControllerBase::RegisterOutput(
    TInputChunkPtr chunkSpec,
    int key,
    int tableIndex)
{
    auto& table = OutputTables[tableIndex];

    if (table.TableUploadOptions.TableSchema.IsSorted() && ShouldVerifySortedOutput()) {
        YCHECK(chunkSpec->BoundaryKeys());

        TOutputResult resultBoundaryKeys;
        // Chunk must have at least one row.
        YCHECK(chunkSpec->GetRowCount() > 0);
        resultBoundaryKeys.set_empty(false);
        resultBoundaryKeys.set_sorted(true);
        resultBoundaryKeys.set_unique_keys(chunkSpec->GetUniqueKeys());
        ToProto(resultBoundaryKeys.mutable_min(), chunkSpec->BoundaryKeys()->MinKey);
        ToProto(resultBoundaryKeys.mutable_max(), chunkSpec->BoundaryKeys()->MaxKey);

        RegisterBoundaryKeys(resultBoundaryKeys, chunkSpec->ChunkId(), &table);
    }

    RegisterOutput(chunkSpec->ChunkId(), key, tableIndex, table);
}

void TOperationControllerBase::RegisterOutput(
    TJobletPtr joblet,
    int key,
    const TCompletedJobSummary& jobSummary)
{
    RegisterOutput(joblet->ChunkListIds, key, jobSummary);
}

void TOperationControllerBase::RegisterOutput(
    const std::vector<TChunkListId>& chunkListIds,
    int key,
    const TCompletedJobSummary& jobSummary)
{
    const auto& result = jobSummary.Result;
    const auto& schedulerResultExt = result.GetExtension(TSchedulerJobResultExt::scheduler_job_result_ext);

    for (int tableIndex = 0; tableIndex < OutputTables.size(); ++tableIndex) {
        auto& table = OutputTables[tableIndex];
        RegisterOutput(chunkListIds[tableIndex], key, tableIndex, table);

        if (table.TableUploadOptions.TableSchema.IsSorted() && ShouldVerifySortedOutput() && !jobSummary.Abandoned) {
            YCHECK(tableIndex < schedulerResultExt.output_boundary_keys_size());
            const auto& boundaryKeys = schedulerResultExt.output_boundary_keys(tableIndex);
            RegisterBoundaryKeys(boundaryKeys, chunkListIds[tableIndex], &table);
        }
    }
}

void TOperationControllerBase::RegisterInputStripe(TChunkStripePtr stripe, TTaskPtr task)
{
    yhash_set<TChunkId> visitedChunks;

    for (const auto& slice : stripe->DataSlices) {
        slice->Tag = CurrentInputDataSliceTag_++;
    }

    TStripeDescriptor stripeDescriptor;
    stripeDescriptor.Stripe = stripe;
    stripeDescriptor.Task = task;
    stripeDescriptor.Cookie = task->GetChunkPoolInput()->Add(stripe);

    for (const auto& dataSlice : stripe->DataSlices) {
        for (const auto& slice : dataSlice->ChunkSlices) {
            auto inputChunk = slice->GetInputChunk();
            const auto& chunkId = inputChunk->ChunkId();

            if (!visitedChunks.insert(chunkId).second) {
                continue;
            }

            auto chunkDescriptorIt = InputChunkMap.find(chunkId);
            YCHECK(chunkDescriptorIt != InputChunkMap.end());

            auto& chunkDescriptor = chunkDescriptorIt->second;
            chunkDescriptor.InputStripes.push_back(stripeDescriptor);

            if (chunkDescriptor.State == EInputChunkState::Waiting) {
                ++stripe->WaitingChunkCount;
            }
        }
    }

    if (stripe->WaitingChunkCount > 0) {
        task->GetChunkPoolInput()->Suspend(stripeDescriptor.Cookie);
    }
}

void TOperationControllerBase::RegisterIntermediate(
    TJobletPtr joblet,
    TCompletedJobPtr completedJob,
    TChunkStripePtr stripe,
    bool attachToLivePreview)
{
    for (const auto& dataSlice : stripe->DataSlices) {
        // NB: intermediate slice must be trivial.
        const auto& chunkId = dataSlice->GetSingleUnversionedChunkOrThrow()->ChunkId();
        YCHECK(ChunkOriginMap.insert(std::make_pair(chunkId, completedJob)).second);

        if (attachToLivePreview && IsIntermediateLivePreviewSupported()) {
            MasterConnector->AttachToLivePreview(
                OperationId,
                AsyncSchedulerTransaction->GetId(),
                IntermediateTable.LivePreviewTableId,
                {chunkId});
        }
    }

    IntermediateChunkScraper->Restart();
}

bool TOperationControllerBase::HasEnoughChunkLists(bool intermediate, bool isWritingStderrTable, bool isWritingCoreTable)
{
    const auto& cellTagToRequiredChunkList = intermediate
        ? CellTagToIntermediateRequiredChunkList
        : CellTagToOutputRequiredChunkList;
    for (const auto& pair : cellTagToRequiredChunkList) {
        const auto cellTag = pair.first;
        auto requiredChunkList = pair.second;
        if (StderrTable && !isWritingStderrTable && StderrTable->CellTag == cellTag) {
            --requiredChunkList;
        }
        if (CoreTable && !isWritingCoreTable && CoreTable->CellTag == cellTag) {
            --requiredChunkList;
        }
        if (requiredChunkList && !ChunkListPool->HasEnough(cellTag, requiredChunkList)) {
            return false;
        }
    }
    return true;
}

TChunkListId TOperationControllerBase::ExtractChunkList(TCellTag cellTag)
{
    return ChunkListPool->Extract(cellTag);
}

void TOperationControllerBase::ReleaseChunkLists(const std::vector<TChunkListId>& ids)
{
    ChunkListPool->Release(ids);
}

void TOperationControllerBase::RegisterJoblet(TJobletPtr joblet)
{
    YCHECK(JobletMap.insert(std::make_pair(joblet->JobId, joblet)).second);
}

TOperationControllerBase::TJobletPtr TOperationControllerBase::FindJoblet(const TJobId& jobId) const
{
    auto it = JobletMap.find(jobId);
    return it == JobletMap.end() ? nullptr : it->second;
}

TOperationControllerBase::TJobletPtr TOperationControllerBase::GetJoblet(const TJobId& jobId) const
{
    auto joblet = FindJoblet(jobId);
    YCHECK(joblet);
    return joblet;
}

TOperationControllerBase::TJobletPtr TOperationControllerBase::GetJobletOrThrow(const TJobId& jobId) const
{
    auto joblet = FindJoblet(jobId);
    if (!joblet) {
        THROW_ERROR_EXCEPTION(
            NScheduler::EErrorCode::NoSuchJob,
            "No such job %v",
            jobId);
    }
    return joblet;
}

void TOperationControllerBase::RemoveJoblet(const TJobId& jobId)
{
    YCHECK(JobletMap.erase(jobId) == 1);
}

bool TOperationControllerBase::HasProgress() const
{
    return IsPrepared() && ProgressString_ && BriefProgressString_;
}

bool TOperationControllerBase::HasJobSplitterInfo() const
{
    return IsPrepared() && JobSplitter_;
}

void TOperationControllerBase::BuildOperationAttributes(IYsonConsumer* consumer) const
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    BuildYsonMapFluently(consumer)
        .Item("async_scheduler_transaction_id").Value(AsyncSchedulerTransaction ? AsyncSchedulerTransaction->GetId() : NullTransactionId)
        .Item("input_transaction_id").Value(InputTransaction ? InputTransaction->GetId() : NullTransactionId)
        .Item("output_transaction_id").Value(OutputTransaction ? OutputTransaction->GetId() : NullTransactionId)
        .Item("debug_output_transaction_id").Value(DebugOutputTransaction ? DebugOutputTransaction->GetId() : NullTransactionId)
        .Item("user_transaction_id").Value(UserTransactionId);
}

void TOperationControllerBase::BuildProgress(IYsonConsumer* consumer) const
{
    BuildYsonMapFluently(consumer)
        .Item("build_time").Value(TInstant::Now())
        .Item("jobs").Value(JobCounter)
        .Item("ready_job_count").Value(GetPendingJobCount())
        .Item("job_statistics").Value(JobStatistics)
        .Item("estimated_input_statistics").BeginMap()
            .Item("chunk_count").Value(TotalEstimatedInputChunkCount)
            .Item("uncompressed_data_size").Value(TotalEstimatedInputDataSize)
            .Item("compressed_data_size").Value(TotalEstimatedCompressedDataSize)
            .Item("data_weight").Value(TotalEstimatedInputDataWeight)
            .Item("row_count").Value(TotalEstimatedInputRowCount)
            .Item("unavailable_chunk_count").Value(UnavailableInputChunkCount)
        .EndMap()
        .Item("live_preview").BeginMap()
            .Item("output_supported").Value(IsOutputLivePreviewSupported())
            .Item("intermediate_supported").Value(IsIntermediateLivePreviewSupported())
            .Item("stderr_supported").Value(StderrTable.HasValue())
        .EndMap()
        .Item("schedule_job_statistics").BeginMap()
            .Item("count").Value(ScheduleJobStatistics_->Count)
            .Item("duration").Value(ScheduleJobStatistics_->Duration)
            .Item("failed").Value(ScheduleJobStatistics_->Failed)
        .EndMap()
        .DoIf(EstimatedInputDataSizeHistogram_.operator bool(), [=] (TFluentMap fluent) {
            EstimatedInputDataSizeHistogram_->BuildHistogramView();
            fluent
                .Item("estimated_input_data_size_histogram").Value(*EstimatedInputDataSizeHistogram_);
        })
        .DoIf(InputDataSizeHistogram_.operator bool(), [=] (TFluentMap fluent) {
            InputDataSizeHistogram_->BuildHistogramView();
            fluent
                .Item("input_data_size_histogram").Value(*InputDataSizeHistogram_);
        });
}

void TOperationControllerBase::BuildBriefProgress(IYsonConsumer* consumer) const
{
    BuildYsonMapFluently(consumer)
        .Item("jobs").Value(JobCounter);
}

void TOperationControllerBase::BuildAndSaveProgress()
{
    auto progressString = BuildYsonStringFluently()
        .BeginMap()
            .Do(BIND([=] (IYsonConsumer* consumer) {
                WaitFor(
                    BIND(&IOperationController::BuildProgress, MakeStrong(this))
                        .AsyncVia(GetInvoker())
                        .Run(consumer));
            }))
        .EndMap();

    auto briefProgressString = BuildYsonStringFluently()
        .BeginMap()
            .Do(BIND([=] (IYsonConsumer* consumer) {
                WaitFor(
                    BIND(&IOperationController::BuildBriefProgress, MakeStrong(this))
                        .AsyncVia(GetInvoker())
                        .Run(consumer));
            }))
        .EndMap();

    {
        TGuard<TSpinLock> guard(ProgressLock_);
        ProgressString_ = progressString;
        BriefProgressString_ = briefProgressString;
    }
}

TYsonString TOperationControllerBase::GetProgress() const
{
    TGuard<TSpinLock> guard(ProgressLock_);
    return ProgressString_;
}

TYsonString TOperationControllerBase::GetBriefProgress() const
{
    TGuard<TSpinLock> guard(ProgressLock_);
    return BriefProgressString_;
}

TYsonString TOperationControllerBase::BuildJobYson(const TJobId& id, bool outputStatistics) const
{
    TCallback<void(IYsonConsumer*)> attributesBuilder;

    // Case of running job.
    {
        auto joblet = FindJoblet(id);
        if (joblet) {
            attributesBuilder = BIND(
                &TOperationControllerBase::BuildJobAttributes,
                MakeStrong(this),
                joblet,
                EJobState::Running,
                outputStatistics);
        }
    }

    // Case of finished job.
    // NB: Temporaly disabled. We should improve UI to consider completed jobs in orchid.
    //{
    //    auto it = FinishedJobs_.find(id);
    //    if (it != FinishedJobs_.end()) {
    //        const auto& job = it->second;
    //        YCHECK(!attributesBuilder);
    //        attributesBuilder = BIND(&TOperationControllerBase::BuildFinishedJobAttributes, MakeStrong(this), job);
    //    }
    //}

    YCHECK(attributesBuilder);

    return BuildYsonStringFluently()
        .BeginMap()
            .Do(attributesBuilder)
        .EndMap();
}

TYsonString TOperationControllerBase::BuildJobsYson() const
{
    return BuildYsonStringFluently<EYsonType::MapFragment>()
        .DoFor(JobletMap, [&] (TFluentMap fluent, const std::pair<TJobId, TJobletPtr>& pair) {
            const auto& jobId = pair.first;
            const auto& joblet = pair.second;
            if (joblet->StartTime) {
                fluent.Item(ToString(jobId)).BeginMap()
                    .Do([&] (IYsonConsumer* consumer) {
                        BuildJobAttributes(joblet, EJobState::Running, /* outputStatistics */ false, consumer);
                    })
                .EndMap();
            }
        })
        // NB: Temporaly disabled. We should improve UI to consider completed jobs in orchid.
        // .DoFor(FinishedJobs_, [&] (TFluentMap fluent, const std::pair<TJobId, TFinishedJobInfoPtr>& pair) {
        //     const auto& jobId = pair.first;
        //     const auto& job = pair.second;
        //     fluent.Item(ToString(jobId)).BeginMap()
        //         .Do([&] (IYsonConsumer* consumer) {
        //             BuildFinishedJobAttributes(job, fluent);
        //         })
        //     .EndMap();
        // })
        .Finish();
}

TYsonString TOperationControllerBase::BuildSuspiciousJobsYson() const
{
    return BuildYsonStringFluently<EYsonType::MapFragment>()
        .DoFor(
            JobletMap,
            [&] (TFluentMap fluent, const std::pair<TJobId, TJobletPtr>& pair) {
                const auto& joblet = pair.second;
                if (joblet->Suspicious) {
                    fluent.Item(ToString(joblet->JobId))
                        .BeginMap()
                            .Item("operation_id").Value(ToString(OperationId))
                            .Item("type").Value(FormatEnum(joblet->JobType))
                            .Item("brief_statistics").Value(joblet->BriefStatistics)
                            .Item("node").Value(joblet->NodeDescriptor.Address)
                            .Item("last_activity_time").Value(joblet->LastActivityTime)
                        .EndMap();
                }
            })
        .Finish();
}

void TOperationControllerBase::AnalyzeBriefStatistics(
    const TJobletPtr& job,
    TDuration suspiciousInactivityTimeout,
    i64 suspiciousCpuUsageThreshold,
    double suspiciousInputPipeIdleTimeFraction,
    const TErrorOr<TBriefJobStatisticsPtr>& briefStatisticsOrError)
{
    if (!briefStatisticsOrError.IsOK()) {
        if (job->BriefStatistics) {
            // Failures in brief statistics building are normal during job startup,
            // when readers and writers are not built yet. After we successfully built
            // brief statistics once, we shouldn't fail anymore.

            LOG_WARNING(
                briefStatisticsOrError,
                "Failed to build brief job statistics (JobId: %v)",
                job->JobId);
        }

        return;
    }

    const auto& briefStatistics = briefStatisticsOrError.Value();

    bool wasActive = !job->BriefStatistics ||
        CheckJobActivity(
            job->BriefStatistics,
            briefStatistics,
            suspiciousCpuUsageThreshold,
            suspiciousInputPipeIdleTimeFraction);

    job->BriefStatistics = briefStatistics;

    bool wasSuspicious = job->Suspicious;
    job->Suspicious = (!wasActive && job->BriefStatistics->Timestamp - job->LastActivityTime > suspiciousInactivityTimeout);
    if (!wasSuspicious && job->Suspicious) {
        LOG_DEBUG("Found a suspicious job (JobId: %v, LastActivityTime: %v, SuspiciousInactivityTimeout: %v)",
            job->JobId,
            job->LastActivityTime,
            suspiciousInactivityTimeout);
    }

    if (wasActive) {
        job->LastActivityTime = job->BriefStatistics->Timestamp;
    }
}

void TOperationControllerBase::UpdateJobStatistics(const TJobletPtr& joblet, const TJobSummary& jobSummary)
{
    YCHECK(jobSummary.Statistics);

    // NB: There is a copy happening here that can be eliminated.
    auto statistics = *jobSummary.Statistics;
    LOG_TRACE("Job data statistics (JobId: %v, Input: %v, Output: %v)",
        jobSummary.Id,
        GetTotalInputDataStatistics(statistics),
        GetTotalOutputDataStatistics(statistics));

    auto statisticsState = GetStatisticsJobState(joblet, jobSummary.State);
    auto statisticsSuffix = JobHelper.GetStatisticsSuffix(statisticsState, joblet->JobType);
    statistics.AddSuffixToNames(statisticsSuffix);
    JobStatistics.Update(statistics);
}

void TOperationControllerBase::BuildBriefSpec(IYsonConsumer* consumer) const
{
    VERIFY_THREAD_AFFINITY_ANY();

    BuildYsonMapFluently(consumer)
        .DoIf(Spec->Title.HasValue(), [&] (TFluentMap fluent) {
            fluent
                .Item("title").Value(*Spec->Title);
        })
        .Item("input_table_paths").ListLimited(GetInputTablePaths(), 1)
        .Item("output_table_paths").ListLimited(GetOutputTablePaths(), 1);
}

TYsonString TOperationControllerBase::BuildInputPathYson(const TJobletPtr& joblet) const
{
    VERIFY_INVOKER_AFFINITY(CancelableInvoker);

    return BuildInputPaths(
        GetInputTablePaths(),
        joblet->InputStripeList,
        OperationType,
        joblet->JobType);
}

void TOperationControllerBase::BuildJobSplitterInfo(IYsonConsumer* consumer) const
{
    VERIFY_INVOKER_AFFINITY(SuspendableInvoker);
    YCHECK(JobSplitter_);

    JobSplitter_->BuildJobSplitterInfo(consumer);
}

std::unique_ptr<TJobMetricsUpdater> TOperationControllerBase::CreateJobMetricsUpdater() const
{
    return std::make_unique<TJobMetricsUpdater>(Host, OperationId, Config->JobMetricsBatchInterval);
}

std::vector<TOperationControllerBase::TPathWithStage> TOperationControllerBase::GetFilePaths() const
{
    return std::vector<TPathWithStage>();
}

bool TOperationControllerBase::IsRowCountPreserved() const
{
    return false;
}

void TOperationControllerBase::InitUserJobSpecTemplate(
    NScheduler::NProto::TUserJobSpec* jobSpec,
    TUserJobSpecPtr config,
    const std::vector<TUserFile>& files,
    const Stroka& fileAccount)
{
    jobSpec->set_shell_command(config->Command);
    if (config->JobTimeLimit) {
        jobSpec->set_job_time_limit(config->JobTimeLimit.Get().MilliSeconds());
    }
    jobSpec->set_memory_limit(config->MemoryLimit);
    jobSpec->set_include_memory_mapped_files(config->IncludeMemoryMappedFiles);
    jobSpec->set_use_yamr_descriptors(config->UseYamrDescriptors);
    jobSpec->set_check_input_fully_consumed(config->CheckInputFullyConsumed);
    jobSpec->set_max_stderr_size(config->MaxStderrSize);
    jobSpec->set_custom_statistics_count_limit(config->CustomStatisticsCountLimit);
    jobSpec->set_copy_files(config->CopyFiles);
    jobSpec->set_file_account(fileAccount);

    if (config->TmpfsPath && Config->EnableTmpfs) {
        auto tmpfsSize = config->TmpfsSize
            ? *config->TmpfsSize
            : config->MemoryLimit;
        jobSpec->set_tmpfs_size(tmpfsSize);
        jobSpec->set_tmpfs_path(*config->TmpfsPath);
    }

    if (Config->IopsThreshold) {
        jobSpec->set_iops_threshold(*Config->IopsThreshold);
        if (Config->IopsThrottlerLimit) {
            jobSpec->set_iops_throttler_limit(*Config->IopsThrottlerLimit);
        }
    }

    {
        // Set input and output format.
        TFormat inputFormat(EFormatType::Yson);
        TFormat outputFormat(EFormatType::Yson);

        if (config->Format) {
            inputFormat = outputFormat = *config->Format;
        }

        if (config->InputFormat) {
            inputFormat = *config->InputFormat;
        }

        if (config->OutputFormat) {
            outputFormat = *config->OutputFormat;
        }

        jobSpec->set_input_format(ConvertToYsonString(inputFormat).GetData());
        jobSpec->set_output_format(ConvertToYsonString(outputFormat).GetData());
    }

    auto fillEnvironment = [&] (yhash<Stroka, Stroka>& env) {
        for (const auto& pair : env) {
            jobSpec->add_environment(Format("%v=%v", pair.first, pair.second));
        }
    };

    // Global environment.
    fillEnvironment(Config->Environment);

    // Local environment.
    fillEnvironment(config->Environment);

    jobSpec->add_environment(Format("YT_OPERATION_ID=%v", OperationId));

    for (const auto& file : files) {
        auto* descriptor = jobSpec->add_files();
        descriptor->set_file_name(file.FileName);
        ToProto(descriptor->mutable_chunk_specs(), file.ChunkSpecs);

        if (file.Type == EObjectType::Table && file.IsDynamic && file.Schema.IsSorted()) {
            auto dataSource = MakeVersionedDataSource(
                file.GetPath(),
                file.Schema,
                file.Path.GetColumns(),
                file.Path.GetTimestamp().Get(AsyncLastCommittedTimestamp));
            ToProto(descriptor->mutable_data_source(), dataSource);
        } else {
            auto dataSource = file.Type == EObjectType::File
                    ? MakeFileDataSource(file.GetPath())
                    : MakeUnversionedDataSource(file.GetPath(), file.Schema, file.Path.GetColumns());

            ToProto(descriptor->mutable_data_source(), dataSource);
        }

        switch (file.Type) {
            case EObjectType::File:
                descriptor->set_executable(file.Executable);
                break;
            case EObjectType::Table:
                descriptor->set_format(file.Format.GetData());
                break;
            default:
                Y_UNREACHABLE();
        }
    }
}

void TOperationControllerBase::InitUserJobSpec(
    NScheduler::NProto::TUserJobSpec* jobSpec,
    TJobletPtr joblet)
{
    ToProto(jobSpec->mutable_async_scheduler_transaction_id(), AsyncSchedulerTransaction->GetId());

    i64 memoryReserve = joblet->EstimatedResourceUsage.GetUserJobMemory() * joblet->UserJobMemoryReserveFactor;
    // Memory reserve should greater than or equal to tmpfs_size (see YT-5518 for more details).
    // This is ensured by adjusting memory reserve factor in user job config as initialization,
    // but just in case we also limit the actual memory_reserve value here.
    if (jobSpec->has_tmpfs_size()) {
        memoryReserve = std::max(memoryReserve, jobSpec->tmpfs_size());
    }
    jobSpec->set_memory_reserve(memoryReserve);

    jobSpec->add_environment(Format("YT_JOB_INDEX=%v", joblet->JobIndex));
    jobSpec->add_environment(Format("YT_JOB_ID=%v", joblet->JobId));
    if (joblet->StartRowIndex >= 0) {
        jobSpec->add_environment(Format("YT_START_ROW_INDEX=%v", joblet->StartRowIndex));
    }

    if (SecureVault) {
        // NB: These environment variables should be added to user job spec, not to the user job spec template.
        // They may contain sensitive information that should not be persisted with a controller.

        // We add a single variable storing the whole secure vault and all top-level scalar values.
        jobSpec->add_environment(Format("YT_SECURE_VAULT=%v",
            ConvertToYsonString(SecureVault, EYsonFormat::Text)));

        for (const auto& pair : SecureVault->GetChildren()) {
            Stroka value;
            auto node = pair.second;
            if (node->GetType() == ENodeType::Int64) {
                value = ToString(node->GetValue<i64>());
            } else if (node->GetType() == ENodeType::Uint64) {
                value = ToString(node->GetValue<ui64>());
            } else if (node->GetType() == ENodeType::Boolean) {
                value = ToString(node->GetValue<bool>());
            } else if (node->GetType() == ENodeType::Double) {
                value = ToString(node->GetValue<double>());
            } else if (node->GetType() == ENodeType::String) {
                value = node->GetValue<Stroka>();
            } else {
                // We do not export composite values as a separate environment variables.
                continue;
            }
            jobSpec->add_environment(Format("YT_SECURE_VAULT_%v=%v", pair.first, value));
        }
    }

    if (joblet->StderrTableChunkListId) {
        AddStderrOutputSpecs(jobSpec, joblet);
    }
    if (joblet->CoreTableChunkListId) {
        AddCoreOutputSpecs(jobSpec, joblet);
    }
}

void TOperationControllerBase::AddStderrOutputSpecs(
    NScheduler::NProto::TUserJobSpec* jobSpec,
    TJobletPtr joblet)
{
    auto* stderrTableSpec = jobSpec->mutable_stderr_table_spec();
    auto* outputSpec = stderrTableSpec->mutable_output_table_spec();
    outputSpec->set_table_writer_options(ConvertToYsonString(StderrTable->Options).GetData());
    ToProto(outputSpec->mutable_table_schema(), StderrTable->TableUploadOptions.TableSchema);
    ToProto(outputSpec->mutable_chunk_list_id(), joblet->StderrTableChunkListId);

    auto writerConfig = GetStderrTableWriterConfig();
    YCHECK(writerConfig);
    stderrTableSpec->set_blob_table_writer_config(ConvertToYsonString(writerConfig).GetData());
}

void TOperationControllerBase::AddCoreOutputSpecs(
    NScheduler::NProto::TUserJobSpec* jobSpec,
    TJobletPtr joblet)
{
    auto* coreTableSpec = jobSpec->mutable_core_table_spec();
    auto* outputSpec = coreTableSpec->mutable_output_table_spec();
    outputSpec->set_table_writer_options(ConvertToYsonString(CoreTable->Options).GetData());
    ToProto(outputSpec->mutable_table_schema(), CoreTable->TableUploadOptions.TableSchema);
    ToProto(outputSpec->mutable_chunk_list_id(), joblet->CoreTableChunkListId);

    auto writerConfig = GetCoreTableWriterConfig();
    YCHECK(writerConfig);
    coreTableSpec->set_blob_table_writer_config(ConvertToYsonString(writerConfig).GetData());
}

TDataSourceDirectoryPtr TOperationControllerBase::MakeInputDataSources() const
{
    auto dataSourceDirectory = New<TDataSourceDirectory>();
    for (const auto& inputTable : InputTables) {
        auto dataSource = (inputTable.IsDynamic && inputTable.Schema.IsSorted())
            ? MakeVersionedDataSource(
                inputTable.GetPath(),
                inputTable.Schema,
                inputTable.Path.GetColumns(),
                inputTable.Path.GetTimestamp().Get(AsyncLastCommittedTimestamp))
            : MakeUnversionedDataSource(
                inputTable.GetPath(),
                inputTable.Schema,
                inputTable.Path.GetColumns());

        dataSourceDirectory->DataSources().push_back(dataSource);
    }
    return dataSourceDirectory;
}

TDataSourceDirectoryPtr TOperationControllerBase::CreateIntermediateDataSource() const
{
    static const Stroka IntermediatePath("<intermediate>");

    auto dataSourceDirectory = New<TDataSourceDirectory>();
    dataSourceDirectory->DataSources().push_back(MakeUnversionedDataSource(
        IntermediatePath,
        Null,
        Null));

    return dataSourceDirectory;
}

i64 TOperationControllerBase::GetFinalOutputIOMemorySize(TJobIOConfigPtr ioConfig) const
{
    i64 result = 0;
    for (const auto& outputTable : OutputTables) {
        if (outputTable.Options->ErasureCodec == NErasure::ECodec::None) {
            i64 maxBufferSize = std::max(
                ioConfig->TableWriter->MaxRowWeight,
                ioConfig->TableWriter->MaxBufferSize);
            result += GetOutputWindowMemorySize(ioConfig) + maxBufferSize;
        } else {
            auto* codec = NErasure::GetCodec(outputTable.Options->ErasureCodec);
            double replicationFactor = (double) codec->GetTotalPartCount() / codec->GetDataPartCount();
            result += static_cast<i64>(ioConfig->TableWriter->DesiredChunkSize * replicationFactor);
        }
    }
    return result;
}

i64 TOperationControllerBase::GetFinalIOMemorySize(
    TJobIOConfigPtr ioConfig,
    const TChunkStripeStatisticsVector& stripeStatistics) const
{
    i64 result = 0;
    for (const auto& stat : stripeStatistics) {
        result += GetInputIOMemorySize(ioConfig, stat);
    }
    result += GetFinalOutputIOMemorySize(ioConfig);
    return result;
}

void TOperationControllerBase::InitIntermediateOutputConfig(TJobIOConfigPtr config)
{
    // Don't replicate intermediate output.
    config->TableWriter->UploadReplicationFactor = Spec->IntermediateDataReplicationFactor;
    config->TableWriter->MinUploadReplicationFactor = 1;

    // Cache blocks on nodes.
    config->TableWriter->PopulateCache = true;

    // Don't sync intermediate chunks.
    config->TableWriter->SyncOnClose = false;
}

void TOperationControllerBase::InitFinalOutputConfig(TJobIOConfigPtr /* config */)
{ }

NTableClient::TTableReaderOptionsPtr TOperationControllerBase::CreateTableReaderOptions(TJobIOConfigPtr ioConfig)
{
    auto options = New<TTableReaderOptions>();
    options->EnableRowIndex = ioConfig->ControlAttributes->EnableRowIndex;
    options->EnableTableIndex = ioConfig->ControlAttributes->EnableTableIndex;
    options->EnableRangeIndex = ioConfig->ControlAttributes->EnableRangeIndex;
    return options;
}

NTableClient::TTableReaderOptionsPtr TOperationControllerBase::CreateIntermediateTableReaderOptions()
{
    auto options = New<TTableReaderOptions>();
    options->AllowFetchingSeedsFromMaster = true;
    return options;
}

INativeClientPtr TOperationControllerBase::CreateClient()
{
    TClientOptions options;
    options.User = AuthenticatedUser;
    return Host
        ->GetMasterClient()
        ->GetNativeConnection()
        ->CreateNativeClient(options);
}

void TOperationControllerBase::ValidateUserFileCount(TUserJobSpecPtr spec, const Stroka& operation)
{
    if (spec && spec->FilePaths.size() > Config->MaxUserFileCount) {
        THROW_ERROR_EXCEPTION("Too many user files in %v: maximum allowed %v, actual %v",
            operation,
            Config->MaxUserFileCount,
            spec->FilePaths.size());
    }
}

void TOperationControllerBase::GetExecNodesInformation()
{
    auto now = NProfiling::GetCpuInstant();
    if (now < GetExecNodesInformationDeadline_) {
        return;
    }

    ExecNodeCount_ = Host->GetExecNodeCount();
    ExecNodesDescriptors_ = Host->GetExecNodeDescriptors(NScheduler::TSchedulingTagFilter(Spec->SchedulingTagFilter));
    GetExecNodesInformationDeadline_ = now + NProfiling::DurationToCpuDuration(Config->ControllerUpdateExecNodesInformationDelay);
}

int TOperationControllerBase::GetExecNodeCount()
{
    GetExecNodesInformation();
    return ExecNodeCount_;
}

const std::vector<TExecNodeDescriptor>& TOperationControllerBase::GetExecNodeDescriptors()
{
    GetExecNodesInformation();
    return ExecNodesDescriptors_->Descriptors;
}

bool TOperationControllerBase::ShouldSkipSanityCheck()
{
    auto nodeCount = GetExecNodeCount();
    if (nodeCount < Config->SafeOnlineNodeCount) {
        return true;
    }

    if (TInstant::Now() < Host->GetConnectionTime() + Config->SafeSchedulerOnlineTime) {
        return true;
    }

    if (!CachedMaxAvailableExecNodeResources_) {
        return true;
    }

    return false;
}

void TOperationControllerBase::BuildMemoryDigestStatistics(IYsonConsumer* consumer) const
{
    VERIFY_INVOKER_AFFINITY(Invoker);

    BuildYsonMapFluently(consumer)
        .Item("job_proxy_memory_digest")
        .DoMapFor(JobProxyMemoryDigests_, [&] (
                TFluentMap fluent,
                const TMemoryDigestMap::value_type& item)
        {
            BuildYsonMapFluently(fluent)
                .Item(ToString(item.first)).Value(
                    item.second->GetQuantile(Config->JobProxyMemoryReserveQuantile));
        })
        .Item("user_job_memory_digest")
        .DoMapFor(JobProxyMemoryDigests_, [&] (
                TFluentMap fluent,
                const TMemoryDigestMap::value_type& item)
        {
            BuildYsonMapFluently(fluent)
                .Item(ToString(item.first)).Value(
                    item.second->GetQuantile(Config->UserJobMemoryReserveQuantile));
        });
}

void TOperationControllerBase::RegisterUserJobMemoryDigest(EJobType jobType, double memoryReserveFactor)
{
    YCHECK(UserJobMemoryDigests_.find(jobType) == UserJobMemoryDigests_.end());
    auto config = New<TLogDigestConfig>();
    config->LowerBound = memoryReserveFactor;
    config->UpperBound = 1.0;
    config->RelativePrecision = Config->UserJobMemoryDigestPrecision;
    UserJobMemoryDigests_[jobType] = CreateLogDigest(config);
}

IDigest* TOperationControllerBase::GetUserJobMemoryDigest(EJobType jobType)
{
    auto iter = UserJobMemoryDigests_.find(jobType);
    YCHECK(iter != UserJobMemoryDigests_.end());
    return iter->second.get();
}

const IDigest* TOperationControllerBase::GetUserJobMemoryDigest(EJobType jobType) const
{
    auto iter = UserJobMemoryDigests_.find(jobType);
    YCHECK(iter != UserJobMemoryDigests_.end());
    return iter->second.get();
}

void TOperationControllerBase::RegisterJobProxyMemoryDigest(EJobType jobType, const TLogDigestConfigPtr& config)
{
    YCHECK(JobProxyMemoryDigests_.find(jobType) == JobProxyMemoryDigests_.end());
    JobProxyMemoryDigests_[jobType] = CreateLogDigest(config);
}

void TOperationControllerBase::InferSchemaFromInput(const TKeyColumns& keyColumns)
{
    // We infer schema only for operations with one output table.
    YCHECK(OutputTables.size() == 1);
    YCHECK(InputTables.size() >= 1);

    OutputTables[0].TableUploadOptions.SchemaMode = InputTables[0].SchemaMode;
    for (const auto& table : InputTables) {
        if (table.SchemaMode != OutputTables[0].TableUploadOptions.SchemaMode) {
            THROW_ERROR_EXCEPTION("Cannot infer output schema from input, tables have different schema modes");
        }
    }

    if (OutputTables[0].TableUploadOptions.SchemaMode == ETableSchemaMode::Weak) {
        OutputTables[0].TableUploadOptions.TableSchema = TTableSchema::FromKeyColumns(keyColumns);
    } else {
        auto schema = InputTables[0].Schema
            .ToStrippedColumnAttributes()
            .ToCanonical();

        for (const auto& table : InputTables) {
            if (table.Schema.ToStrippedColumnAttributes().ToCanonical() != schema) {
                THROW_ERROR_EXCEPTION("Cannot infer output schema from input in strong schema mode, tables have incompatible schemas");
            }
        }

        OutputTables[0].TableUploadOptions.TableSchema = InputTables[0].Schema
            .ToSorted(keyColumns)
            .ToSortedStrippedColumnAttributes()
            .ToCanonical();
    }

    FilterOutputSchemaByInputColumnSelectors();
}

void TOperationControllerBase::InferSchemaFromInputOrdered()
{
    // We infer schema only for operations with one output table.
    YCHECK(OutputTables.size() == 1);
    YCHECK(InputTables.size() >= 1);

    auto& outputUploadOptions = OutputTables[0].TableUploadOptions;

    if (InputTables.size() == 1 && outputUploadOptions.UpdateMode == EUpdateMode::Overwrite) {
        // If only only one input table given, we inherit the whole schema including column attributes.
        outputUploadOptions.SchemaMode = InputTables[0].SchemaMode;
        outputUploadOptions.TableSchema = InputTables[0].Schema;
        FilterOutputSchemaByInputColumnSelectors();
        return;
    }

    InferSchemaFromInput();
}

void TOperationControllerBase::FilterOutputSchemaByInputColumnSelectors()
{
    yhash_set<Stroka> columns;
    for (const auto& table : InputTables) {
        if (auto selectors = table.Path.GetColumns()) {
            for (const auto& column : *selectors) {
                columns.insert(column);
            }
        } else {
            return;
        }
    }

    OutputTables[0].TableUploadOptions.TableSchema =
        OutputTables[0].TableUploadOptions.TableSchema.Filter(columns);
}

void TOperationControllerBase::ValidateOutputSchemaOrdered() const
{
    YCHECK(OutputTables.size() == 1);
    YCHECK(InputTables.size() >= 1);

    if (InputTables.size() > 1 && OutputTables[0].TableUploadOptions.TableSchema.IsSorted()) {
        THROW_ERROR_EXCEPTION("Cannot generate sorted output for ordered operation with multiple input tables")
            << TErrorAttribute("output_schema", OutputTables[0].TableUploadOptions.TableSchema);
    }
}

void TOperationControllerBase::ValidateOutputSchemaCompatibility(bool ignoreSortOrder) const
{
    YCHECK(OutputTables.size() == 1);

    for (const auto& inputTable : InputTables) {
        if (inputTable.SchemaMode == ETableSchemaMode::Strong) {
            ValidateTableSchemaCompatibility(
                inputTable.Schema.Filter(inputTable.Path.GetColumns()),
                OutputTables[0].TableUploadOptions.TableSchema,
                ignoreSortOrder)
                .ThrowOnError();
        }
    }
}

TJobSplitterConfigPtr TOperationControllerBase::GetJobSplitterConfig() const
{
    return nullptr;
}

IDigest* TOperationControllerBase::GetJobProxyMemoryDigest(EJobType jobType)
{
    auto iter = JobProxyMemoryDigests_.find(jobType);
    YCHECK(iter != JobProxyMemoryDigests_.end());
    return iter->second.get();
}

const IDigest* TOperationControllerBase::GetJobProxyMemoryDigest(EJobType jobType) const
{
    auto iter = JobProxyMemoryDigests_.find(jobType);
    YCHECK(iter != JobProxyMemoryDigests_.end());
    return iter->second.get();
}

void TOperationControllerBase::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, TotalEstimatedInputChunkCount);
    Persist(context, TotalEstimatedInputDataSize);
    Persist(context, TotalEstimatedInputRowCount);
    Persist(context, TotalEstimatedCompressedDataSize);
    Persist(context, TotalEstimatedInputDataWeight);

    Persist(context, UnavailableInputChunkCount);

    Persist(context, JobCounter);

    Persist(context, InputNodeDirectory);

    Persist(context, InputTables);

    Persist(context, OutputTables);

    Persist(context, StderrTable);

    Persist(context, CoreTable);

    Persist(context, IntermediateTable);

    Persist(context, Files);

    Persist(context, Tasks);

    Persist(context, TaskGroups);

    Persist(context, InputChunkMap);

    Persist(context, IntermediateOutputCellTag);

    Persist(context, CellTagToOutputRequiredChunkList);

    Persist(context, CachedPendingJobCount);

    Persist(context, CachedNeededResources);

    Persist(context, ChunkOriginMap);

    Persist(context, JobletMap);

    Persist(context, JobIndexGenerator);

    Persist(context, JobStatistics);

    Persist(context, ScheduleJobStatistics_);

    Persist(context, RowCountLimitTableIndex);
    Persist(context, RowCountLimit);

    Persist<
        TMapSerializer<
            TDefaultSerializer,
            TDefaultSerializer,
            TUnsortedTag
        >
    >(context, JobProxyMemoryDigests_);

    Persist<
        TMapSerializer<
            TDefaultSerializer,
            TDefaultSerializer,
            TUnsortedTag
        >
    >(context, UserJobMemoryDigests_);

    Persist(context, EstimatedInputDataSizeHistogram_);
    Persist(context, InputDataSizeHistogram_);

    Persist(context, CurrentInputDataSliceTag_);

    if (context.IsLoad()) {
        for (const auto& task : Tasks) {
            task->Initialize();
        }
        InitUpdatingTables();
    }

    Persist(context, StderrCount_);
    Persist(context, JobNodeCount_);
    Persist(context, FinishedJobs_);
}

TCodicilGuard TOperationControllerBase::MakeCodicilGuard() const
{
    return TCodicilGuard(CodicilData_);
}

TBlobTableWriterConfigPtr TOperationControllerBase::GetStderrTableWriterConfig() const
{
    return nullptr;
}

TNullable<TRichYPath> TOperationControllerBase::GetStderrTablePath() const
{
    return Null;
}

TBlobTableWriterConfigPtr TOperationControllerBase::GetCoreTableWriterConfig() const
{
    return nullptr;
}

TNullable<TRichYPath> TOperationControllerBase::GetCoreTablePath() const
{
    return Null;
}

////////////////////////////////////////////////////////////////////

//! Ensures that operation controllers are being destroyed in a
//! dedicated invoker.
class TOperationControllerWrapper
    : public IOperationController
{
public:
    TOperationControllerWrapper(
        const TOperationId& id,
        IOperationControllerPtr underlying,
        IInvokerPtr dtorInvoker)
        : Id_(id)
        , Underlying_(std::move(underlying))
        , DtorInvoker_(std::move(dtorInvoker))
    { }

    virtual ~TOperationControllerWrapper()
    {
        DtorInvoker_->Invoke(BIND([underlying = std::move(Underlying_), id = Id_] () mutable {
            auto Logger = OperationLogger;
            Logger.AddTag("OperationId: %v", id);
            NProfiling::TScopedTimer timer;
            LOG_INFO("Started destroying operation controller");
            underlying.Reset();
            LOG_INFO("Finished destroying operation controller (Elapsed: %v)",
                timer.GetElapsed());
        }));
    }

    virtual void Initialize() override
    {
        Underlying_->Initialize();
    }

    virtual TOperationControllerInitializeResult GetInitializeResult() const override
    {
        return Underlying_->GetInitializeResult();
    }

    virtual void InitializeReviving(TControllerTransactionsPtr controllerTransactions) override
    {
        Underlying_->InitializeReviving(controllerTransactions);
    }

    virtual void Prepare() override
    {
        Underlying_->Prepare();
    }

    virtual void Materialize() override
    {
        Underlying_->Materialize();
    }

    virtual void Commit() override
    {
        Underlying_->Commit();
    }

    virtual void SaveSnapshot(TOutputStream* stream) override
    {
        Underlying_->SaveSnapshot(stream);
    }

    virtual void Revive() override
    {
        Underlying_->Revive();
    }

    virtual void Abort() override
    {
        Underlying_->Abort();
    }

    virtual void Forget() override
    {
        Underlying_->Forget();
    }

    virtual void OnTransactionAborted(const TTransactionId& transactionId) override
    {
        Underlying_->OnTransactionAborted(transactionId);
    }

    virtual std::vector<ITransactionPtr> GetTransactions() override
    {
        return Underlying_->GetTransactions();
    }

    virtual void Complete() override
    {
        Underlying_->Complete();
    }

    virtual TCancelableContextPtr GetCancelableContext() const override
    {
        return Underlying_->GetCancelableContext();
    }

    virtual IInvokerPtr GetCancelableInvoker() const override
    {
        return Underlying_->GetCancelableInvoker();
    }

    virtual IInvokerPtr GetInvoker() const override
    {
        return Underlying_->GetInvoker();
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

    virtual int GetTotalJobCount() const override
    {
        return Underlying_->GetTotalJobCount();
    }

    virtual bool IsForgotten() const override
    {
        return Underlying_->IsForgotten();
    }

    virtual bool IsRevivedFromSnapshot() const override
    {
        return Underlying_->IsRevivedFromSnapshot();
    }

    virtual TJobResources GetNeededResources() const override
    {
        return Underlying_->GetNeededResources();
    }

    virtual std::vector<TJobResources> GetMinNeededJobResources() const
    {
        return Underlying_->GetMinNeededJobResources();
    }

    virtual void OnJobStarted(const TJobId& jobId, TInstant startTime) override
    {
        Underlying_->OnJobStarted(jobId, startTime);
    }

    virtual void OnJobCompleted(std::unique_ptr<TCompletedJobSummary> jobSummary) override
    {
        Underlying_->OnJobCompleted(std::move(jobSummary));
    }

    virtual void OnJobFailed(std::unique_ptr<TFailedJobSummary> jobSummary) override
    {
        Underlying_->OnJobFailed(std::move(jobSummary));
    }

    virtual void OnJobAborted(std::unique_ptr<TAbortedJobSummary> jobSummary) override
    {
        Underlying_->OnJobAborted(std::move(jobSummary));
    }

    virtual void OnJobRunning(std::unique_ptr<TRunningJobSummary> jobSummary) override
    {
        Underlying_->OnJobRunning(std::move(jobSummary));
    }

    virtual TScheduleJobResultPtr ScheduleJob(
        ISchedulingContextPtr context,
        const TJobResources& jobLimits) override
    {
        return Underlying_->ScheduleJob(std::move(context), jobLimits);
    }

    virtual void UpdateConfig(TSchedulerConfigPtr config) override
    {
        Underlying_->UpdateConfig(std::move(config));
    }

    virtual bool HasProgress() const override
    {
        return Underlying_->HasProgress();
    }

    virtual bool HasJobSplitterInfo() const override
    {
        return Underlying_->HasJobSplitterInfo();
    }

    virtual void BuildOperationAttributes(NYson::IYsonConsumer* consumer) const override
    {
        Underlying_->BuildOperationAttributes(consumer);
    }

    virtual void BuildProgress(IYsonConsumer* consumer) const override
    {
        Underlying_->BuildProgress(consumer);
    }

    virtual void BuildBriefProgress(IYsonConsumer* consumer) const override
    {
        Underlying_->BuildBriefProgress(consumer);
    }

    virtual Stroka GetLoggingProgress() const override
    {
        return Underlying_->GetLoggingProgress();
    }

    virtual void BuildMemoryDigestStatistics(IYsonConsumer* consumer) const override
    {
        Underlying_->BuildMemoryDigestStatistics(consumer);
    }

    virtual void BuildJobSplitterInfo(IYsonConsumer* consumer) const override
    {
        Underlying_->BuildJobSplitterInfo(consumer);
    }

    virtual TYsonString GetProgress() const override
    {
        return Underlying_->GetProgress();
    }

    virtual TYsonString GetBriefProgress() const override
    {
        return Underlying_->GetBriefProgress();
    }

    virtual TYsonString BuildJobYson(const TJobId& jobId, bool outputStatistics) const override
    {
        return Underlying_->BuildJobYson(jobId, outputStatistics);
    }

    virtual TYsonString BuildJobsYson() const override
    {
        return Underlying_->BuildJobsYson();
    }

    virtual TYsonString BuildSuspiciousJobsYson() const override
    {
        return Underlying_->BuildSuspiciousJobsYson();
    }

private:
    const TOperationId Id_;
    const IOperationControllerPtr Underlying_;
    const IInvokerPtr DtorInvoker_;
};

////////////////////////////////////////////////////////////////////

IOperationControllerPtr CreateControllerForOperation(
    IOperationHost* host,
    TOperation* operation)
{
    auto config = host->GetConfig();

    IOperationControllerPtr controller;
    switch (operation->GetType()) {
        case EOperationType::Map:
            controller = CreateMapController(config, host, operation);
            break;
        case EOperationType::Merge:
            controller = CreateMergeController(config, host, operation);
            break;
        case EOperationType::Erase:
            controller = CreateEraseController(config, host, operation);
            break;
        case EOperationType::Sort:
            controller = CreateSortController(config, host, operation);
            break;
        case EOperationType::Reduce: {
            auto legacySpec = ParseOperationSpec<TOperationWithLegacyControllerSpec>(operation->GetSpec());
            if (legacySpec->UseLegacyController) {
                controller = CreateLegacyReduceController(config, host, operation);
            } else {
                controller = CreateSortedReduceController(config, host, operation);
            }
            break;
        }
        case EOperationType::JoinReduce: {
            auto legacySpec = ParseOperationSpec<TOperationWithLegacyControllerSpec>(operation->GetSpec());
            if (legacySpec->UseLegacyController) {
                controller = CreateLegacyJoinReduceController(config, host, operation);
            } else {
                controller = CreateJoinReduceController(config, host, operation);
            }
            break;
        }
        case EOperationType::MapReduce:
            controller = CreateMapReduceController(config, host, operation);
            break;
        case EOperationType::RemoteCopy:
            controller = CreateRemoteCopyController(config, host, operation);
            break;
        default:
            Y_UNREACHABLE();
    }

    return New<TOperationControllerWrapper>(
        operation->GetId(),
        controller,
        controller->GetInvoker());
}

////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT
