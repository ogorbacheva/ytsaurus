#include "stdafx.h"
#include "decorated_meta_state.h"
#include "change_log_cache.h"
#include "snapshot_store.h"

#include "../actions/action_util.h"

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = MetaStateLogger;

////////////////////////////////////////////////////////////////////////////////

TDecoratedMetaState::TDecoratedMetaState(
    IMetaState::TPtr state,
    TSnapshotStore::TPtr snapshotStore,
    TChangeLogCache::TPtr changeLogCache)
    : State(state)
    , SnapshotStore(snapshotStore)
    , ChangeLogCache(changeLogCache)
    , StateQueue(New<TActionQueue>())
    , SnapshotQueue(New<TActionQueue>())
{
    YASSERT(~state != NULL);
    YASSERT(~snapshotStore != NULL);
    YASSERT(~changeLogCache != NULL);

    VERIFY_INVOKER_AFFINITY(StateQueue->GetInvoker(), StateThread);

    ComputeReachableVersion();
}

IInvoker::TPtr TDecoratedMetaState::GetStateInvoker()
{
    VERIFY_THREAD_AFFINITY_ANY();

    return StateQueue->GetInvoker();
}

IInvoker::TPtr TDecoratedMetaState::GetSnapshotInvoker()
{
    VERIFY_THREAD_AFFINITY_ANY();

    return SnapshotQueue->GetInvoker();
}

IMetaState::TPtr TDecoratedMetaState::GetState() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return State;
}

void TDecoratedMetaState::Clear()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    State->Clear();
    Version = TMetaVersion();
    CurrentChangeLog.Reset();
}

TFuture<TVoid>::TPtr TDecoratedMetaState::Save(TOutputStream* output)
{
    YASSERT(output != NULL);
    VERIFY_THREAD_AFFINITY(StateThread);

    LOG_INFO("Started saving snapshot");

    auto started = TInstant::Now();
    return State->Save(output, GetSnapshotInvoker())->Apply(FromFunctor([=] (TVoid) -> TVoid
        {
            auto finished = TInstant::Now();
            LOG_INFO("Finished saving snapshot (Time: %.3f)", (finished - started).SecondsFloat());
            return TVoid();
        }));
}

void TDecoratedMetaState::Load(
    i32 segmentId,
    TInputStream* input)
{
    YASSERT(input != NULL);
    VERIFY_THREAD_AFFINITY(StateThread);

    LOG_INFO("Started loading snapshot %d", segmentId);

    CurrentChangeLog.Reset();
    UpdateVersion(TMetaVersion(segmentId, 0));

    auto started = TInstant::Now();
    State->Load(input);
    auto finished = TInstant::Now();

    LOG_INFO("Finished loading snapshot (Time: %.3f)", (finished - started).SecondsFloat());
}

void TDecoratedMetaState::ApplyChange(const TSharedRef& changeData)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    try {
        State->ApplyChange(changeData);
    } catch (...) {
        // TODO: consider adjusting error level or hiding it from log during recovery
        LOG_INFO("Failed to apply the change (Version: %s)\n%s",
            ~GetVersion().ToString(),
            ~CurrentExceptionMessage());
    }

    IncrementRecordCount();
}

void TDecoratedMetaState::ApplyChange(IAction::TPtr changeAction)
{
    YASSERT(~changeAction != NULL);
    VERIFY_THREAD_AFFINITY(StateThread);

    try {
        changeAction->Do();
    } catch (...) {
        // TODO: consider adjusting error level or hiding it from log during recovery
        LOG_INFO("Failed to apply the change (Version: %s)\n%s",
            ~GetVersion().ToString(),
            ~CurrentExceptionMessage());
    }

    IncrementRecordCount();
}

void TDecoratedMetaState::IncrementRecordCount()
{
    UpdateVersion(TMetaVersion(Version.SegmentId, Version.RecordCount + 1));
}

TCachedAsyncChangeLog::TPtr TDecoratedMetaState::GetCurrentChangeLog()
{
    if (~CurrentChangeLog != NULL) {
        return CurrentChangeLog;
    }

    CurrentChangeLog = ChangeLogCache->Get(Version.SegmentId);
    if (~CurrentChangeLog == NULL) {
        LOG_FATAL("The current changelog %d is missing", Version.SegmentId);
    }

    return CurrentChangeLog;
}

TAsyncChangeLog::TAppendResult::TPtr TDecoratedMetaState::LogChange(
    const TMetaVersion& version,
    const TSharedRef& changeData)
{
    VERIFY_THREAD_AFFINITY(StateThread);
    YASSERT(version.SegmentId == Version.SegmentId);

    auto changeLog = GetCurrentChangeLog();
    return changeLog->Append(version.RecordCount, changeData);
}

void TDecoratedMetaState::AdvanceSegment()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    CurrentChangeLog.Reset();
    UpdateVersion(TMetaVersion(Version.SegmentId + 1, 0));
   
    LOG_INFO("Switched to a new segment %d", Version.SegmentId);
}

void TDecoratedMetaState::RotateChangeLog()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    auto changeLog = GetCurrentChangeLog();
    changeLog->Finalize();

    AdvanceSegment();

    ChangeLogCache->Create(Version.SegmentId, changeLog->GetRecordCount());
}

void TDecoratedMetaState::ComputeReachableVersion()
{
    i32 maxSnapshotId = SnapshotStore->GetMaxSnapshotId();
    if (maxSnapshotId == NonexistingSnapshotId) {
        LOG_INFO("No snapshots found");
        // Let's pretend we have snapshot 0.
        maxSnapshotId = 0;
    } else {
        auto snapshotReader = SnapshotStore->GetReader(maxSnapshotId);
        LOG_INFO("Latest snapshot is %d", maxSnapshotId);
    }

    auto currentVersion = TMetaVersion(maxSnapshotId, 0);

    for (i32 segmentId = maxSnapshotId; ; ++segmentId) {
        auto changeLog = ChangeLogCache->Get(segmentId);
        if (~changeLog == NULL) {
            ReachableVersion = currentVersion;
            break;
        }

        bool isFinal = ~ChangeLogCache->Get(segmentId + 1) == NULL;

        LOG_DEBUG("Found changelog (Id: %d, RecordCount: %d, PrevRecordCount: %d, IsFinal: %s)",
            segmentId,
            changeLog->GetRecordCount(),
            changeLog->GetPrevRecordCount(),
            ~ToString(isFinal));

        currentVersion = TMetaVersion(segmentId, changeLog->GetRecordCount());
    }

    LOG_INFO("Reachable version is %s",
        ~ReachableVersion.ToString());
}         

TMetaVersion TDecoratedMetaState::GetVersion() const
{
    VERIFY_THREAD_AFFINITY(StateThread);

    // NB: No need to take a spinlock here since both
    // GetVersion and UpdateVersion have same affinity.
    return Version;
}

TMetaVersion TDecoratedMetaState::GetReachableVersion() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    TGuard<TSpinLock> guard(VersionSpinLock);
    return ReachableVersion;
}

void TDecoratedMetaState::UpdateVersion(const TMetaVersion& newVersion)
{
    TGuard<TSpinLock> guard(VersionSpinLock);
    Version = newVersion;
    ReachableVersion = Max(ReachableVersion, Version);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
