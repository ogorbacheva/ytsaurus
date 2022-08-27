#include "replicated_table_tracker.h"

#include "chaos_manager.h"
#include "chaos_slot.h"
#include "replication_card.h"

#include <yt/yt/server/lib/tablet_server/replicated_table_tracker.h>

#include <yt/yt/ytlib/chaos_client/chaos_node_service_proxy.h>

#include <yt/yt/client/chaos_client/helpers.h>
#include <yt/yt/client/chaos_client/replication_card.h>

#include <yt/yt/client/transaction_client/helpers.h>
#include <yt/yt/client/transaction_client/timestamp_provider.h>

namespace NYT::NChaosNode {

using namespace NTabletServer;
using namespace NTabletClient;
using namespace NChaosClient;
using namespace NTransactionClient;

////////////////////////////////////////////////////////////////////////////////

class TReplicatedTableTrackerHost
    : public IReplicatedTableTrackerHost
{
public:
    TReplicatedTableTrackerHost(IChaosSlotPtr slot)
        : Slot_(std::move(slot))
    { }

    TFuture<TReplicatedTableTrackerSnapshot> GetSnapshot() override
    {
        YT_VERIFY(LoadingFromSnapshotRequested());

        return BIND([slot = Slot_, host = MakeStrong(this)] {
            TReplicatedTableTrackerSnapshot snapshot;

            const auto& chaosManager = slot->GetChaosManager();
            for (auto [_, replicationCard] : chaosManager->ReplicationCards()) {
                if (replicationCard->IsMigrated()) {
                    continue;
                }

                snapshot.ReplicatedTables.push_back(TReplicatedTableData{
                    .Id = replicationCard->GetId(),
                    .Options = replicationCard->GetReplicatedTableOptions()
                });

                for (const auto& [replicaId, replica] : replicationCard->Replicas()) {
                    snapshot.Replicas.push_back(TReplicaData{
                        .TableId = replicationCard->GetId(),
                        .Id = replicaId,
                        .Mode = replica.Mode,
                        .Enabled = IsReplicaEnabled(replica.State),
                        .ClusterName = replica.ClusterName,
                        .TablePath = replica.ReplicaPath,
                        .TrackingEnabled = replica.EnableReplicatedTableTracker,
                    });
                }
            }

            host->LoadingFromSnapshotRequested_.store(false);

            return snapshot;
        })
            .AsyncVia(GetAutomatonInvoker())
            .Run();
    }

    bool LoadingFromSnapshotRequested() const override
    {
        return LoadingFromSnapshotRequested_.load();
    }

    void RequestLoadingFromSnapshot() override
    {
        LoadingFromSnapshotRequested_.store(true);
    }

    TFuture<TReplicaLagTimes> ComputeReplicaLagTimes(
        std::vector<NTabletClient::TTableReplicaId> replicaIds) override
    {
        return BIND([slot = Slot_, replicaIds = std::move(replicaIds)] {
            auto latestTimestamp = slot->GetTimestampProvider()->GetLatestTimestamp();
            const auto& chaosManager = slot->GetChaosManager();

            TReplicaLagTimes results;
            results.reserve(replicaIds.size());

            for (auto replicaId : replicaIds) {
                auto replicationCardId = ReplicationCardIdFromReplicaId(replicaId);
                auto *replicationCard = chaosManager->FindReplicationCard(replicationCardId);
                if (!replicationCard) {
                    continue;
                }

                auto* replica = replicationCard->FindReplica(replicaId);
                if (replica) {
                    auto minTimestamp = GetReplicationProgressMinTimestamp(replica->ReplicationProgress);
                    results.emplace_back(replicaId, TimestampDiffToDuration(minTimestamp, latestTimestamp).second);
                }
            }

            return results;
        })
            .AsyncVia(GetAutomatonInvoker())
            .Run();
    }

    NApi::IClientPtr CreateClusterClient(const TString& clusterName) override
    {
        return Slot_->CreateClusterClient(clusterName);
    }

    TFuture<TApplyChangeReplicaCommandResults> ApplyChangeReplicaModeCommands(
        std::vector<TChangeReplicaModeCommand> commands) override
    {
        return BIND([slot = Slot_, commands = std::move(commands)] {
            std::vector<TFuture<void>> futures;
            futures.reserve(commands.size());

            for (const auto& command : commands) {
                NChaosClient::NProto::TReqAlterTableReplica req;
                ToProto(req.mutable_replica_id(), command.ReplicaId);                
                ToProto(req.mutable_replication_card_id(), ReplicationCardIdFromReplicaId(command.ReplicaId));                
                req.set_mode(static_cast<int>(command.TargetMode));
                futures.push_back(slot->GetChaosManager()->ExecuteAlterTableReplica(req));
            }

            return AllSet(std::move(futures));
        })
            .AsyncVia(GetAutomatonInvoker())
            .Run();
    }

    void SubscribeReplicatedTableCreated(TCallback<void(TReplicatedTableData)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicatedTableCreated(std::move(callback));
    }

    void SubscribeReplicatedTableDestroyed(TCallback<void(NTableClient::TTableId)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicatedTableDestroyed(std::move(callback));
    }

    void SubscribeReplicatedTableOptionsUpdated(
        TCallback<void(NTableClient::TTableId, TReplicatedTableOptionsPtr)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicatedTableOptionsUpdated(std::move(callback));
    }

    void SubscribeReplicaCreated(TCallback<void(TReplicaData)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicaCreated(std::move(callback));
    }

    void SubscribeReplicaDestroyed(
        TCallback<void(NTabletClient::TTableReplicaId)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicaDestroyed(std::move(callback));
    }

    void SubscribeReplicaModeUpdated(
        TCallback<void(NTabletClient::TTableReplicaId, NTabletClient::ETableReplicaMode)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicaModeUpdated(std::move(callback));
    }

    void SubscribeReplicaEnablementUpdated(
        TCallback<void(NTabletClient::TTableReplicaId, bool)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicaEnablementUpdated(std::move(callback));
    }

    void SubscribeReplicaTrackingPolicyUpdated(
        TCallback<void(NTabletClient::TTableReplicaId, bool)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicaTrackingPolicyUpdated(std::move(callback));
    }

    void SubscribeReplicationCollocationUpdated(TCallback<void(TTableCollocationData)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicationCollocationUpdated(std::move(callback));
    }

    void SubscribeReplicationCollocationDestroyed(
        TCallback<void(NTableClient::TTableCollocationId)> callback) override
    {
        Slot_->GetChaosManager()->SubscribeReplicationCollocationDestroyed(std::move(callback));
    }

    void SubscribeConfigChanged(TCallback<void(TDynamicReplicatedTableTrackerConfigPtr)> callback) override
    {
        Slot_->SubscribeReplicatedTableTrackerConfigChanged(BIND(
            [callback = std::move(callback)] (
                const TDynamicReplicatedTableTrackerConfigPtr& /* oldConfig */,
                const TDynamicReplicatedTableTrackerConfigPtr& newConfig)
            {
                callback(newConfig);
            })
            .Via(GetAutomatonInvoker())
        );
    }

private:
    IChaosSlotPtr const Slot_;

    std::atomic<bool> LoadingFromSnapshotRequested_ = false;


    IInvokerPtr GetAutomatonInvoker() const
    {
        return Slot_->GetEpochAutomatonInvoker(EAutomatonThreadQueue::ReplicatedTableTracker);
    }
};

////////////////////////////////////////////////////////////////////////////////

IReplicatedTableTrackerHostPtr CreateReplicatedTableTrackerHost(IChaosSlotPtr slot)
{
    return New<TReplicatedTableTrackerHost>(std::move(slot));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChaosNode
