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
#include "user_job_io.h"

#include <ytlib/actions/invoker_util.h>
#include <ytlib/actions/parallel_awaiter.h>

#include <ytlib/logging/log_manager.h>

#include <ytlib/scheduler/public.h>

#include <ytlib/bus/tcp_client.h>

#include <ytlib/rpc/bus_channel.h>

#include <server/scheduler/job_resources.h>

#include <ytlib/chunk_client/config.h>
#include <ytlib/chunk_client/client_block_cache.h>
#include <ytlib/chunk_client/remote_reader.h>
#include <ytlib/chunk_client/async_reader.h>
#include <ytlib/chunk_client/node_directory.h>

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
using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

TJobProxy::TJobProxy(
    TJobProxyConfigPtr config,
    const TJobId& jobId)
    : Config(config)
    , JobId(jobId)
    , Logger(JobProxyLogger)
{
    Logger.AddTag(Sprintf("JobId: %s", ~ToString(JobId)));
}

void TJobProxy::SendHeartbeat()
{
    HeartbeatInvoker->ScheduleNext();

    auto req = SupervisorProxy->OnJobProgress();
    ToProto(req->mutable_job_id(), JobId);
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
        _exit(EJobProxyExitCode::HeartbeatFailed);
    }

    LOG_DEBUG("Successfully reported heartbeat to supervisor");
}

void TJobProxy::RetrieveJobSpec()
{
    LOG_INFO("Requesting job spec");

    auto req = SupervisorProxy->GetJobSpec();
    ToProto(req->mutable_job_id(), JobId);

    auto rsp = req->Invoke().Get();
    THROW_ERROR_EXCEPTION_IF_FAILED(*rsp, "Failed to get job spec");

    JobSpec = rsp->job_spec();
    ResourceUsage = rsp->resource_usage();

    LOG_INFO("Job spec received (JobType: %s, ResourceLimits: {%s})\n%s",
        ~NScheduler::EJobType(rsp->job_spec().type()).ToString(),
        ~FormatResources(ResourceUsage),
        ~rsp->job_spec().DebugString());
}

void TJobProxy::Run()
{
    auto result = DoRun();

    if (Job) {
        HeartbeatInvoker->Stop();

        std::vector<NChunkClient::TChunkId> failedChunks;
        GetFailedChunks(&failedChunks).Get();
        LOG_DEBUG("Found %d failed chunks", static_cast<int>(failedChunks.size()));
        ToProto(result.mutable_failed_chunk_ids(), failedChunks);
    }
    
    ReportResult(result);
}
   
TJobResult TJobProxy::DoRun()
{
    try {
        auto supervisorClient = CreateTcpBusClient(Config->SupervisorConnection);

        auto supervisorChannel = CreateBusChannel(supervisorClient, Config->SupervisorRpcTimeout);
        SupervisorProxy.Reset(new TSupervisorServiceProxy(supervisorChannel));

        MasterChannel = CreateBusChannel(supervisorClient, Config->MasterRpcTimeout);

        RetrieveJobSpec();

        const auto& jobSpec = GetJobSpec();
        auto jobType = NScheduler::EJobType(jobSpec.type());

        BlockCache = NChunkClient::CreateClientBlockCache(New<NChunkClient::TClientBlockCacheConfig>());

        NodeDirectory = New<TNodeDirectory>();
        NodeDirectory->MergeFrom(jobSpec.node_directory());

        HeartbeatInvoker = New<TPeriodicInvoker>(
            GetSyncInvoker(),
            BIND(&TJobProxy::SendHeartbeat, MakeWeak(this)),
            Config->HeartbeatPeriod);

        NYT::NThread::SetCurrentThreadName(~jobType.ToString());
        
        SetLargeBlockLimit(jobSpec.lfalloc_buffer_size());

        switch (jobType) {
            case NScheduler::EJobType::Map: {
                const auto& jobSpecExt = jobSpec.GetExtension(TMapJobSpecExt::map_job_spec_ext);
                auto userJobIO = CreateMapJobIO(Config->JobIO, this);
                Job = CreateUserJob(this, jobSpecExt.mapper_spec(), userJobIO);
                break;
            }

            case NScheduler::EJobType::SortedReduce: {
                const auto& jobSpecExt = jobSpec.GetExtension(TReduceJobSpecExt::reduce_job_spec_ext);
                auto userJobIO = CreateSortedReduceJobIO(Config->JobIO, this);
                Job = CreateUserJob(this, jobSpecExt.reducer_spec(), userJobIO);
                break;
            }

            case NScheduler::EJobType::PartitionMap: {
                const auto& jobSpecExt = jobSpec.GetExtension(TPartitionJobSpecExt::partition_job_spec_ext);
                YCHECK(jobSpecExt.has_mapper_spec());
                auto userJobIO = CreatePartitionMapJobIO(Config->JobIO, this);
                Job = CreateUserJob(this, jobSpecExt.mapper_spec(), userJobIO);
                break;
            }

            case NScheduler::EJobType::PartitionReduce: {
                const auto& jobSpecExt = jobSpec.GetExtension(TReduceJobSpecExt::reduce_job_spec_ext);
                auto userJobIO = CreatePartitionReduceJobIO(Config->JobIO, this);
                Job = CreateUserJob(this, jobSpecExt.reducer_spec(), userJobIO);
                break;
            }

            case NScheduler::EJobType::OrderedMerge:
                Job = CreateOrderedMergeJob(this);
                break;

            case NScheduler::EJobType::UnorderedMerge:
                Job = CreateUnorderedMergeJob(this);
                break;

            case NScheduler::EJobType::SortedMerge:
                Job = CreateSortedMergeJob(this);
                break;

            case NScheduler::EJobType::PartitionSort:
                Job = CreatePartitionSortJob(this);
                break;

            case NScheduler::EJobType::SimpleSort:
                Job = CreateSimpleSortJob(this);
                break;

            case NScheduler::EJobType::Partition:
                Job = CreatePartitionJob(this);
                break;

            default:
                YUNREACHABLE();
        }

        HeartbeatInvoker->Start();
        return Job->Run();

    } catch (const std::exception& ex) {
        LOG_ERROR(ex, "Job failed");

        TJobResult result;
        ToProto(result.mutable_error(), TError(ex));
        return result;
    }
}

TFuture<void> TJobProxy::GetFailedChunks(std::vector<NChunkClient::TChunkId>* failedChunks)
{
    *failedChunks = Job->GetFailedChunks();
     return MakeFuture();
/*
    if (failedChunks->empty()) {
        return MakeFuture();
    }

    yhash_set<NChunkClient::TChunkId> failedSet(failedChunks->begin(), failedChunks->end());

    auto awaiter = New<TParallelAwaiter>(GetSyncInvoker());

    LOG_INFO("Started collection additional failed chunks");

    FOREACH (const auto& inputSpec, JobSpec.input_specs()) {
        FOREACH (const auto& chunk, inputSpec.chunks()) {
            auto chunkId = NChunkClient::FromProto<TChunkId>(chunk.slice().chunk_id());
            auto pair = failedSet.insert(chunkId);
            if (pair.second) {
                LOG_INFO("Checking input chunk %s", ~ToString(chunkId));

                auto remoteReader = NChunkClient::CreateRemoteReader(
                    Config->JobIO->TableReader,
                    BlockCache,
                    MasterChannel,
                    chunkId,
                    FromProto<Stroka>(chunk.node_addresses()));

                awaiter->Await(
                    remoteReader->AsyncGetChunkMeta(),
                    BIND([=] (NChunkClient::IAsyncReader::TGetMetaResult result) mutable {
                        if (result.IsOK()) {
                            LOG_INFO("Input chunk %s is OK", ~ToString(chunkId));
                        } else {
                            LOG_ERROR(result, "Input chunk %s has failed", ~ToString(chunkId));
                            failedChunks->push_back(chunkId);
                        }
                        // This is important to capture #remoteReader.
                        remoteReader.Reset();
                    }));
            }
        }
    }

    return awaiter->Complete();
*/
}

void TJobProxy::ReportResult(const TJobResult& result)
{
    HeartbeatInvoker->Stop();

    auto req = SupervisorProxy->OnJobFinished();
    ToProto(req->mutable_job_id(), JobId);
    *req->mutable_result() = result;

    auto rsp = req->Invoke().Get();
    if (!rsp->IsOK()) {
        LOG_ERROR(*rsp, "Failed to report job result");
        NLog::TLogManager::Get()->Shutdown();
        _exit(EJobProxyExitCode::ResultReportFailed);
    }
}

TJobProxyConfigPtr TJobProxy::GetConfig()
{
    return Config;
}

const TJobSpec& TJobProxy::GetJobSpec() const
{
    return JobSpec;
}

const TNodeResources& TJobProxy::GetResourceUsage() const
{
    return ResourceUsage;
}

void TJobProxy::SetResourceUsage(const TNodeResources& usage)
{
    ResourceUsage = usage;

    // Fire-and-forget.
    auto req = SupervisorProxy->UpdateResourceUsage();
    ToProto(req->mutable_job_id(), JobId);
    *req->mutable_resource_usage() = ResourceUsage;
    req->Invoke().Subscribe(BIND(&TJobProxy::OnResourcesUpdated, MakeWeak(this)));
}

void TJobProxy::OnResourcesUpdated(TSupervisorServiceProxy::TRspUpdateResourceUsagePtr rsp)
{
    if (!rsp->IsOK()) {
        LOG_ERROR(*rsp, "Failed to update resource usage");
        NLog::TLogManager::Get()->Shutdown();
        _exit(EJobProxyExitCode::ResourcesUpdateFailed);
    }

    LOG_DEBUG("Successfully updated resource usage");
}

void TJobProxy::ReleaseNetwork()
{
    auto usage = GetResourceUsage();
    usage.set_network(0);
    SetResourceUsage(usage);
}

IChannelPtr TJobProxy::GetMasterChannel() const
{
    return MasterChannel;
}

IBlockCachePtr TJobProxy::GetBlockCache() const 
{
    return BlockCache;
}

TNodeDirectoryPtr TJobProxy::GetNodeDirectory() const 
{
    return NodeDirectory;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
