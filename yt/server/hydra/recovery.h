#pragma once

#include "private.h"

#include <core/concurrency/thread_affinity.h>

#include <core/logging/tagged_logger.h>

#include <ytlib/election/public.h>

#include <ytlib/hydra/version.h>

namespace NYT {
namespace NHydra {

////////////////////////////////////////////////////////////////////////////////

//! Base class for both leader and follower recovery models.
class TRecovery
    : public TRefCounted
{
public:
    TRecovery(
        TDistributedHydraManagerConfigPtr config,
        NElection::TCellManagerPtr cellManager,
        TDecoratedAutomatonPtr decoratedAutomaton,
        IChangelogStorePtr changelogStore,
        ISnapshotStorePtr snapshotStore,
        TEpochContextPtr epochContext);

protected:
    friend class TLeaderRecovery;
    friend class TFollowerRecovery;

    //! Must be derived the the inheritors to control the recovery behavior.
    virtual bool IsLeader() const = 0;

    //! Recovers to the desired state by first loading an appropriate snapshot
    //! and then applying changelogs, if necessary.
    void RecoverToVersion(TVersion targetVersion);

    //! Recovers to the desired version by first loading the given snapshot
    //! and then applying changelogs, if necessary.
    void RecoverToVersionWithSnapshot(TVersion targetVersion, int snapshotId);

    //! Recovers to the desired state by applying changelogs.
    void ReplayChangelogs(TVersion targetVersion, int expectedPrevRecordCount);

    //! Synchronizes the changelog at follower with the leader, i.e.
    //! downloads missing records or truncates redundant ones.
    void SyncChangelog(IChangelogPtr changelog, int changelogId);

    //! Applies records from a given changes up to a given one.
    /*!
     *  The current segment id should match that of #changeLog.
     *  The methods ensures that no mutation is applied twice.
     */
    void ReplayChangelog(IChangelogPtr changelog, int changelogId, int targetRecordId);

    //! Computes the previous record count parameter for a given segment id.
    /*!
     *  First tries to open the corresponding changelog and extract its record count.
     *  If no changelog exists, then tries to consult the corresponding snapshot.
     */
    int ComputePrevRecordCount(int segmentId);


    TDistributedHydraManagerConfigPtr Config_;
    NElection::TCellManagerPtr CellManager_;
    TDecoratedAutomatonPtr DecoratedAutomaton_;
    IChangelogStorePtr ChangelogStore_;
    ISnapshotStorePtr SnapshotStore_;
    TEpochContextPtr EpochContext_;

    TVersion SyncVersion_;

    NLog::TTaggedLogger Logger;

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);

};

////////////////////////////////////////////////////////////////////////////////

//! Drives leader recovery.
class TLeaderRecovery
    : public TRecovery
{
public:
    TLeaderRecovery(
        TDistributedHydraManagerConfigPtr config,
        NElection::TCellManagerPtr cellManager,
        TDecoratedAutomatonPtr decoratedAutomaton,
        IChangelogStorePtr changelogStore,
        ISnapshotStorePtr snapshotStore,
        TEpochContextPtr epochContext);

    //! Performs leader recovery up to a given version.
    TAsyncError Run(TVersion targetVersion);

private:
    void DoRun(TVersion targetVersion);

    virtual bool IsLeader() const;

};

DEFINE_REFCOUNTED_TYPE(TLeaderRecovery)

////////////////////////////////////////////////////////////////////////////////

//! Drives follower recovery.
class TFollowerRecovery
    : public TRecovery
{
public:
    TFollowerRecovery(
        TDistributedHydraManagerConfigPtr config,
        NElection::TCellManagerPtr cellManager,
        TDecoratedAutomatonPtr decoratedAutomaton,
        IChangelogStorePtr changelogStore,
        ISnapshotStorePtr snapshotStore,
        TEpochContextPtr epochContext,
        TVersion syncVersion);

    //! Performs follower recovery bringing the follower up-to-date and synchronized with the leader.
    TAsyncError Run();

    //! Postpones an incoming request for changelog rotation.
    TError PostponeChangelogRotation(TVersion version);

    //! Postpones incoming changes.
    TError PostponeMutations(
        TVersion version,
        const std::vector<TSharedRef>& recordsData);

private:
    struct TPostponedMutation
    {
        DECLARE_ENUM(EType,
            (Mutation)
            (ChangelogRotation)
        );

        EType Type;
        TSharedRef RecordData;

        static TPostponedMutation CreateMutation(const TSharedRef& recordData)
        {
            return TPostponedMutation(EType::Mutation, recordData);
        }

        static TPostponedMutation CreateChangelogRotation()
        {
            return TPostponedMutation(EType::ChangelogRotation, TSharedRef());
        }

        TPostponedMutation(EType type, const TSharedRef& recordData)
            : Type(type)
            , RecordData(recordData)
        { }

    };

    typedef std::vector<TPostponedMutation> TPostponedMutations;

    TSpinLock SpinLock;
    TPostponedMutations PostponedMutations;
    TVersion PostponedVersion;

    void DoRun();

    virtual bool IsLeader() const;

};

DEFINE_REFCOUNTED_TYPE(TFollowerRecovery)

//////////////////////////////////////////////////////////////////////////////

} // namespace NHydra
} // namespace NYT
