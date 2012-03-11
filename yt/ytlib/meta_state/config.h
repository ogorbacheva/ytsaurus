#pragma once

#include "public.h"

#include <ytlib/election/config.h>
#include <ytlib/misc/configurable.h>

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

struct TCellConfig
    : public TConfigurable
{
    //! Master server addresses.
    yvector<Stroka> Addresses;

    //! The current master server id.
    TPeerId Id;

    TCellConfig()
    {
        Register("id", Id)
            .Default(NElection::InvalidPeerId);
        Register("addresses", Addresses)
            .NonEmpty();
    }

    virtual void DoValidate() const
    {
        if (Id == NElection::InvalidPeerId) {
            ythrow yexception() << "Missing peer id";
        }
        if (Id < 0 || Id >= Addresses.ysize()) {
            ythrow yexception() << Sprintf("Id must be in range 0..%d", Addresses.ysize() - 1);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TChangeLogDownloaderConfig
    : public TConfigurable
{
    TDuration LookupTimeout;
    TDuration ReadTimeout;
    i32 RecordsPerRequest;

    TChangeLogDownloaderConfig()
    {
        Register("lookup_timeout", LookupTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::Seconds(5));
        Register("read_timeout", ReadTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::Seconds(10));
        Register("records_per_request", RecordsPerRequest)
            .GreaterThan(0)
            .Default(1024 * 1024);
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TSnapshotDownloaderConfig
    : public TConfigurable
{

    TDuration LookupTimeout;
    TDuration ReadTimeout;
    i32 BlockSize;

    TSnapshotDownloaderConfig()
    {
        Register("lookup_timeout", LookupTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::Seconds(2));
        Register("read_timeout", ReadTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::Seconds(10));
        Register("block_size", BlockSize)
            .GreaterThan(0)
            .Default(32 * 1024 * 1024);
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TSnapshotBuilderConfig
    : public TConfigurable
{
    TDuration RemoteTimeout;
    TDuration LocalTimeout;

    TSnapshotBuilderConfig()
    {
        Register("remote_timeout", RemoteTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::Minutes(1));
        Register("local_timeout", LocalTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::Minutes(1));
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TFollowerPingerConfig
    : public TConfigurable
{
    TDuration PingInterval;
    TDuration RpcTimeout;

    TFollowerPingerConfig()
    {
        Register("ping_interval", PingInterval)
            .GreaterThan(TDuration())
            .Default(TDuration::MilliSeconds(1000));
        Register("rpc_timeout", RpcTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::MilliSeconds(1000));
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TFollowerTrackerConfig
    : public TConfigurable
{
    TDuration PingTimeout;

    TFollowerTrackerConfig()
    {
        Register("ping_timeout", PingTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::MilliSeconds(3000));
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TLeaderCommitterConfig
    : public TConfigurable
{
    TDuration RpcTimeout;
    TDuration MaxBatchDelay;
    int MaxBatchSize;

    TLeaderCommitterConfig()
    {
        Register("rpc_timeout", RpcTimeout)
            .GreaterThan(TDuration())
            .Default(TDuration::Seconds(3));
        Register("max_batch_delay", MaxBatchDelay)
            .Default(TDuration::MilliSeconds(10));
        Register("max_batch_size", MaxBatchSize)
            .Default(10000);
    }
};

////////////////////////////////////////////////////////////////////////////////

//! Describes a configuration of TMetaStateManager.
struct TPersistentStateManagerConfig
    : public TConfigurable
{
    //! A path where changelogs are stored.
    Stroka LogPath;

    //! A path where snapshots are stored.
    Stroka SnapshotPath;

    //! Snapshotting period (measured in number of changes).
    /*!
     *  This is also an upper limit for the number of records in a changelog.
     *
     *  The limit may be violated if the server is under heavy load and
     *  a new snapshot generation request is issued when the previous one is still in progress.
     *  This situation is considered abnormal and a warning is reported.
     *
     *  A special value of -1 means that snapshot creation is switched off.
     */
    i32 MaxChangesBetweenSnapshots;

    //! Maximum time a follower waits for "Sync" request from the leader.
    TDuration SyncTimeout;

    //! Default timeout for RPC requests.
    TDuration RpcTimeout;

    TCellConfigPtr Cell;

    NElection::TElectionManagerConfigPtr Election;

    TChangeLogDownloaderConfigPtr ChangeLogDownloader;

    TSnapshotDownloaderConfigPtr SnapshotDownloader;

    TFollowerPingerConfigPtr FollowerPinger;

    TFollowerTrackerConfigPtr FollowerTracker;

    TLeaderCommitterConfigPtr LeaderCommitter;

    TSnapshotBuilderConfigPtr SnapshotBuilder;

    TPersistentStateManagerConfig()
    {
        Register("log_path", LogPath)
            .NonEmpty();
        Register("snapshot_path", SnapshotPath)
            .NonEmpty();
        Register("max_changes_between_snapshots", MaxChangesBetweenSnapshots)
            .Default(-1)
            .GreaterThanOrEqual(-1);
        Register("sync_timeout", SyncTimeout)
            .Default(TDuration::MilliSeconds(5000));
        Register("rpc_timeout", RpcTimeout)
            .Default(TDuration::MilliSeconds(3000));
        Register("cell", Cell)
            .DefaultNew();
        Register("election", Election)
            .DefaultNew();
        Register("change_log_downloader", ChangeLogDownloader)
            .DefaultNew();
        Register("snapshot_downloader", SnapshotDownloader)
            .DefaultNew();
        Register("follower_pinger", FollowerPinger)
            .DefaultNew();
        Register("follower_tracker", FollowerTracker)
            .DefaultNew();
        Register("leader_committer", LeaderCommitter)
            .DefaultNew();
        Register("snapshot_builder", SnapshotBuilder)
            .DefaultNew();
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
