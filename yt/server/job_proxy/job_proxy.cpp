#include "stdafx.h"
#include "config.h"
#include "job_proxy.h"
#include "user_job.h"
#include "sorted_merge_job.h"
#include "merge_job.h"
#include "simple_sort_job.h"
#include "partition_sort_job.h"
#include "partition_job.h"
#include "map_job_io.h"
#include "partition_map_job_io.h"
#include "sorted_reduce_job_io.h"
#include "partition_reduce_job_io.h"

#include <ytlib/actions/invoker_util.h>
#include <ytlib/actions/parallel_awaiter.h>

#include <ytlib/logging/log_manager.h>

#include <ytlib/scheduler/public.h>

#include <ytlib/bus/tcp_client.h>

#include <ytlib/rpc/bus_channel.h>

#include <server/scheduler/job_resources.h>

#include <ytlib/chunk_client/client_block_cache.h>
#include <ytlib/chunk_client/remote_reader.h>
#include <ytlib/chunk_client/async_reader.h>

#include <ytlib/meta_state/master_channel.h>


// Defined inside util/private/lf_alloc/lf_allocX64.cpp
void SetLargeBlockLimit(i64 limit);


namespace NYT {
namespace NJobProxy {

using namespace NElection;
using namespace NScheduler;
using namespace NExecAgent;
using namespace NBus;
using namespace NRpc;
using namespace NScheduler;
using namespace NScheduler::NProto;

////////////////////////////////////////////////////////////////////////////////

TJobProxy::TJobProxy(
    TJobProxyConfigPtr config,
    const TJobId& jobId)
    : Config(config)
    , JobId(jobId)
    , Logger(JobProxyLogger)
{
    Logger.AddTag(Sprintf("JobId: %s", ~JobId.ToString()));

    auto client = CreateTcpBusClient(Config->SupervisorConnection);
    auto channel = CreateBusChannel(client, Config->SupervisorRpcTimeout);
    SupervisorProxy.Reset(new TSupervisorServiceProxy(channel));
}

void TJobProxy::SendHeartbeat()
{
    HeartbeatInvoker->ScheduleNext();

    auto req = SupervisorProxy->OnJobProgress();
    *req->mutable_job_id() = JobId.ToProto();
    req->set_progress(Job->GetProgress());
    req->Invoke().Subscribe(BIND(&TJobProxy::OnHeartbeatResponse, MakeWeak(this)));

    LOG_DEBUG("Supervisor heartbeat sent");
}

void TJobProxy::OnHeartbeatResponse(TSupervisorServiceProxy::TRspOnJobProgressPtr rsp)
{
    if (!rsp->IsOK()) {
        // NB: user process is not killed here.
        // Good user processes are supposed to die themselves
        // when io pipes are closed.
        // Bad processes will die at container shutdown.
        LOG_ERROR(*rsp, "Error sending heartbeat to supervisor");

        NLog::TLogManager::Get()->Shutdown();
        // TODO(babenko): extract error code constant
        _exit(122);
    }

    LOG_DEBUG("Successfully reported heartbeat to supervisor");
}

void TJobProxy::RetrieveJobSpec()
{
    LOG_INFO("Requesting job spec");
    auto req = SupervisorProxy->GetJobSpec();
    *req->mutable_job_id() = JobId.ToProto();

    auto rsp = req->Invoke().Get();
    if (!rsp->IsOK()) {
        THROW_ERROR_EXCEPTION("Failed to get job spec")
            << rsp->GetError();
    }

    JobSpec = rsp->job_spec();
    ResourceUsage = rsp->resource_usage();

    LOG_INFO("Job spec received (JobType: %s, ResourceLimits: {%s})\n%s",
        ~EJobType(rsp->job_spec().type()).ToString(),
        ~FormatResources(ResourceUsage),
        ~rsp->job_spec().DebugString());
}

void TJobProxy::Run()
{
    HeartbeatInvoker = New<TPeriodicInvoker>(
        GetSyncInvoker(),
        BIND(&TJobProxy::SendHeartbeat, MakeWeak(this)),
        Config->HeartbeatPeriod);

    try {
        RetrieveJobSpec();

        const auto& jobSpec = GetJobSpec();
        auto jobType = EJobType(jobSpec.type());
        NYT::NThread::SetCurrentThreadName(~jobType.ToString());
        SetLargeBlockLimit(jobSpec.lfalloc_buffer_size());

        switch (jobType) {
            case EJobType::Map: {
                const auto& jobSpecExt = jobSpec.GetExtension(TMapJobSpecExt::map_job_spec_ext);
                auto userJobIO = CreateMapJobIO(Config->JobIO, Config->Masters, jobSpec);
                Job = CreateUserJob(this, jobSpecExt.mapper_spec(), userJobIO);
                break;
            }

            case EJobType::SortedReduce: {
                const auto& jobSpecExt = jobSpec.GetExtension(TReduceJobSpecExt::reduce_job_spec_ext);
                auto userJobIO = CreateSortedReduceJobIO(Config->JobIO, Config->Masters, jobSpec);
                Job = CreateUserJob(this, jobSpecExt.reducer_spec(), userJobIO);
                break;
            }

            case EJobType::PartitionMap: {
                const auto& jobSpecExt = jobSpec.GetExtension(TPartitionJobSpecExt::partition_job_spec_ext);
                YCHECK(jobSpecExt.has_mapper_spec());
                auto userJobIO = CreatePartitionMapJobIO(Config->JobIO, Config->Masters, jobSpec);
                Job = CreateUserJob(this, jobSpecExt.mapper_spec(), userJobIO);
                break;
            }

            case EJobType::PartitionReduce: {
                const auto& jobSpecExt = jobSpec.GetExtension(TReduceJobSpecExt::reduce_job_spec_ext);
                auto userJobIO = CreatePartitionReduceJobIO(
                    Config->JobIO,
                    this,
                    Config->Masters,
                    jobSpec);
                Job = CreateUserJob(this, jobSpecExt.reducer_spec(), userJobIO);
                break;
            }

            case EJobType::OrderedMerge:
                Job = CreateOrderedMergeJob(this);
                break;

            case EJobType::UnorderedMerge:
                Job = CreateUnorderedMergeJob(this);
                break;

            case EJobType::SortedMerge:
                Job = CreateSortedMergeJob(this);
                break;

            case EJobType::PartitionSort:
                Job = CreatePartitionSortJob(this);
                break;

            case EJobType::SimpleSort:
                Job = CreateSimpleSortJob(this);
                break;

            case EJobType::Partition:
                Job = CreatePartitionJob(this);
                break;

            default:
                YUNREACHABLE();
        }

        HeartbeatInvoker->Start();
        auto result = Job->Run();
        HeartbeatInvoker->Stop();

        ReportResult(result);
    } catch (const std::exception& ex) {
        HeartbeatInvoker->Stop();

        LOG_ERROR(ex, "Job failed");

        TJobResult result;
        ToProto(result.mutable_error(), TError(ex));

        std::vector<NChunkClient::TChunkId> failedChunks;
        GetFailedChunks(&failedChunks).Get();
        LOG_DEBUG("Found %d failed chunks", static_cast<int>(failedChunks.size()));
        ToProto(result.mutable_failed_chunk_ids(), failedChunks);

        ReportResult(result);
    }
}

TFuture<void> TJobProxy::GetFailedChunks(std::vector<NChunkClient::TChunkId>* failedChunks)
{
    *failedChunks = Job->GetFailedChunks();

    if (!failedChunks->empty()) {
        yhash_set<NChunkClient::TChunkId> failedSet(failedChunks->begin(), failedChunks->end());

        auto blockCache = CreateClientBlockCache(New<NChunkClient::TClientBlockCacheConfig>());
        auto masterChannel = CreateLeaderChannel(Config->Masters);
        auto awaiter = New<TParallelAwaiter>(GetSyncInvoker());

        FOREACH (const auto& inputSpec, JobSpec.input_specs()) {
            FOREACH (const auto& chunk, inputSpec.chunks()) {
                auto chunkId = NChunkClient::TChunkId::FromProto(chunk.slice().chunk_id());
                auto pair = failedSet.insert(chunkId);
                if (pair.second) {
                    auto remoteReader = NChunkClient::CreateRemoteReader(
                        Config->JobIO->TableReader,
                        blockCache,
                        masterChannel,
                        chunkId,
                        FromProto<Stroka>(chunk.node_addresses()));

                    awaiter->Await(
                        remoteReader->AsyncGetChunkMeta(),
                        BIND([=] (NChunkClient::IAsyncReader::TGetMetaResult meta) mutable {
                            if (!meta.IsOK()) {
                                failedChunks->push_back(chunkId);
                            }
                            // This is important to capture #remoteReader.
                            remoteReader.Reset();
                        }));
                }
            }
        }

        auto result = NewPromise<void>();
        awaiter->Complete(BIND([=] () mutable {
            result.Set();
        }));

        return result;
    }

    return MakeFuture();
}

void TJobProxy::ReportResult(const TJobResult& result)
{
    HeartbeatInvoker->Stop();

    auto req = SupervisorProxy->OnJobFinished();
    *req->mutable_job_id() = JobId.ToProto();
    *req->mutable_result() = result;

    auto rsp = req->Invoke().Get();
    if (!rsp->IsOK()) {
        LOG_ERROR(*rsp, "Failed to report job result");

        NLog::TLogManager::Get()->Shutdown();
        // TODO(babenko): extract error code constant
        _exit(123);
    }
}

TJobProxyConfigPtr TJobProxy::GetConfig()
{
    return Config;
}

const TJobSpec& TJobProxy::GetJobSpec()
{
    return JobSpec;
}

TNodeResources TJobProxy::GetResourceUsage()
{
    return ResourceUsage;
}

void TJobProxy::SetResourceUsage(const TNodeResources& usage)
{
    ResourceUsage = usage;

    // Fire-and-forget.
    auto req = SupervisorProxy->UpdateResourceUsage();
    *req->mutable_job_id() = JobId.ToProto();
    *req->mutable_resource_usage() = ResourceUsage;
    req->Invoke().Subscribe(BIND(&TJobProxy::OnResourcesUpdated, MakeWeak(this)));
}

void TJobProxy::OnResourcesUpdated(TSupervisorServiceProxy::TRspUpdateResourceUsagePtr rsp)
{
    if (!rsp->IsOK()) {
        LOG_ERROR(*rsp, "Failed to update resource usage");

        NLog::TLogManager::Get()->Shutdown();
        // TODO(babenko): extract error code constant
        _exit(121);
    }

    LOG_DEBUG("Successfully updated resource usage");
}

void TJobProxy::ReleaseNetwork()
{
    auto usage = GetResourceUsage();
    usage.set_network(0);
    SetResourceUsage(usage);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
