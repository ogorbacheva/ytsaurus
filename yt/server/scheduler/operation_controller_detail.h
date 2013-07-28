#pragma once

#include "private.h"
#include "operation_controller.h"
#include "chunk_pool.h"
#include "chunk_list_pool.h"
#include "job_resources.h"
#include "statistics.h"
#include "serialization_context.h"

#include <ytlib/misc/thread_affinity.h>
#include <ytlib/misc/nullable.h>
#include <ytlib/misc/id_generator.h>
#include <ytlib/misc/periodic_invoker.h>

#include <ytlib/logging/tagged_logger.h>

#include <ytlib/actions/cancelable_context.h>

#include <ytlib/chunk_client/chunk_owner_ypath_proxy.h>

#include <ytlib/table_client/table_ypath_proxy.h>
#include <ytlib/table_client/config.h>

#include <ytlib/file_client/file_ypath_proxy.h>

#include <ytlib/cypress_client/public.h>

#include <ytlib/ytree/ypath_client.h>
#include <ytlib/ytree/yson_string.h>

#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_client/chunk_service_proxy.h>

#include <ytlib/node_tracker_client/public.h>
#include <ytlib/node_tracker_client/helpers.h>

#include <server/chunk_server/public.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////

class TOperationControllerBase
    : public IOperationController
    , public NPhoenix::IPersistent
    , public NPhoenix::TFactoryTag<NPhoenix::TNullFactory>
{
public:
    TOperationControllerBase(
        TSchedulerConfigPtr config,
        TOperationSpecBasePtr spec,
        IOperationHost* host,
        TOperation* operation);

    virtual void Initialize() override;
    virtual TFuture<TError> Prepare() override;
    virtual void SaveSnapshot(TOutputStream* output) override;
    virtual TFuture<TError> Revive() override;
    virtual TFuture<TError> Commit() override;

    virtual void OnJobRunning(TJobPtr job, const NJobTrackerClient::NProto::TJobStatus& status) override;
    virtual void OnJobCompleted(TJobPtr job) override;
    virtual void OnJobFailed(TJobPtr job) override;
    virtual void OnJobAborted(TJobPtr job) override;

    virtual void Abort() override;

    virtual TJobPtr ScheduleJob(
        ISchedulingContext* context,
        const NNodeTrackerClient::NProto::TNodeResources& jobLimits) override;

    virtual TCancelableContextPtr GetCancelableContext() override;
    virtual IInvokerPtr GetCancelableControlInvoker() override;
    virtual IInvokerPtr GetCancelableBackgroundInvoker() override;

    virtual int GetPendingJobCount() override;
    virtual int GetTotalJobCount() override;
    virtual NNodeTrackerClient::NProto::TNodeResources GetNeededResources() override;

    virtual void BuildProgressYson(NYson::IYsonConsumer* consumer) override;
    virtual void BuildResultYson(NYson::IYsonConsumer* consumer) override;

    virtual void Persist(TPersistenceContext& context) override;

protected:
    // Forward declarations.
    class TTask;
    typedef TIntrusivePtr<TTask> TTaskPtr;

    struct TTaskGroup;
    typedef TIntrusivePtr<TTaskGroup> TTaskGroupPtr;

    struct TJoblet;
    typedef TIntrusivePtr<TJoblet> TJobletPtr;

    struct TCompletedJob;
    typedef TIntrusivePtr<TCompletedJob> TCompleteJobPtr;


    TSchedulerConfigPtr Config;
    IOperationHost* Host;
    TOperation* Operation;

    NRpc::IChannelPtr AuthenticatedMasterChannel;
    mutable NLog::TTaggedLogger Logger;

    TCancelableContextPtr CancelableContext;
    IInvokerPtr CancelableControlInvoker;
    IInvokerPtr CancelableBackgroundInvoker;


    //! Remains |true| as long as the operation can schedule new jobs.
    bool Running;


    // Totals.
    int TotalInputChunkCount;
    i64 TotalInputDataSize;
    i64 TotalInputRowCount;
    i64 TotalInputValueCount;

    int TotalIntermeidateChunkCount;
    int TotalIntermediateDataSize;
    int TotalIntermediateRowCount;

    int TotalOutputChunkCount;
    int TotalOutputDataSize;
    int TotalOutputRowCount;

    int UnavailableInputChunkCount;

    // Job counters.
    TProgressCounter JobCounter;

    // Job statistics.
    TTotalJobStatistics CompletedJobStatistics;
    TTotalJobStatistics FailedJobStatistics;
    TTotalJobStatistics AbortedJobStatistics;

    // Maps node ids seen in fetch responses to node descriptors.
    NNodeTrackerClient::TNodeDirectoryPtr NodeDirectory;


    struct TUserTableBase
    {
        NYPath::TRichYPath Path;
        NObjectClient::TObjectId ObjectId;

        void Persist(TPersistenceContext& context);
    };


    struct TLivePreviewTableBase
    {
        // Live preview table id.
        NCypressClient::TNodeId LivePreviewTableId;

        // Chunk list for appending live preview results.
        NChunkClient::TChunkListId LivePreviewChunkListId;

        void Persist(TPersistenceContext& context);
    };

    struct TInputTable
        : public TUserTableBase
    {
        TInputTable()
            : ComplementFetch(false)
        { }

        NChunkClient::NProto::TRspFetch FetchResponse;
        bool ComplementFetch;
        TNullable< std::vector<Stroka> > KeyColumns;

        void Persist(TPersistenceContext& context);
    };

    std::vector<TInputTable> InputTables;


    struct TEndpoint
    {
        NChunkClient::NProto::TKey Key;
        bool Left;
        int ChunkTreeKey;

        void Persist(TPersistenceContext& context);

    };

    struct TOutputTable
        : public TUserTableBase
        , public TLivePreviewTableBase
    {
        TOutputTable()
            : Clear(false)
            , Overwrite(false)
            , LockMode(NCypressClient::ELockMode::Shared)
            , Options(New<NTableClient::TTableWriterOptions>())
        { }

        bool Clear;
        bool Overwrite;
        NCypressClient::ELockMode LockMode;
        NTableClient::TTableWriterOptionsPtr Options;

        // Chunk list for appending the output.
        NChunkClient::TChunkListId OutputChunkListId;

        //! Chunk trees comprising the output (the order matters).
        //! Keys are used when the output is sorted (e.g. in sort operations).
        //! Trees are sorted w.r.t. key and appended to #OutputChunkListId.
        std::multimap<int, NChunkServer::TChunkTreeId> OutputChunkTreeIds;

        std::vector<TEndpoint> Endpoints;

        void Persist(TPersistenceContext& context);

    };

    std::vector<TOutputTable> OutputTables;


    struct TIntermediateTable
        : public TLivePreviewTableBase
    {
        void Persist(TPersistenceContext& context);
    };

    TIntermediateTable IntermediateTable;


    //! Describes which part of the operation needs a particular file.
    //! Values must be contiguous.
    DECLARE_ENUM(EOperationStage,
        (Map)
        (Reduce)
    );

    struct TUserFileBase
    {
        NYPath::TRichYPath Path;
        EOperationStage Stage;
        Stroka FileName;

        void Persist(TPersistenceContext& context);

    };

    struct TRegularUserFile
        : public TUserFileBase
    {
        NChunkClient::NProto::TRspFetch FetchResponse;
        bool Executable;

        void Persist(TPersistenceContext& context);

    };

    std::vector<TRegularUserFile> RegularFiles;


    struct TUserTableFile
        : public TUserFileBase
    {
        NChunkClient::NProto::TRspFetch FetchResponse;
        NYTree::TYsonString Format;

        void Persist(TPersistenceContext& context);

    };

    std::vector<TUserTableFile> TableFiles;


    struct TJoblet
        : public TIntrinsicRefCounted
    {
        //! For serialization only.
        TJoblet()
            : JobIndex(-1)
            , StartRowIndex(-1)
            , OutputCookie(-1)
        { }

        explicit TJoblet(TTaskPtr task, int jobIndex)
            : Task(task)
            , JobIndex(jobIndex)
            , StartRowIndex(-1)
            , OutputCookie(IChunkPoolOutput::NullCookie)
        { }

        TTaskPtr Task;
        int JobIndex;
        i64 StartRowIndex;

        TJobPtr Job;
        TChunkStripeListPtr InputStripeList;
        IChunkPoolOutput::TCookie OutputCookie;

        //! All chunk lists allocated for this job.
        /*!
         *  For jobs with intermediate output this list typically contains one element.
         *  For jobs with final output this list typically contains one element per each output table.
         */
        std::vector<NChunkClient::TChunkListId> ChunkListIds;

        void Persist(TPersistenceContext& context);

    };

    struct TCompletedJob
        : public TIntrinsicRefCounted
    {
        //! For persistence only.
        TCompletedJob()
            : IsLost(false)
            , DestinationPool(nullptr)
        { }

        TCompletedJob(
            const TJobId& jobId,
            TTaskPtr sourceTask,
            IChunkPoolOutput::TCookie outputCookie,
            IChunkPoolInput* destinationPool,
            IChunkPoolInput::TCookie inputCookie,
            const Stroka& address)
            : IsLost(false)
            , JobId(jobId)
            , SourceTask(std::move(sourceTask))
            , OutputCookie(outputCookie)
            , DestinationPool(destinationPool)
            , InputCookie(inputCookie)
            , Address(address)
        { }

        bool IsLost;

        TJobId JobId;

        TTaskPtr SourceTask;
        IChunkPoolOutput::TCookie OutputCookie;

        IChunkPoolInput* DestinationPool;
        IChunkPoolInput::TCookie InputCookie;

        Stroka Address;

        void Persist(TPersistenceContext& context);

    };

    class TTask
        : public TRefCounted
        , public NPhoenix::IPersistent
    {
    public:
        //! For persistence only.
        TTask();
        explicit TTask(TOperationControllerBase* controller);

        void Initialize();

        virtual Stroka GetId() const = 0;
        virtual TTaskGroupPtr GetGroup() const = 0;

        virtual int GetPendingJobCount() const;
        int GetPendingJobCountDelta();

        int GetTotalJobCount() const;
        int GetTotalJobCountDelta();

        virtual NNodeTrackerClient::NProto::TNodeResources GetTotalNeededResources() const;
        NNodeTrackerClient::NProto::TNodeResources GetTotalNeededResourcesDelta();

        virtual int GetChunkListCountPerJob() const = 0;

        virtual TDuration GetLocalityTimeout() const = 0;
        virtual i64 GetLocality(const Stroka& address) const;
        virtual bool HasInputLocality();

        const NNodeTrackerClient::NProto::TNodeResources& GetMinNeededResources() const;
        virtual NNodeTrackerClient::NProto::TNodeResources GetNeededResources(TJobletPtr joblet) const;

        DEFINE_BYVAL_RW_PROPERTY(TNullable<TInstant>, DelayedTime);

        void AddInput(TChunkStripePtr stripe);
        void AddInput(const std::vector<TChunkStripePtr>& stripes);
        void FinishInput();

        void CheckCompleted();

        TJobPtr ScheduleJob(ISchedulingContext* context, const NNodeTrackerClient::NProto::TNodeResources& jobLimits);

        virtual void OnJobCompleted(TJobletPtr joblet);
        virtual void OnJobFailed(TJobletPtr joblet);
        virtual void OnJobAborted(TJobletPtr joblet);
        virtual void OnJobLost(TCompleteJobPtr completedJob);

        // First checks against a given node, then against all nodes if needed.
        void CheckResourceDemandSanity(
            TExecNodePtr node,
            const NNodeTrackerClient::NProto::TNodeResources& neededResources);

        // Checks against all available nodes.
        void CheckResourceDemandSanity(
            const NNodeTrackerClient::NProto::TNodeResources& neededResources);

        void DoCheckResourceDemandSanity(const NNodeTrackerClient::NProto::TNodeResources& neededResources);

        bool IsPending() const;
        bool IsCompleted() const;

        i64 GetTotalDataSize() const;
        i64 GetCompletedDataSize() const;
        i64 GetPendingDataSize() const;

        virtual IChunkPoolInput* GetChunkPoolInput() const = 0;
        virtual IChunkPoolOutput* GetChunkPoolOutput() const = 0;

        virtual void Persist(TPersistenceContext& context) override;

    private:
        TOperationControllerBase* Controller;

        int CachedPendingJobCount;
        int CachedTotalJobCount;

        NNodeTrackerClient::NProto::TNodeResources CachedTotalNeededResources;
        mutable TNullable<NNodeTrackerClient::NProto::TNodeResources> CachedMinNeededResources;

        TInstant LastDemandSanityCheckTime;
        bool CompletedFired;

        //! For each lost job currently being replayed, maps output cookie to corresponding input cookie.
        yhash_map<IChunkPoolOutput::TCookie, IChunkPoolInput::TCookie> LostJobCookieMap;

    protected:
        NLog::TTaggedLogger Logger;

        virtual NNodeTrackerClient::NProto::TNodeResources GetMinNeededResourcesHeavy() const = 0;

        virtual void OnTaskCompleted();

        virtual EJobType GetJobType() const = 0;
        virtual void PrepareJoblet(TJobletPtr joblet);
        virtual void BuildJobSpec(TJobletPtr joblet, NJobTrackerClient::NProto::TJobSpec* jobSpec) = 0;

        virtual void OnJobStarted(TJobletPtr joblet);

        void AddPendingHint();
        void AddLocalityHint(const Stroka& address);

        DECLARE_ENUM(EJobReinstallReason,
            (Failed)
            (Aborted)
        );

        void ReinstallJob(TJobletPtr joblet, EJobReinstallReason reason);

        void AddSequentialInputSpec(
            NJobTrackerClient::NProto::TJobSpec* jobSpec,
            TJobletPtr joblet,
            bool enableTableIndex = false);
        void AddParallelInputSpec(
            NJobTrackerClient::NProto::TJobSpec* jobSpec,
            TJobletPtr joblet,
            bool enableTableIndex = false);
        static void AddChunksToInputSpec(
            NNodeTrackerClient::TNodeDirectoryBuilder* directoryBuilder,
            NScheduler::NProto::TTableInputSpec* inputSpec,
            TChunkStripePtr stripe,
            TNullable<int> partitionTag,
            bool enableTableIndex);

        void AddFinalOutputSpecs(NJobTrackerClient::NProto::TJobSpec* jobSpec, TJobletPtr joblet);
        void AddIntermediateOutputSpec(NJobTrackerClient::NProto::TJobSpec* jobSpec, TJobletPtr joblet);

        static void UpdateInputSpecTotals(
            NJobTrackerClient::NProto::TJobSpec* jobSpec,
            TJobletPtr joblet);

        void RegisterIntermediate(TJobletPtr joblet, TChunkStripePtr stripe, TTaskPtr destinationTask);
        void RegisterIntermediate(TJobletPtr joblet, TChunkStripePtr stripe, IChunkPoolInput* destinationPool);

        static TChunkStripePtr BuildIntermediateChunkStripe(
            google::protobuf::RepeatedPtrField<NChunkClient::NProto::TChunkSpec>* chunkSpecs);

        void RegisterOutput(TJobletPtr joblet, int key);

    };

    //! All tasks declared by calling #RegisterTask, mostly for debugging purposes.
    std::vector<TTaskPtr> Tasks;


    //! Groups provide means:
    //! - to prioritize tasks
    //! - to skip a vast number of tasks whose resource requirements cannot be met
    struct TTaskGroup
        : public TIntrinsicRefCounted
    {
        //! No task from this group is considered for scheduling unless this requirement is met.
        NNodeTrackerClient::NProto::TNodeResources MinNeededResources;

        //! All non-local tasks.
        yhash_set<TTaskPtr> NonLocalTasks;

        //! Non-local tasks that may possibly be ready (but a delayed check is still needed)
        //! keyed by min memory demand (as reported by TTask::GetMinNeededResources).
        std::multimap<i64, TTaskPtr> CandidateTasks;

        //! Non-local tasks keyed by deadline.
        std::multimap<TInstant, TTaskPtr> DelayedTasks;

        //! Local tasks keyed by address.
        yhash_map<Stroka, yhash_set<TTaskPtr>> LocalTasks;


        void Persist(TPersistenceContext& context);

    };

    //! All task groups declared by calling #RegisterTaskGroup, in the order of decreasing priority.
    std::vector<TTaskGroupPtr> TaskGroups;

    void RegisterTask(TTaskPtr task);
    void RegisterTaskGroup(TTaskGroupPtr group);

    void OnTaskUpdated(TTaskPtr task);

    virtual void CustomizeJoblet(TJobletPtr joblet);
    virtual void CustomizeJobSpec(TJobletPtr joblet, NJobTrackerClient::NProto::TJobSpec* jobSpec);

    void DoAddTaskLocalityHint(TTaskPtr task, const Stroka& address);
    void AddTaskLocalityHint(TTaskPtr task, const Stroka& address);
    void AddTaskLocalityHint(TTaskPtr task, TChunkStripePtr stripe);
    void AddTaskPendingHint(TTaskPtr task);
    void ResetTaskLocalityDelays();

    void MoveTaskToCandidates(TTaskPtr task, std::multimap<i64, TTaskPtr>& candidateTasks);

    bool CheckJobLimits(TExecNodePtr node, TTaskPtr task, const NNodeTrackerClient::NProto::TNodeResources& jobLimits);

    TJobPtr DoScheduleJob(ISchedulingContext* context, const NNodeTrackerClient::NProto::TNodeResources& jobLimits);
    TJobPtr DoScheduleLocalJob(ISchedulingContext* context, const NNodeTrackerClient::NProto::TNodeResources& jobLimits);
    TJobPtr DoScheduleNonLocalJob(ISchedulingContext* context, const NNodeTrackerClient::NProto::TNodeResources& jobLimits);

    void OnJobStarted(TJobPtr job);

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);
    DECLARE_THREAD_AFFINITY_SLOT(BackgroundThread);


    // Jobs in progress management.
    void RegisterJoblet(TJobletPtr joblet);
    TJobletPtr GetJoblet(TJobPtr job);
    void RemoveJoblet(TJobPtr job);


    // Initialization.
    virtual void DoInitialize();   


    // Preparation.
    TError DoPrepare();
    void GetObjectIds();
    void ValidateInputTypes();
    void RequestInputs();
    void CreateLivePreviewTables();
    void PrepareLivePreviewTablesForUpdate();
    void CollectTotals();
    virtual void CustomPrepare();
    void AddAllTaskPendingHints();
    void InitChunkListPool();
    void InitInputChunkScratcher();
    void SuspendUnavailableInputStripes();


    // Completion.
    TError DoCommit();
    void CommitResults();


    // Revival.
    void DoReviveFromSnapshot();
    void ReinstallLivePreview();
    void AbortAllJoblets();


    void DoSaveSnapshot(TOutputStream* output);
    void DoLoadSnapshot();


    //! Called to extract input table paths from the spec.
    virtual std::vector<NYPath::TRichYPath> GetInputTablePaths() const = 0;

    //! Called to extract output table paths from the spec.
    virtual std::vector<NYPath::TRichYPath> GetOutputTablePaths() const = 0;

    typedef std::pair<NYPath::TRichYPath, EOperationStage> TPathWithStage;

    //! Called to extract file paths from the spec.
    virtual std::vector<TPathWithStage> GetFilePaths() const;

    //! Called when a job is unable to read a chunk.
    void OnChunkFailed(const NChunkClient::TChunkId& chunkId);

    //! Called when a job is unable to read an intermediate chunk
    //! (i.e. that is not a part of the input).
    /*!
     *  The default implementation fails the operation immediately.
     *  Those operations providing some fault tolerance for intermediate chunks
     *  must override this method.
     */
    void OnIntermediateChunkUnavailable(const NChunkClient::TChunkId& chunkId);


    DECLARE_ENUM(EInputChunkState,
        (Active)
        (Skipped)
        (Waiting)
    );

    struct TStripeDescriptor
    {
        TChunkStripePtr Stripe;
        IChunkPoolInput::TCookie Cookie;
        TTaskPtr Task;

        TStripeDescriptor()
            : Cookie(IChunkPoolInput::NullCookie)
        { }

        void Persist(TPersistenceContext& context);

    };

    struct TInputChunkDescriptor
    {
        TSmallVector<TStripeDescriptor, 1> InputStripes;
        TSmallVector<NChunkClient::TRefCountedChunkSpecPtr, 1> ChunkSpecs;
        EInputChunkState State;

        TInputChunkDescriptor()
            : State(EInputChunkState::Active)
        { }

        void Persist(TPersistenceContext& context);

    };

    //! Called when a job is unable to read an input chunk or
    //! chunk scratcher has encountered unavailable chunk.
    void OnInputChunkUnavailable(
        const NChunkClient::TChunkId& chunkId,
        TInputChunkDescriptor& descriptor);

    void OnInputChunkAvailable(
        const NChunkClient::TChunkId& chunkId,
        TInputChunkDescriptor& descriptor,
         const NChunkClient::TChunkReplicaList& replicas);

    virtual bool IsOutputLivePreviewSupported() const;
    virtual bool IsIntermediateLivePreviewSupported() const;

    void OnOperationCompleted();
    virtual void DoOperationCompleted();

    void OnOperationFailed(const TError& error);
    virtual void DoOperationFailed(const TError& error);

    virtual bool IsCompleted() const = 0;


    // Unsorted helpers.

    // Enables sorted output from user jobs.
    virtual bool IsSortedOutputSupported() const;

    std::vector<Stroka> CheckInputTablesSorted(
        const TNullable< std::vector<Stroka> >& keyColumns);
    static bool CheckKeyColumnsCompatible(
        const std::vector<Stroka>& fullColumns,
        const std::vector<Stroka>& prefixColumns);

    void RegisterInputStripe(TChunkStripePtr stripe, TTaskPtr task);

    void RegisterOutput(
        const NChunkServer::TChunkTreeId& chunkTreeId,
        int key,
        int tableIndex);
    void RegisterOutput(
        const NChunkServer::TChunkTreeId& chunkTreeId,
        int key,
        int tableIndex,
        TOutputTable& table);
    void RegisterOutput(
        TJobletPtr joblet,
        int key);

    void RegisterIntermediate(
        TCompleteJobPtr completedJob,
        TChunkStripePtr stripe);

    bool HasEnoughChunkLists(int requestedCount);
    NChunkClient::TChunkListId ExtractChunkList();

    //! Returns the list of all input chunks collected from all input tables.
    std::vector<NChunkClient::TRefCountedChunkSpecPtr> CollectInputChunks() const;

    //! Converts a list of input chunks into a list of chunk stripes for further
    //! processing. Each stripe receives exactly one chunk (as suitable for most
    //! jobs except merge). The resulting stripes are of approximately equal
    //! size. The size per stripe is either |maxSliceDataSize| or
    //! |TotalInputDataSize / jobCount|, whichever is smaller. If the resulting
    //! list contains less than |jobCount| stripes then |jobCount| is decreased
    //! appropriately.
    std::vector<TChunkStripePtr> SliceInputChunks(i64 maxSliceDataSize, int jobCount);

    int SuggestJobCount(
        i64 totalDataSize,
        i64 dataSizePerJob,
        TNullable<int> configJobCount) const;

    void InitUserJobSpec(
        NScheduler::NProto::TUserJobSpec* proto,
        TUserJobSpecPtr config,
        const std::vector<TRegularUserFile>& regularFiles,
        const std::vector<TUserTableFile>& tableFiles);

    static void AddUserJobEnvironment(
        NScheduler::NProto::TUserJobSpec* proto,
        TJobletPtr joblet);

    // Amount of memory reserved for output table writers in job proxy.
    i64 GetFinalOutputIOMemorySize(TJobIOConfigPtr ioConfig) const;

    i64 GetFinalIOMemorySize(
        TJobIOConfigPtr ioConfig,
        const TChunkStripeStatisticsVector& stripeStatistics) const;

    static void InitIntermediateInputConfig(TJobIOConfigPtr config);

    static void InitIntermediateOutputConfig(TJobIOConfigPtr config);
    void InitFinalOutputConfig(TJobIOConfigPtr config);

private:
    typedef TOperationControllerBase TThis;

    typedef yhash_map<NChunkClient::TChunkId, TInputChunkDescriptor> TInputChunkMap;

    //! Keeps information needed to maintain the liveness state of input chunks.
    TInputChunkMap InputChunkMap;

    class TInputChunkScratcher
        : public virtual TRefCounted
    {
    public:
        explicit TInputChunkScratcher(TOperationControllerBase* controller);

        //! Starts periodic polling.
        /*!
         *  Should be called when operation preparation is complete.
         *  Safe to call multiple times.
         */
        void Start();

    private:
        void LocateChunks();
        void OnLocateChunksResponse(NChunkClient::TChunkServiceProxy::TRspLocateChunksPtr rsp);

        TOperationControllerBase* Controller;
        TPeriodicInvokerPtr PeriodicInvoker;
        NChunkClient::TChunkServiceProxy Proxy;
        TInputChunkMap::iterator NextChunkIterator;
        bool Started;

        NLog::TTaggedLogger& Logger;

    };

    typedef TIntrusivePtr<TInputChunkScratcher> TInputChunkScratcherPtr;

    TOperationSpecBasePtr Spec;
    TChunkListPoolPtr ChunkListPool;

    int CachedPendingJobCount;

    NNodeTrackerClient::NProto::TNodeResources CachedNeededResources;

    //! Maps an intermediate chunk id to its originating completed job.
    yhash_map<NChunkServer::TChunkId, TCompleteJobPtr> ChunkOriginMap;

    //! Maps scheduler's job ids to controller's joblets.
    //! NB: |TJobPtr -> TJobletPtr| mapping would be faster but
    //! it cannot be serialized that easily.
    yhash_map<TJobId, TJobletPtr> JobletMap;

    //! Used to distinguish already seen ChunkSpecs while building #InputChunkMap.
    yhash_set<NChunkClient::TRefCountedChunkSpecPtr> InputChunkSpecs;

    TInputChunkScratcherPtr InputChunkScratcher;

    //! Increments each time a new job is scheduled.
    TIdGenerator JobIndexGenerator;


    static const NProto::TUserJobResult* FindUserJobResult(TJobletPtr joblet);

};

////////////////////////////////////////////////////////////////////////////////

namespace {

template <class TSpec>
TIntrusivePtr<TSpec> ParseOperationSpec(TOperation* operation, NYTree::INodePtr specTemplateNode)
{
    auto specNode = specTemplateNode
        ? NYTree::UpdateNode(specTemplateNode, operation->GetSpec())
        : operation->GetSpec();
    auto spec = New<TSpec>();
    try {
        spec->Load(specNode);
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error parsing operation spec") << ex;
    }
    return spec;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
