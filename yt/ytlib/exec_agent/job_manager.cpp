#include "stdafx.h"
#include "job_manager.h"
#include "config.h"
#include "slot.h"
#include "job.h"
#include "bootstrap.h"
#include "private.h"
#include "environment_manager.h"

#include <ytlib/ytree/yson_writer.h>
#include <ytlib/job_proxy/config.h>
#include <ytlib/misc/fs.h>

namespace NYT {
namespace NExecAgent {

using namespace NScheduler;
using namespace NScheduler::NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ExecAgentLogger;

////////////////////////////////////////////////////////////////////////////////

TJobManager::TJobManager(
    TJobManagerConfigPtr config,
    TBootstrap* bootstrap)
    : Config(config)
    , Bootstrap(bootstrap)
{
    YASSERT(config);
    YASSERT(bootstrap);
    VERIFY_INVOKER_AFFINITY(bootstrap->GetControlInvoker(), ControlThread);

    // Init job slots.
    for (int slotIndex = 0; slotIndex < Config->SlotCount; ++slotIndex) {
        auto slotName = Sprintf("slot.%d", slotIndex);
        auto slotPath = NFS::CombinePaths(Config->SlotLocation, slotName);
        Slots.push_back(New<TSlot>(slotPath, slotName));

        auto proxyConfigPath = NFS::CombinePaths(slotPath, ProxyConfigFileName);
        TFileOutput output(proxyConfigPath);
        NYTree::TYsonWriter writer(&output);
        bootstrap->GetJobProxyConfig()->Save(&writer);
    }
}

TJobPtr TJobManager::FindJob(const TJobId& jobId)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    auto it = Jobs.find(jobId);
    return it == Jobs.end() ? NULL : it->second;
}

TJobPtr TJobManager::GetJob(const TJobId& jobId)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    auto job = FindJob(jobId);
    if (!job) {
        // TODO(babenko): error code
        ythrow yexception() << Sprintf("No such job %s", ~jobId.ToString());
    }
    return job;
}

std::vector<TJobPtr> TJobManager::GetAllJobs()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    std::vector<TJobPtr> result;
    FOREACH (const auto& pair, Jobs) {
        result.push_back(pair.second);
    }
    return result;
}

TNodeUtilization TJobManager::GetUtilization()
{
    TNodeUtilization result;
    result.set_total_slot_count(Slots.size());
    int freeCount = 0;
    FOREACH (auto slot, Slots) {
        if (slot->IsFree()) {
            ++freeCount;
        }
    }
    result.set_free_slot_count(freeCount);
    return result;
}

void TJobManager::StartJob(
    const TJobId& jobId,
    const NScheduler::NProto::TJobSpec& jobSpec)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    TSlotPtr emptySlot;
    FOREACH(auto slot, Slots) {
        if (slot->IsFree()) {
            emptySlot = slot;
            break;
        }
    }

    if (!emptySlot) {
        LOG_FATAL("All slots are busy (JobId: %s)", ~jobId.ToString());
    }

    LOG_DEBUG("Found slot for new job (JobId: %s, working directory: %s)", 
        ~jobId.ToString(),
        ~emptySlot->GetWorkingDirectory());

    auto job = New<TJob>(
        jobId,
        jobSpec,
        ~Bootstrap->GetChunkCache(),
        ~emptySlot);

    job->Start(~Bootstrap->GetEnvironmentManager());

    Jobs[jobId] = job;

    LOG_DEBUG("Job started (JobId: %s, JobType: %s)",
        ~jobId.ToString(),
        ~EJobType(jobSpec.type()).ToString());
}

void TJobManager::AbortJob(const TJobId& jobId)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    auto job = GetJob(jobId);
    job->Abort();
}

void TJobManager::RemoveJob(const TJobId& jobId)
{
    VERIFY_THREAD_AFFINITY(ControlThread);
    YASSERT(GetJob(jobId)->GetProgress() > EJobProgress::Cleanup);

    Jobs.erase(jobId);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
} // namespace NExecAgent
