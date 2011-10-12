#pragma once

#include "common.h"
#include "meta_state.h"
#include "snapshot_store.h"
#include "change_log_cache.h"

#include "../misc/thread_affinity.h"

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TDecoratedMetaState
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TDecoratedMetaState> TPtr;

    TDecoratedMetaState(
        IMetaState::TPtr state,
        TSnapshotStore::TPtr snapshotStore,
        TChangeLogCache::TPtr changeLogCache);

    //! Returns the invoker used for updating the state.
    /*!
     * \note Thread affinity: any
     */
    IInvoker::TPtr GetStateInvoker();

    //! Returns the invoker used for creating snapshots.
    /*!
     * \note Thread affinity: any
     */
    IInvoker::TPtr GetSnapshotInvoker();

    //! Returns the current version of the state.
    /*!
     * \note Thread affinity: StateThread
     */
    TMetaVersion GetVersion() const;

    //! Returns the maximum reachable version of the state that
    //! can be obtained by reading the local snapshots, changelogs.
    /*!
     *  It is always no smaller than #GetVersion.
     *  Since the reachable version is used to determine the current priority
     *  during elections it can be read from an arbitrary thread.
     *
     * \note Thread affinity: any
     */
    TMetaVersion GetReachableVersion() const;

    //! Returns the underlying state.
    /*!
     * \note Thread affinity: any
     */
    IMetaState::TPtr GetState() const;

    //! Delegates the call to IMetaState::Clear.
    /*!
     * \note Thread affinity: StateThread
     */
    void Clear();
    
    //! Delegates the call to IMetaState::Save.
    /*!
     * \note Thread affinity: StateThread
     */
    TFuture<TVoid>::TPtr Save(TOutputStream* output);

    //! Delegates the call to IMetaState::Load and updates the version.
    /*!
     * \note Thread affinity: StateThread
     */
    TFuture<TVoid>::TPtr Load(i32 segmentId, TInputStream* input);
    
    //! Delegates the call to IMetaState::ApplyChange and updates the version.
    /*!
     * \note Thread affinity: StateThread
     */
    void ApplyChange(const TSharedRef& changeData);

    //! Executes a given action and updates the version.
    /*!
     * \note Thread affinity: StateThread
     */
    void ApplyChange(IAction::TPtr changeAction);

    //! Appends a new record into an appropriate changelog.
    /*!
     * \note Thread affinity: StateThread
     */
    TAsyncChangeLog::TAppendResult::TPtr LogChange(
        const TMetaVersion& version,
        const TSharedRef& changeData);
    
    //! Updates the version so as to switch to a new segment.
    /*!
     * \note Thread affinity: StateThread
     */
    void AdvanceSegment();

    //! Finalizes the current changelog, advances the segment, and creates a new changelog.
    /*!
     * \note Thread affinity: StateThread
     */
    void RotateChangeLog();

private:
    void IncrementRecordCount();
    void ComputeReachableVersion();
    void UpdateVersion(const TMetaVersion& newVersion);
    TCachedAsyncChangeLog::TPtr GetCurrentChangeLog();

    TVoid OnSave(TVoid, TInstant started);
    TVoid OnLoad(TVoid, TInstant started);

    IMetaState::TPtr State;
    TSnapshotStore::TPtr SnapshotStore;
    TChangeLogCache::TPtr ChangeLogCache;

    TActionQueue::TPtr StateQueue;
    TActionQueue::TPtr SnapshotQueue;

    TSpinLock VersionSpinLock;
    TMetaVersion Version;
    TMetaVersion ReachableVersion;

    DECLARE_THREAD_AFFINITY_SLOT(StateThread);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
