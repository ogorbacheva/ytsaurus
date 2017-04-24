#pragma once

#include <yt/ytlib/job_tracker_client/public.h>

#include <yt/ytlib/scheduler/public.h>

#include <yt/core/actions/callback.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

using NJobTrackerClient::TJobId;
using NJobTrackerClient::EJobType;
using NJobTrackerClient::EJobState;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TSchedulerService)

DECLARE_REFCOUNTED_CLASS(TOperation)

DECLARE_REFCOUNTED_STRUCT(TBriefJobStatistics)

DECLARE_REFCOUNTED_CLASS(TJob)

DECLARE_REFCOUNTED_STRUCT(TScheduleJobResult)

struct TJobStartRequest;

using TJobList = std::list<TJobPtr>;
using TJobSpecBuilder = TCallback<void(NJobTrackerClient::NProto::TJobSpec* jobSpec)>;

struct TUpdatedJob;
struct TCompletedJob;

struct TExecNodeDescriptor;
DECLARE_REFCOUNTED_STRUCT(TExecNodeDescriptorList);

DECLARE_REFCOUNTED_CLASS(TExecNode)

DECLARE_REFCOUNTED_CLASS(TFairShareStrategyConfig)
DECLARE_REFCOUNTED_CLASS(TEventLogConfig)
DECLARE_REFCOUNTED_CLASS(TIntermediateChunkScraperConfig)
DECLARE_REFCOUNTED_CLASS(TJobSplitterConfig)

DECLARE_REFCOUNTED_CLASS(TOperationOptions)
DECLARE_REFCOUNTED_CLASS(TSimpleOperationOptions)
DECLARE_REFCOUNTED_CLASS(TMapOperationOptions)
DECLARE_REFCOUNTED_CLASS(TUnorderedMergeOperationOptions)
DECLARE_REFCOUNTED_CLASS(TOrderedMergeOperationOptions)
DECLARE_REFCOUNTED_CLASS(TSortedMergeOperationOptions)
DECLARE_REFCOUNTED_CLASS(TEraseOperationOptions)
DECLARE_REFCOUNTED_CLASS(TReduceOperationOptions)
DECLARE_REFCOUNTED_CLASS(TJoinReduceOperationOptions)
DECLARE_REFCOUNTED_CLASS(TSortOperationOptionsBase)
DECLARE_REFCOUNTED_CLASS(TSortOperationOptions)
DECLARE_REFCOUNTED_CLASS(TMapReduceOperationOptions)
DECLARE_REFCOUNTED_CLASS(TRemoteCopyOperationOptions)
DECLARE_REFCOUNTED_CLASS(TTestingOptions)

DECLARE_REFCOUNTED_CLASS(TOperationAlertsConfig)
DECLARE_REFCOUNTED_CLASS(TSchedulerConfig)
DECLARE_REFCOUNTED_CLASS(TScheduler)

struct IEventLogHost;

DECLARE_REFCOUNTED_STRUCT(ISchedulerStrategy)
struct ISchedulerStrategyHost;

DECLARE_REFCOUNTED_STRUCT(TControllerTransactions)

struct IOperationHost;

DECLARE_REFCOUNTED_STRUCT(ISchedulingContext)
DECLARE_REFCOUNTED_STRUCT(IOperationController)

DECLARE_REFCOUNTED_CLASS(TIntermediateChunkScraper)

DECLARE_REFCOUNTED_STRUCT(IJobHost)
DECLARE_REFCOUNTED_CLASS(TJobHost)

class TMasterConnector;

using NJobTrackerClient::NProto::TJobResult;
using NJobTrackerClient::NProto::TJobStatus;

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EAlertType,
    (UpdatePools)
    (UpdateConfig)
    (UpdateFairShare)
);

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EOperationAlertType,
    (UnusedTmpfsSpace)
    (LostIntermediateChunks)
    (LostInputChunks)
    (IntermediateDataSkew)
    (LongAbortedJobs)
    (ExcessiveDiskUsage)
    (ShortJobsDuration)
);

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
