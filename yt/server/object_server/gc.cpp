#include "stdafx.h"
#include "gc.h"
#include "private.h"
#include "config.h"
#include "object_manager.h"

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/meta_state_facade.h>
#include <server/cell_master/serialization_context.h>

#include <server/object_server/object_manager.pb.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ObjectServerLogger;
static NProfiling::TProfiler& Profiler = ObjectServerProfiler;

////////////////////////////////////////////////////////////////////////////////

TGarbageCollector::TGarbageCollector(
    TObjectManagerConfigPtr config,
    NCellMaster::TBootstrap* bootstrap)
    : Config(config)
    , Bootstrap(bootstrap)
    , QueueSizeCounter("/gc_queue_size")
{
    YCHECK(Config);
    YCHECK(Bootstrap);
}

void TGarbageCollector::StartSweep()
{
    YCHECK(!SweepInvoker);
    SweepInvoker = New<TPeriodicInvoker>(
        Bootstrap->GetMetaStateFacade()->GetInvoker(),
        BIND(&TGarbageCollector::OnSweep, MakeWeak(this)),
        Config->GCSweepPeriod);
    SweepInvoker->Start();
}

void TGarbageCollector::StopSweep()
{
    if (SweepInvoker) {
        SweepInvoker->Stop();
        SweepInvoker.Reset();
    }
}

void TGarbageCollector::Save(const NCellMaster::TSaveContext& context) const
{
    SaveSet(context.GetOutput(), ZombieIds);
}

void TGarbageCollector::Load(const NCellMaster::TLoadContext& context)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    LoadSet(context.GetInput(), ZombieIds);

    CollectPromise = NewPromise<void>();
    if (ZombieIds.empty()) {
        CollectPromise.Set();
    }

    ProfileQueueSize();
}

void TGarbageCollector::Clear()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    ZombieIds.clear();

    CollectPromise = NewPromise<void>();
    CollectPromise.Set();

    ProfileQueueSize();
}

TFuture<void> TGarbageCollector::Collect()
{
    VERIFY_THREAD_AFFINITY_ANY();

    return CollectPromise;
}

void TGarbageCollector::Enqueue(const TObjectId& id)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    if (ZombieIds.empty()) {
        CollectPromise = NewPromise<void>();
    }

    YCHECK(ZombieIds.insert(id).second);

    ProfileQueueSize();
}

void TGarbageCollector::Dequeue(const TObjectId& id)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    YCHECK(ZombieIds.erase(id) == 1);

    if (ZombieIds.empty()) {
        auto metaStateManager = Bootstrap->GetMetaStateFacade()->GetManager();
        LOG_DEBUG_UNLESS(metaStateManager->IsRecovery(), "GC queue is empty");
        CollectPromise.Set();
    }

    ProfileQueueSize();
}

void TGarbageCollector::OnSweep()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    auto metaStateFacade = Bootstrap->GetMetaStateFacade();
    auto metaStateManager = metaStateFacade->GetManager();
    if (ZombieIds.empty() || !metaStateManager->HasActiveQuorum()) {
        SweepInvoker->ScheduleNext();
        return;
    }

    // Extract up to MaxObjectsPerGCSweep objects and post a mutation.
    NProto::TMetaReqDestroyObjects request;
    auto it = ZombieIds.begin();
    while (it != ZombieIds.end() && request.object_ids_size() < Config->MaxObjectsPerGCSweep) {
        *request.add_object_ids() = it->ToProto();
        ++it;
    }

    LOG_DEBUG("Starting GC sweep for %d objects", request.object_ids_size());

    auto invoker = metaStateFacade->GetEpochInvoker();
    Bootstrap
        ->GetObjectManager()
        ->CreateDestroyObjectsMutation(request)
        ->OnSuccess(BIND(&TGarbageCollector::OnCommitSucceeded, MakeWeak(this)).Via(invoker))
        ->OnError(BIND(&TGarbageCollector::OnCommitFailed, MakeWeak(this)).Via(invoker))
        ->PostCommit();
}

void TGarbageCollector::OnCommitSucceeded()
{
    LOG_DEBUG("GC sweep commit succeeded");

    SweepInvoker->ScheduleOutOfBand();
    SweepInvoker->ScheduleNext();
}

void TGarbageCollector::OnCommitFailed(const TError& error)
{
    LOG_ERROR(error, "GC sweep commit failed");

    SweepInvoker->ScheduleNext();
}

void TGarbageCollector::ProfileQueueSize()
{
    Profiler.Aggregate(QueueSizeCounter, ZombieIds.size());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT
