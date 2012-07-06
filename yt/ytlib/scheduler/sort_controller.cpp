#include "stdafx.h"
#include "map_controller.h"
#include "private.h"
#include "operation_controller_detail.h"
#include "chunk_pool.h"
#include "chunk_list_pool.h"
#include "samples_fetcher.h"

#include <ytlib/misc/string.h>
#include <ytlib/ytree/fluent.h>
#include <ytlib/table_client/schema.h>
#include <ytlib/table_client/key.h>
#include <ytlib/table_client/chunk_meta_extensions.h>
#include <ytlib/chunk_holder/chunk_meta_extensions.h>
#include <ytlib/job_proxy/config.h>
#include <ytlib/transaction_client/transaction.h>

#include <cmath>

namespace NYT {
namespace NScheduler {

using namespace NYTree;
using namespace NChunkServer;
using namespace NTableClient;
using namespace NJobProxy;
using namespace NObjectServer;
using namespace NScheduler::NProto;
using namespace NChunkHolder::NProto;
using namespace NTableClient::NProto;

////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger(OperationLogger);
static NProfiling::TProfiler Profiler("/operations/sort");

////////////////////////////////////////////////////////////////////

class TSortController
    : public TOperationControllerBase
{
public:
    TSortController(
        TSchedulerConfigPtr config,
        TSortOperationSpecPtr spec,
        IOperationHost* host,
        TOperation* operation)
        : TOperationControllerBase(config, host, operation)
        , Config(config)
        , Spec(spec)
        , CompletedPartitionCount(0)
        , MaxSortJobCount(0)
        , RunningSortJobCount(0)
        , CompletedSortJobCount(0)
        , TotalSortedMergeJobCount(0)
        , RunningSortedMergeJobCount(0)
        , CompletedSortedMergeJobCount(0)
        , TotalUnorderedMergeJobCount(0)
        , RunningUnorderedMergeJobCount(0)
        , CompletedUnorderedMergeJobCount(0)
        , SamplesFetcher(New<TSamplesFetcher>(
            Config,
            Spec,
            Host->GetBackgroundInvoker(),
            Operation->GetOperationId()))
        , PartitionTask(New<TPartitionTask>(this))
    { }

private:
    TSchedulerConfigPtr Config;
    TSortOperationSpecPtr Spec;

    // Counters.
    int CompletedPartitionCount;
    TProgressCounter PartitionJobCounter;
    // Sort job counters.
    int MaxSortJobCount;
    int RunningSortJobCount;
    int CompletedSortJobCount;
    TProgressCounter SortWeightCounter;
    // Sorted merge job counters.
    int TotalSortedMergeJobCount;
    int RunningSortedMergeJobCount;
    int CompletedSortedMergeJobCount;
    // Unordered merge job counters.
    int TotalUnorderedMergeJobCount;
    int RunningUnorderedMergeJobCount;
    int CompletedUnorderedMergeJobCount;

    // Forward declarations.
    class TPartitionTask;
    typedef TIntrusivePtr<TPartitionTask> TPartitionTaskPtr;

    class TSortTask;
    typedef TIntrusivePtr<TSortTask> TSortTaskPtr;

    class TSortedMergeTask;
    typedef TIntrusivePtr<TSortedMergeTask> TSortedMergeTaskPtr;

    class TUnorderedMergeTask;
    typedef TIntrusivePtr<TUnorderedMergeTask> TUnorderedMergeTaskPtr;

    // Samples and partitions.
    struct TPartition
        : public TIntrinsicRefCounted
    {
        TPartition(TSortController* controller, int index)
            : Index(index)
            , Completed(false)
            , SortedMergeNeeded(false)
            , Megalomaniac(false)
            , Empty(true)
            , SortTask(New<TSortTask>(controller, this))
            , SortedMergeTask(New<TSortedMergeTask>(controller, this))
            , UnorderedMergeTask(New<TUnorderedMergeTask>(controller, this))
        { }

        //! Sequential index (zero based).
        int Index;

        //! Is partition completed?
        bool Completed;

        //! Do we need to run merge tasks for this partition?
        bool SortedMergeNeeded;

        //! Does the partition consist of rows with the same key?
        bool Megalomaniac;

        //! Is there any data here?
        bool Empty;

        TSortTaskPtr SortTask;
        TSortedMergeTaskPtr SortedMergeTask;
        TUnorderedMergeTaskPtr UnorderedMergeTask;
    };

    typedef TIntrusivePtr<TPartition> TPartitionPtr;

    TSamplesFetcherPtr SamplesFetcher;
    std::vector<const NTableClient::NProto::TKey*> SortedSamples;

    //! |PartitionCount - 1| separating keys.
    std::vector<NTableClient::NProto::TKey> PartitionKeys;
    
    //! List of all partitions.
    std::vector<TPartitionPtr> Partitions;

    //! Templates for starting new jobs.
    TJobSpec PartitionJobSpecTemplate;
    TJobSpec SortJobSpecTemplate;
    TJobSpec SortedMergeJobSpecTemplate;
    TJobSpec UnorderedMergeJobSpecTemplate;

    
    // Partition task.

    class TPartitionTask
        : public TTask
    {
    public:
        explicit TPartitionTask(TSortController* controller)
            : TTask(controller)
            , Controller(controller)
        {
            ChunkPool = CreateUnorderedChunkPool();
        }

        virtual Stroka GetId() const OVERRIDE
        {
            return "Partition";
        }

        virtual int GetPendingJobCount() const OVERRIDE
        {
            return
                IsCompleted() 
                ? 0
                : Controller->PartitionJobCounter.GetPending();
        }

        virtual TDuration GetMaxLocalityDelay() const OVERRIDE
        {
            // TODO(babenko): make customizable
            return TDuration::Seconds(5);
        }

    private:
        TSortController* Controller;

        virtual int GetChunkListCountPerJob() const OVERRIDE
        {
            return 1;
        }

        virtual TNullable<i64> GetJobWeightThreshold() const OVERRIDE
        {
            return GetJobWeightThresholdGeneric(
                GetPendingJobCount(),
                WeightCounter().GetPending());
        }

        virtual TJobSpec GetJobSpec(TJobInProgressPtr jip) OVERRIDE
        {
            auto jobSpec = Controller->PartitionJobSpecTemplate;
            AddSequentialInputSpec(&jobSpec, jip);
            AddTabularOutputSpec(&jobSpec, jip, 0);
            return jobSpec;
        }

        virtual void OnJobStarted(TJobInProgressPtr jip) OVERRIDE
        {
            Controller->PartitionJobCounter.Start(1);

            TTask::OnJobStarted(jip);
        }

        virtual void OnJobCompleted(TJobInProgressPtr jip) OVERRIDE
        {
            TTask::OnJobCompleted(jip);

            Controller->PartitionJobCounter.Completed(1);

            auto* resultExt = jip->Job->Result().MutableExtension(TPartitionJobResultExt::partition_job_result_ext);
            FOREACH (auto& partitionChunk, *resultExt->mutable_chunks()) {
                // We're keeping chunk information received from partition jobs to populate sort pools.
                // TPartitionsExt is, however, quite heavy.
                // Deserialize it and then drop its protobuf copy immediately.
                auto partitionsExt = GetProtoExtension<NTableClient::NProto::TPartitionsExt>(partitionChunk.extensions());
                RemoveProtoExtension<NTableClient::NProto::TPartitionsExt>(partitionChunk.mutable_extensions());

                YCHECK(partitionsExt.partitions_size() == Controller->Partitions.size());
                LOG_TRACE("Partition attributes are:");
                for (int index = 0; index < partitionsExt.partitions_size(); ++index) {
                    const auto& partitionAttributes = partitionsExt.partitions(index);
                    LOG_TRACE("Partition[%d] = {%s}", index, ~partitionAttributes.DebugString());
                    if (partitionAttributes.data_weight() > 0) {
                        auto stripe = New<TChunkStripe>(
                            partitionChunk,
                            partitionAttributes.data_weight(),
                            partitionAttributes.row_count());
                        auto partition = Controller->Partitions[index];
                        partition->Empty = false;
                        auto destinationTask = partition->Megalomaniac
                            ? TTaskPtr(partition->UnorderedMergeTask)
                            : TTaskPtr(partition->SortTask);
                        destinationTask->AddStripe(stripe);
                    }
                }
            }
        }

        virtual void OnJobFailed(TJobInProgressPtr jip) OVERRIDE
        {
            Controller->PartitionJobCounter.Failed(1);

            TTask::OnJobFailed(jip);
        }

        virtual void OnTaskCompleted() OVERRIDE
        {
            TTask::OnTaskCompleted();

            // Compute jobs totals.
            FOREACH (auto partition, Controller->Partitions) {
                Controller->TotalSortedMergeJobCount += partition->SortedMergeTask->GetPendingJobCount();
                Controller->TotalUnorderedMergeJobCount += partition->UnorderedMergeTask->GetPendingJobCount();
            }

            // Kick-start sort and unordered merge tasks.
            // Mark empty partitions are completed.
            FOREACH (auto partition, Controller->Partitions) {
                if (partition->Empty) {
                    LOG_DEBUG("Partition is empty (Partition: %d)", partition->Index);
                    Controller->OnPartitionCompeted(partition);
                } else {
                    auto taskToKick = partition->Megalomaniac
                        ? TTaskPtr(partition->UnorderedMergeTask)
                        : TTaskPtr(partition->SortTask);
                    Controller->AddTaskPendingHint(taskToKick);
                }
            }
        }
    };

    TPartitionTaskPtr PartitionTask;


    // Sort task.

    class TSortTask
        : public TTask
    {
    public:
        TSortTask(TSortController* controller, TPartition* partition)
            : TTask(controller)
            , Controller(controller)
            , Partition(partition)
        {
            ChunkPool = CreateUnorderedChunkPool();
        }

        virtual Stroka GetId() const OVERRIDE
        {
            return Sprintf("Sort(%d)", Partition->Index);
        }


        virtual int GetPendingJobCount() const OVERRIDE
        {
            i64 weight = ChunkPool->WeightCounter().GetPending();
            i64 weightPerJob = Controller->Spec->MaxWeightPerSortJob;
            double fractionalJobCount = (double) weight / weightPerJob;
            return
                Controller->PartitionTask->IsCompleted()
                ? static_cast<int>(ceil(fractionalJobCount))
                : static_cast<int>(floor(fractionalJobCount));
        }

        virtual TDuration GetMaxLocalityDelay() const OVERRIDE
        {
            // TODO(babenko): make customizable
            // If no primary node is chosen yet then start the job immediately.
            return AddressToOutputLocality.empty() ? TDuration::Zero() : TDuration::Seconds(30);
        }

        virtual i64 GetLocality(const Stroka& address) const OVERRIDE
        {
            // To make subsequent merges local,
            // sort locality is assigned based on outputs (including those that are still running)
            // rather than on on inputs (they are scattered anyway).
            if (AddressToOutputLocality.empty()) {
                // No primary node is chosen yet, an arbitrary one will do.
                // Return some magic number.
                return Controller->Spec->MaxWeightPerSortJob;
            } else {
                auto it = AddressToOutputLocality.find(address);
                return it == AddressToOutputLocality.end() ? 0 : it->second;
            }
        }

        bool IsCompleted() const
        {
            return
                Controller->PartitionTask->IsCompleted() &&
                TTask::IsCompleted();
        }

    private:
        TSortController* Controller;
        TPartition* Partition;

        yhash_map<Stroka, i64> AddressToOutputLocality;

        bool CheckSortedMergeNeeded()
        {
            if (Partition->SortedMergeNeeded) {
                return true;
            }

            // Check if this sort job only handles a fraction of the partition.
            // Two cases are possible:
            // 1) Partition task is still running and thus may enqueue
            // additional data to be sorted.
            // 2) The sort pool hasn't been exhausted by the current job.
            bool mergeNeeded =
                !Controller->PartitionTask->IsCompleted() ||
                IsPending();

            if (mergeNeeded) {
                LOG_DEBUG("Partition needs sorted merge (Partition: %d)", Partition->Index);
                Partition->SortedMergeNeeded = true;
            }

            return mergeNeeded;
        }

        virtual int GetChunkListCountPerJob() const OVERRIDE
        {
            return 1;
        }

        virtual TNullable<i64> GetJobWeightThreshold() const OVERRIDE
        {
            return Controller->Spec->MaxWeightPerSortJob;
        }

        virtual TJobSpec GetJobSpec(TJobInProgressPtr jip) OVERRIDE
        {
            auto jobSpec = Controller->SortJobSpecTemplate;

            AddSequentialInputSpec(&jobSpec, jip);
            AddTabularOutputSpec(&jobSpec, jip, 0);

            {
                // Use output replication to sort jobs in small partitions since their chunks go directly to the output.
                // Don't use replication for sort jobs in large partitions since their chunks will be merged.
                auto ioConfig = Controller->PrepareJobIOConfig(Controller->Config->SortJobIO, !CheckSortedMergeNeeded());
                jobSpec.set_io_config(ConvertToYsonString(ioConfig).Data());
            }

            {
                auto* jobSpecExt = jobSpec.MutableExtension(TSortJobSpecExt::sort_job_spec_ext);
                if (Controller->Partitions.size() > 1) {
                    auto* inputSpec = jobSpec.mutable_input_specs(0);
                    FOREACH (auto& chunk, *inputSpec->mutable_chunks()) {
                        chunk.set_partition_tag(Partition->Index);
                    }
                }
            }

            return jobSpec;
        }

        virtual void OnJobStarted(TJobInProgressPtr jip) OVERRIDE
        {
            YCHECK(!Partition->Megalomaniac);

            ++Controller->RunningSortJobCount;
            Controller->SortWeightCounter.Start(jip->PoolResult->TotalChunkWeight);

            // Increment output locality.
            // Also notify the controller that we're willing to use this node
            // for all subsequent jobs.
            auto address = jip->Job->GetNode()->GetAddress();
            AddressToOutputLocality[address] += jip->PoolResult->TotalChunkWeight;
            Controller->AddTaskLocalityHint(this, address);

            TTask::OnJobStarted(jip);
        }

        virtual void OnJobCompleted(TJobInProgressPtr jip) OVERRIDE
        {
            TTask::OnJobCompleted(jip);

            --Controller->RunningSortJobCount;
            ++Controller->CompletedSortJobCount;
            Controller->SortWeightCounter.Completed(jip->PoolResult->TotalChunkWeight);

            if (!Partition->SortedMergeNeeded) {
                Controller->RegisterOutputChunkTree(Partition, jip->ChunkListIds[0]);
                Controller->OnPartitionCompeted(Partition);
                return;
            } 

            // Sort outputs in large partitions are queued for further merge.

            // Construct a stripe consisting of sorted chunks.
            const auto& resultExt = jip->Job->Result().GetExtension(TSortJobResultExt::sort_job_result_ext);
            auto stripe = New<TChunkStripe>();
            FOREACH (const auto& chunk, resultExt.chunks()) {
                stripe->AddChunk(chunk);
            }

            // Put the stripe into the pool.
            Partition->SortedMergeTask->AddStripe(stripe);
        }

        virtual void OnJobFailed(TJobInProgressPtr jip) OVERRIDE
        {
            --Controller->RunningSortJobCount;
            Controller->SortWeightCounter.Failed(jip->PoolResult->TotalChunkWeight);

            // Decrement output locality and purge zeros.
            auto address = jip->Job->GetNode()->GetAddress();
            if ((AddressToOutputLocality[address] -= jip->PoolResult->TotalChunkWeight) == 0) {
                YCHECK(AddressToOutputLocality.erase(address) == 1);
            }

            TTask::OnJobFailed(jip);
        }

        virtual void OnTaskCompleted() OVERRIDE
        {
            TTask::OnTaskCompleted();

            // Kick-start the corresponding merge task.
            if (Partition->SortedMergeNeeded) {
                Controller->AddTaskPendingHint(Partition->SortedMergeTask);
            }
        }

        virtual void AddInputLocalityHint(TChunkStripePtr stripe) OVERRIDE
        {
            UNUSED(stripe);
            // See #GetLocality.
        }
    };


    // Sorted merge task.

    class TSortedMergeTask
        : public TTask
    {
    public:
        TSortedMergeTask(TSortController* controller, TPartition* partition)
            : TTask(controller)
            , Controller(controller)
            , Partition(partition)
        {
            ChunkPool = CreateAtomicChunkPool();
        }

        virtual Stroka GetId() const OVERRIDE
        {
            return Sprintf("SortedMerge(%d)", Partition->Index);
        }

        virtual int GetPendingJobCount() const OVERRIDE
        {
            return
                !Partition->Megalomaniac &&
                Partition->SortedMergeNeeded &&
                Partition->SortTask->IsCompleted() &&
                IsPending()
                ? 1 : 0;
        }

        virtual TDuration GetMaxLocalityDelay() const OVERRIDE
        {
            // TODO(babenko): make configurable
            return TDuration::Seconds(30);
        }

    private:
        TSortController* Controller;
        TPartition* Partition;

        virtual int GetChunkListCountPerJob() const OVERRIDE
        {
            return 1;
        }

        virtual TNullable<i64> GetJobWeightThreshold() const OVERRIDE
        {
            return Null;
        }

        virtual TJobSpec GetJobSpec(TJobInProgressPtr jip) OVERRIDE
        {
            auto jobSpec = Controller->SortedMergeJobSpecTemplate;
            AddParallelInputSpec(&jobSpec, jip);
            AddTabularOutputSpec(&jobSpec, jip, 0);
            return jobSpec;
        }

        virtual void OnJobStarted(TJobInProgressPtr jip) OVERRIDE
        {
            YCHECK(!Partition->Megalomaniac);

            ++Controller->RunningSortedMergeJobCount;

            TTask::OnJobStarted(jip);
        }

        virtual void OnJobCompleted(TJobInProgressPtr jip) OVERRIDE
        {
            TTask::OnJobCompleted(jip);

            --Controller->RunningSortedMergeJobCount;
            ++Controller->CompletedSortedMergeJobCount;

            Controller->RegisterOutputChunkTree(Partition, jip->ChunkListIds[0]);

            YCHECK(ChunkPool->IsCompleted());
            Controller->OnPartitionCompeted(Partition);
        }

        virtual void OnJobFailed(TJobInProgressPtr jip) OVERRIDE
        {
            --Controller->RunningSortedMergeJobCount;

            TTask::OnJobFailed(jip);
        }
    };

    // Unorderded merge task (for megalomaniacs).
    
    class TUnorderedMergeTask
        : public TTask
    {
    public:
        TUnorderedMergeTask(TSortController* controller, TPartition* partition)
            : TTask(controller)
            , Controller(controller)
            , Partition(partition)
        {
            ChunkPool = CreateUnorderedChunkPool();
        }

        virtual Stroka GetId() const OVERRIDE
        {
            return Sprintf("UnorderedMerge(%d)", Partition->Index);
        }

        virtual int GetPendingJobCount() const OVERRIDE
        {
            if (!Partition->Megalomaniac ||
                !Controller->PartitionTask->IsCompleted())
            {
                return 0;
            }

            i64 weight = ChunkPool->WeightCounter().GetPending();
            i64 weightPerJob = Controller->Spec->MaxWeightPerUnorderedMergeJob;
            return static_cast<int>(ceil((double) weight / weightPerJob));
        }

        virtual TDuration GetMaxLocalityDelay() const OVERRIDE
        {
            // Unordered merge will fetch all partitions so the locality is not an issue here.
            return TDuration::Zero();
        }

    private:
        TSortController* Controller;
        TPartition* Partition;

        virtual int GetChunkListCountPerJob() const OVERRIDE
        {
            return 1;
        }

        virtual TNullable<i64> GetJobWeightThreshold() const OVERRIDE
        {
            return Controller->Spec->MaxWeightPerUnorderedMergeJob;
        }

        virtual TJobSpec GetJobSpec(TJobInProgressPtr jip) OVERRIDE
        {
            auto jobSpec = Controller->UnorderedMergeJobSpecTemplate;
            AddSequentialInputSpec(&jobSpec, jip);
            AddTabularOutputSpec(&jobSpec, jip, 0);

            if (Controller->Partitions.size() > 1) {
                auto* inputSpec = jobSpec.mutable_input_specs(0);
                FOREACH (auto& chunk, *inputSpec->mutable_chunks()) {
                    chunk.set_partition_tag(Partition->Index);
                }
            }

            return jobSpec;
        }

        virtual void OnJobStarted(TJobInProgressPtr jip) OVERRIDE
        {
            YCHECK(Partition->Megalomaniac);

            ++Controller->RunningUnorderedMergeJobCount;

            TTask::OnJobStarted(jip);
        }

        virtual void OnJobCompleted(TJobInProgressPtr jip) OVERRIDE
        {
            TTask::OnJobCompleted(jip);

            --Controller->RunningUnorderedMergeJobCount;
            ++Controller->CompletedUnorderedMergeJobCount;

            Controller->RegisterOutputChunkTree(Partition, jip->ChunkListIds[0]);

            if (ChunkPool->IsCompleted()) {
                Controller->OnPartitionCompeted(Partition);
            }
        }

        virtual void OnJobFailed(TJobInProgressPtr jip) OVERRIDE
        {
            --Controller->RunningUnorderedMergeJobCount;

            TTask::OnJobFailed(jip);
        }
    };


    // Init/finish.

    void RegisterOutputChunkTree(TPartitionPtr partition, const TChunkTreeId& chunkTreeId)
    {
        TOperationControllerBase::RegisterOutputChunkTree(chunkTreeId, partition->Index, 0);
    }

    void OnPartitionCompeted(TPartitionPtr partition)
    {
        YCHECK(!partition->Completed);
        partition->Completed = true;

        ++CompletedPartitionCount;

        LOG_INFO("Partition completed (Partition: %d)", partition->Index);
    }

    virtual void OnOperationCompleted() OVERRIDE
    {
        YCHECK(CompletedPartitionCount == Partitions.size());
        TOperationControllerBase::OnOperationCompleted();
    }


    // Job scheduling and outcome handling for sort phase.

    // Custom bits of preparation pipeline.

    virtual std::vector<TYPath> GetInputTablePaths() OVERRIDE
    {
        return Spec->InputTablePaths;
    }

    virtual std::vector<TYPath> GetOutputTablePaths() OVERRIDE
    {
        std::vector<TYPath> result;
        result.push_back(Spec->OutputTablePath);
        return result;
    }

    virtual TAsyncPipeline<void>::TPtr CustomizePreparationPipeline(TAsyncPipeline<void>::TPtr pipeline) OVERRIDE
    {
        return pipeline
            ->Add(BIND(&TSortController::RequestSamples, MakeStrong(this)))
            ->Add(BIND(&TSortController::OnSamplesReceived, MakeStrong(this)));
    }

    TFuture< TValueOrError<void> > RequestSamples()
    {
        PROFILE_TIMING ("/input_processing_time") {
            LOG_INFO("Processing inputs");

            // Prepare the fetcher.
            int chunkCount = 0;
            FOREACH (const auto& table, InputTables) {
                FOREACH (const auto& chunk, table.FetchResponse->chunks()) {
                    SamplesFetcher->AddChunk(chunk);
                    ++chunkCount;
                }
            }

            // Check for empty inputs.
            if (chunkCount == 0) {
                LOG_INFO("Empty input");
                OnOperationCompleted();
                return MakeFuture();
            }

            LOG_INFO("Inputs processed (Weight: %" PRId64 ", ChunkCount: %" PRId64 ")",
                PartitionTask->WeightCounter().GetTotal(),
                PartitionTask->ChunkCounter().GetTotal());

            return SamplesFetcher->Run();
        }
    }

    virtual void OnCustomInputsRecieved(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp) OVERRIDE
    {
        UNUSED(batchRsp);

        CheckOutputTablesEmpty();
        SetOutputTablesSorted(Spec->KeyColumns);
    }

    void SortSamples()
    {
        const auto& samples = SamplesFetcher->GetSamples();
        int sampleCount = static_cast<int>(samples.size());
        LOG_INFO("Sorting %d samples", sampleCount);

        SortedSamples.reserve(sampleCount);
        FOREACH (const auto& sample, samples) {
            SortedSamples.push_back(&sample);
        }

        std::sort(SortedSamples.begin(), SortedSamples.end(), 
            [] (const NTableClient::NProto::TKey* lhs, const NTableClient::NProto::TKey* rhs) {
                return CompareKeys(*lhs, *rhs) < 0;
            }
        );
    }

    void BuildPartitions()
    {
        FOREACH (const auto& table, InputTables) {
            FOREACH (const auto& chunk, table.FetchResponse->chunks()) {
                auto miscExt = GetProtoExtension<TMiscExt>(chunk.extensions());
                i64 weight = miscExt.data_weight();
                SortWeightCounter.Increment(weight);
            }
        }

        // Use partition count provided by user, if given.
        // Otherwise use size estimates.
        int partitionCount = Spec->PartitionCount
            ? Spec->PartitionCount.Get()
            : static_cast<int>(ceil((double) SortWeightCounter.GetTotal() / Spec->MaxWeightPerSortJob));

        // Don't create more partitions than we have samples.
        partitionCount = std::min(partitionCount, static_cast<int>(SortedSamples.size()) + 1);

        // Don't create more partitions than allowed by the global config.
        partitionCount = std::min(partitionCount, Config->MaxPartitionCount);

        YCHECK(partitionCount > 0);

        if (partitionCount == 1) {
            BuildSinglePartition();
        } else {
            BuildMulitplePartitions(partitionCount);
        }
    }

    void BuildSinglePartition()
    {
        // Create a single partition.
        Partitions.resize(1);
        auto partition = Partitions[0] = New<TPartition>(this, 0);

        // Put all input chunks into this unique partition.
        int chunkCount = 0;
        FOREACH (const auto& table, InputTables) {
            FOREACH (auto& chunk, *table.FetchResponse->mutable_chunks()) {
                auto stripe = New<TChunkStripe>(chunk);
                partition->SortTask->AddStripe(stripe);
                ++chunkCount;
            }
        }

        // A pretty accurate estimate.
        MaxSortJobCount = GetJobCount(
            SortWeightCounter.GetTotal(),
            Spec->MaxWeightPerSortJob,
            Spec->SortJobCount,
            chunkCount);

        // Can be zero but better be pessimists.
        TotalSortedMergeJobCount = 1;

        LOG_INFO("Sorting without partitioning");

        // Kick-start the sort task.
        AddTaskPendingHint(partition->SortTask);
    }

    void AddPartition(const NTableClient::NProto::TKey& key)
    {
        int index = static_cast<int>(Partitions.size());
        LOG_DEBUG("Partition %d has starting key %s",
            index,
            ~ToString(TNonOwningKey::FromProto(key)));

        YCHECK(PartitionKeys.empty() || CompareKeys(PartitionKeys.back(), key) < 0);

        PartitionKeys.push_back(key);
        Partitions.push_back(New<TPartition>(this, index));
    }

    void BuildMulitplePartitions(int partitionCount)
    {
        LOG_DEBUG("Building partition keys");

        auto GetSampleKey = [&](int sampleIndex) {
            return SortedSamples[(sampleIndex + 1) * (SortedSamples.size() - 1) / partitionCount];
        };

        // Construct the leftmost partition.
        Partitions.push_back(New<TPartition>(this, 0));

        // Invariant:
        //   lastPartition = Partitions.back()
        //   lastKey = PartitionKeys.back()
        //   lastPartition receives keys in [lastKey, ...)
        //   
        // Initially PartitionKeys is empty so lastKey is assumed to be -inf.

        // Take partition keys evenly.
        int sampleIndex = 0;
        while (sampleIndex < partitionCount - 1) {
            auto* sampleKey = GetSampleKey(sampleIndex);
            // Check for same keys.
            if (PartitionKeys.empty() || CompareKeys(*sampleKey, PartitionKeys.back()) != 0) {
                AddPartition(*sampleKey);
                ++sampleIndex;
            } else {
                // Skip same keys.
                int skippedCount = 0;
                while (sampleIndex < partitionCount - 1 &&
                       CompareKeys(*GetSampleKey(sampleIndex), PartitionKeys.back()) == 0)
                {
                    ++sampleIndex;
                    ++skippedCount;
                }

                auto lastPartition = Partitions.back();
                LOG_DEBUG("Partition %d is a megalomaniac, skipped %d samples",
                    lastPartition->Index,
                    skippedCount);

                lastPartition->Megalomaniac = true;
                YCHECK(skippedCount >= 1);
                
                auto successorKey = GetSuccessorKey(*sampleKey);
                AddPartition(successorKey);
            }
        }

        // Populate the partition pool.
        FOREACH (const auto& table, InputTables) {
            FOREACH (const auto& chunk, table.FetchResponse->chunks()) {
                auto stripe = New<TChunkStripe>(chunk);
                PartitionTask->AddStripe(stripe);
            }
        }

        // Init counters.
        PartitionJobCounter.Set(GetJobCount(
            PartitionTask->WeightCounter().GetTotal(),
            Config->PartitionJobIO->ChunkSequenceWriter->DesiredChunkSize,
            Spec->PartitionJobCount,
            PartitionTask->ChunkCounter().GetTotal()));

        // Some upper bound.
        MaxSortJobCount =
            GetJobCount(
                PartitionTask->WeightCounter().GetTotal(),
                Spec->MaxWeightPerSortJob,
                Null,
                std::numeric_limits<int>::max()) +
            Partitions.size();

        LOG_INFO("Sorting with partitioning (PartitionCount: %d, PartitionJobCount: %" PRId64 ")",
            static_cast<int>(Partitions.size()),
            PartitionJobCounter.GetTotal());

        // Kick-start the partition task.
        AddTaskPendingHint(PartitionTask);
    }

    void OnSamplesReceived()
    {
        PROFILE_TIMING ("/samples_processing_time") {
            SortSamples();
            BuildPartitions();
           
            // Allocate some initial chunk lists.
            // TODO(babenko): reserve chunk lists for unordered merge jobs
            ChunkListPool->Allocate(
                PartitionJobCounter.GetTotal() +
                MaxSortJobCount +
                Partitions.size() + // for sorted merge jobs
                Config->SpareChunkListCount);

            InitJobSpecTemplates();
        }
    }


    // Progress reporting.

    virtual void LogProgress() OVERRIDE
    {
        LOG_DEBUG("Progress: "
            "Jobs = {R: %d, C: %d, P: %d, F: %d}, "
            "Partitions = {T: %d, C: %d}, "
            "PartitionJobs = {%s}, "
            "PartitionChunks = {%s}, "
            "PartitionWeight = {%s}, "
            "SortJobs = {M: %d, R: %d, C: %d}, "
            "SortWeight = {%s}, "
            "SortedMergeJobs = {T: %d, R: %d, C: %d}, "
            "UnorderedMergeJobs = {T: %d, R: %d, C: %d}",
            // Jobs
            RunningJobCount,
            CompletedJobCount,
            GetPendingJobCount(),
            FailedJobCount,
            // Partitions
            static_cast<int>(Partitions.size()),
            CompletedPartitionCount,
            // PartitionJobs
            ~ToString(PartitionJobCounter),
            ~ToString(PartitionTask->ChunkCounter()),
            ~ToString(PartitionTask->WeightCounter()),
            // SortJobs
            MaxSortJobCount,
            RunningSortJobCount,
            CompletedSortJobCount,
            ~ToString(SortWeightCounter),
            // SortedMergeJobs
            TotalSortedMergeJobCount,
            RunningSortedMergeJobCount,
            CompletedSortedMergeJobCount,
            // UnorderedMergeJobs
            TotalUnorderedMergeJobCount,
            RunningUnorderedMergeJobCount,
            CompletedUnorderedMergeJobCount);
    }

    virtual void DoGetProgress(IYsonConsumer* consumer) OVERRIDE
    {
        BuildYsonMapFluently(consumer)
            .Item("partitions").BeginMap()
                .Item("total").Scalar(Partitions.size())
                .Item("completed").Scalar(CompletedPartitionCount)
            .EndMap()
            .Item("partition_jobs").Do(BIND(&TProgressCounter::ToYson, &PartitionJobCounter))
            .Item("partition_chunks").Do(BIND(&TProgressCounter::ToYson, &PartitionTask->ChunkCounter()))
            .Item("partition_weight").Do(BIND(&TProgressCounter::ToYson, &PartitionTask->WeightCounter()))
            .Item("sort_jobs").BeginMap()
                .Item("max").Scalar(MaxSortJobCount)
                .Item("running").Scalar(RunningSortJobCount)
                .Item("completed").Scalar(CompletedSortJobCount)
            .EndMap()
            .Item("sort_weight").Do(BIND(&TProgressCounter::ToYson, &SortWeightCounter))
            .Item("sorted_merge_jobs").BeginMap()
                .Item("total").Scalar(TotalSortedMergeJobCount)
                .Item("running").Scalar(RunningSortedMergeJobCount)
                .Item("completed").Scalar(CompletedSortedMergeJobCount)
            .EndMap()
            .Item("unordered_merge_jobs").BeginMap()
                .Item("total").Scalar(TotalUnorderedMergeJobCount)
                .Item("running").Scalar(RunningUnorderedMergeJobCount)
                .Item("completed").Scalar(CompletedUnorderedMergeJobCount)
            .EndMap();
    }


    // Unsorted helpers.

    TJobIOConfigPtr PrepareJobIOConfig(TJobIOConfigPtr config, bool replicateOutput)
    {
        if (replicateOutput) {
            return config;
        } else {
            auto newConfig = CloneConfigurable(config);
            newConfig->ChunkSequenceWriter->ReplicationFactor = 1;
            newConfig->ChunkSequenceWriter->UploadReplicationFactor = 1;
            return newConfig;
        }
    }

    void InitJobSpecTemplates()
    {
        {
            PartitionJobSpecTemplate.set_type(EJobType::Partition);
            *PartitionJobSpecTemplate.mutable_output_transaction_id() = OutputTransaction->GetId().ToProto();

            auto* specExt = PartitionJobSpecTemplate.MutableExtension(TPartitionJobSpecExt::partition_job_spec_ext);
            FOREACH (const auto& key, PartitionKeys) {
                *specExt->add_partition_keys() = key;
            }
            ToProto(specExt->mutable_key_columns(), Spec->KeyColumns);

            // Don't replicate partition chunks.
            PartitionJobSpecTemplate.set_io_config(ConvertToYsonString(
                PrepareJobIOConfig(Config->PartitionJobIO, false)).Data());
        }
        {
            SortJobSpecTemplate.set_type(Partitions.size() == 1 ? EJobType::SimpleSort : EJobType::PartitionSort);
            *SortJobSpecTemplate.mutable_output_transaction_id() = OutputTransaction->GetId().ToProto();

            auto* specExt = SortJobSpecTemplate.MutableExtension(TSortJobSpecExt::sort_job_spec_ext);
            ToProto(specExt->mutable_key_columns(), Spec->KeyColumns);

            // Can't fill io_config right away: some sort jobs need output replication
            // while others don't. Leave this customization to #TSortTask::GetJobSpec.
        }
        {
            SortedMergeJobSpecTemplate.set_type(EJobType::SortedMerge);
            *SortedMergeJobSpecTemplate.mutable_output_transaction_id() = OutputTransaction->GetId().ToProto();

            auto* specExt = SortedMergeJobSpecTemplate.MutableExtension(TMergeJobSpecExt::merge_job_spec_ext);
            ToProto(specExt->mutable_key_columns(), Spec->KeyColumns);

            SortedMergeJobSpecTemplate.set_io_config(ConvertToYsonString(
                PrepareJobIOConfig(Config->MergeJobIO, true)).Data());
        }
        {
            UnorderedMergeJobSpecTemplate.set_type(EJobType::UnorderedMerge);
            *UnorderedMergeJobSpecTemplate.mutable_output_transaction_id() = OutputTransaction->GetId().ToProto();

            auto* specExt = UnorderedMergeJobSpecTemplate.MutableExtension(TMergeJobSpecExt::merge_job_spec_ext);
            ToProto(specExt->mutable_key_columns(), Spec->KeyColumns);

            UnorderedMergeJobSpecTemplate.set_io_config(ConvertToYsonString(
                PrepareJobIOConfig(Config->MergeJobIO, true)).Data());
        }
    }
};

IOperationControllerPtr CreateSortController(
    TSchedulerConfigPtr config,
    IOperationHost* host,
    TOperation* operation)
{
    auto spec = ParseOperationSpec<TSortOperationSpec>(operation);
    return New<TSortController>(config, spec, host, operation);
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

