#include "stdafx.h"
#include "partition_balancer.h"
#include "config.h"
#include "tablet_slot.h"
#include "tablet_slot_manager.h"
#include "tablet_manager.h"
#include "tablet.h"
#include "partition.h"
#include "store.h"
#include "chunk_store.h"
#include "private.h"

#include <core/concurrency/scheduler.h>

#include <core/logging/log.h>

#include <ytlib/tablet_client/config.h>

#include <ytlib/new_table_client/unversioned_row.h>
#include <ytlib/new_table_client/samples_fetcher.h>

#include <ytlib/node_tracker_client/node_directory.h>

#include <ytlib/chunk_client/chunk_spec.h>
#include <ytlib/chunk_client/chunk_service_proxy.h>

#include <ytlib/api/client.h>

#include <server/hydra/hydra_manager.h>
#include <server/hydra/mutation.h>

#include <server/tablet_node/tablet_manager.pb.h>

#include <server/cell_node/bootstrap.h>

namespace NYT {
namespace NTabletNode {

using namespace NConcurrency;
using namespace NHydra;
using namespace NVersionedTableClient;
using namespace NNodeTrackerClient;
using namespace NChunkClient;
using namespace NTabletNode::NProto;

////////////////////////////////////////////////////////////////////////////////

class TPartitionBalancer
    : public TRefCounted
{
public:
    TPartitionBalancer(
        TPartitionBalancerConfigPtr config,
        NCellNode::TBootstrap* bootstrap)
        : Config_(config)
        , Bootstrap_(bootstrap)
    { }

    void Start()
    {
        auto tabletSlotManager = Bootstrap_->GetTabletSlotManager();
        tabletSlotManager->SubscribeScanSlot(BIND(&TPartitionBalancer::OnScanSlot, MakeStrong(this)));
    }

private:
    TPartitionBalancerConfigPtr Config_;
    NCellNode::TBootstrap* Bootstrap_;


    void OnScanSlot(TTabletSlotPtr slot)
    {
        if (slot->GetAutomatonState() != EPeerState::Leading)
            return;

        auto tabletManager = slot->GetTabletManager();
        for (const auto& pair : tabletManager->Tablets()) {
            auto* tablet = pair.second;
            ScanTablet(slot, tablet);
        }
    }

    void ScanTablet(TTabletSlotPtr slot, TTablet* tablet)
    {
        for (const auto& partition : tablet->Partitions()) {
            ScanPartition(slot, partition.get());
        }
    }

    void ScanPartition(TTabletSlotPtr slot, TPartition* partition)
    {
        i64 dataSize = partition->GetUncompressedDataSize();
        
        auto* tablet = partition->GetTablet();
        int partitionCount = static_cast<int>(tablet->Partitions().size());

        const auto& config = tablet->GetConfig();

        if (dataSize >  config->MaxPartitionDataSize) {
            int splitFactor = static_cast<int>(std::min(
                dataSize / config->DesiredPartitionDataSize + 1,
                static_cast<i64>(config->MaxPartitionCount - partitionCount)));
            if (splitFactor > 1) {
                RunSplit(partition, splitFactor);
            }
        }
        
        if (dataSize + tablet->GetEden()->GetUncompressedDataSize() < config->MinPartitionDataSize && partitionCount > 1) {
            int firstPartitionIndex = partition->GetIndex();
            int lastPartitionIndex = firstPartitionIndex + 1;

            if (lastPartitionIndex == partitionCount) {
                --firstPartitionIndex;
                --lastPartitionIndex;
            }

            RunMerge(partition, firstPartitionIndex, lastPartitionIndex);
        }

        if (partition->GetSamplingRequestTime() > partition->GetSamplingTime() &&
            partition->GetSamplingTime() < TInstant::Now() - Config_->ResamplingPeriod)
        {
            RunSample(partition);
        }
    }


    void RunSplit(TPartition* partition, int splitFactor)
    {
        if (partition->GetState() != EPartitionState::Normal)
            return;

        for (auto store : partition->Stores()) {
            if (store->GetState() != EStoreState::Persistent)
                return;
        }

        partition->SetState(EPartitionState::Splitting);

        BIND(&TPartitionBalancer::DoRunSplit, MakeStrong(this))
            .AsyncVia(partition->GetTablet()->GetEpochAutomatonInvoker())
            .Run(partition, splitFactor);
    }

    void DoRunSplit(TPartition* partition, int splitFactor)
    {
        auto Logger = BuildLogger(partition);

        auto* tablet = partition->GetTablet();
        auto slot = tablet->GetSlot();
        auto hydraManager = slot->GetHydraManager();

        LOG_INFO("Partition is eligible for split (SplitFactor: %v)",
            splitFactor);

        try {
            auto samples = GetPartitionSamples(partition, Config_->MaxPartitioningSampleCount);
            int sampleCount = static_cast<int>(samples.size());
            int minSampleCount = std::max(Config_->MinPartitioningSampleCount, splitFactor);
            if (sampleCount < minSampleCount) {
                THROW_ERROR_EXCEPTION("Too few samples fetched: %v < %v",
                    sampleCount,
                    minSampleCount);
            }

            std::vector<TOwningKey> pivotKeys;
            pivotKeys.push_back(partition->GetPivotKey());
            for (int i = 0; i < splitFactor; ++i) {
                int j = static_cast<int>(i * sampleCount / splitFactor);
                const auto& key = samples[j];
                if (key > pivotKeys.back()) {
                    pivotKeys.push_back(key);
                }
            }

            if (pivotKeys.size() < 2) {
                THROW_ERROR_EXCEPTION("No valid pivot keys can be obtained from samples");
            }

            TReqSplitPartition request;
            ToProto(request.mutable_tablet_id(), tablet->GetId());
            ToProto(request.mutable_pivot_keys(), pivotKeys);
            CreateMutation(hydraManager, request)
                ->Commit();
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "Partitioning aborted");
            partition->SetState(EPartitionState::Normal);
        }
    }


    void RunMerge(
        TPartition* partition,
        int firstPartitionIndex,
        int lastPartitionIndex)
    {
        auto* tablet = partition->GetTablet();

        for (int index = firstPartitionIndex; index <= lastPartitionIndex; ++index) {
            if (tablet->Partitions()[index]->GetState() != EPartitionState::Normal)
                return;
        }

        for (int index = firstPartitionIndex; index <= lastPartitionIndex; ++index) {
            tablet->Partitions()[index]->SetState(EPartitionState::Merging);
        }

        auto Logger = BuildLogger(partition);

        LOG_INFO("Partition is eligible for merge");

        auto slot = tablet->GetSlot();
        auto hydraManager = slot->GetHydraManager();

        TReqMergePartitions request;
        ToProto(request.mutable_tablet_id(), tablet->GetId());
        ToProto(request.mutable_pivot_key(), tablet->Partitions()[firstPartitionIndex]->GetPivotKey());
        request.set_partition_count(lastPartitionIndex - firstPartitionIndex + 1);
        CreateMutation(hydraManager, request)
            ->Commit();
    }



    void RunSample(TPartition* partition)
    {
        if (partition->GetState() != EPartitionState::Normal)
            return;

        partition->SetState(EPartitionState::Sampling);

        BIND(&TPartitionBalancer::DoRunSample, MakeStrong(this))
            .AsyncVia(partition->GetTablet()->GetEpochAutomatonInvoker())
            .Run(partition);
    }

    void DoRunSample(TPartition* partition)
    {
        auto Logger = BuildLogger(partition);

        auto* tablet = partition->GetTablet();
        auto config = tablet->GetConfig();

        auto slot = tablet->GetSlot();
        auto hydraManager = slot->GetHydraManager();

        LOG_INFO("Sampling partition (DesiredSampleCount: %v)",
            config->SamplesPerPartition);

        try {
            auto samples = GetPartitionSamples(partition, config->SamplesPerPartition - 1);
            samples.erase(
                std::unique(samples.begin(), samples.end()),
                samples.end());

            TReqUpdatePartitionSampleKeys request;
            ToProto(request.mutable_tablet_id(), tablet->GetId());
            ToProto(request.mutable_pivot_key(), partition->GetPivotKey());
            ToProto(request.mutable_sample_keys(), samples);
            CreateMutation(hydraManager, request)
                ->Commit();
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "Partition sampling aborted");
        }

        partition->SetState(EPartitionState::Normal);
        // NB: Update the timestamp even in case of failure to prevent
        // repeating unsuccessful samplings too rapidly.
        partition->SetSamplingTime(TInstant::Now());
    }


    std::vector<TOwningKey> GetPartitionSamples(
        TPartition* partition,
        int maxSampleCount)
    {
        YCHECK(partition->GetIndex() != TPartition::EdenIndex);

        if (maxSampleCount == 0) {
            return std::vector<TOwningKey>();
        }

        auto Logger = BuildLogger(partition);

        auto* tablet = partition->GetTablet();

        auto nodeDirectory = New<TNodeDirectory>();

        auto fetcher = New<TSamplesFetcher>(
            Config_->SamplesFetcher,
            maxSampleCount,
            tablet->KeyColumns(),
            nodeDirectory,
            GetCurrentInvoker(),
            Logger);

        {
            TChunkServiceProxy proxy(Bootstrap_->GetMasterClient()->GetMasterChannel());
            auto req = proxy.LocateChunks();

            yhash_map<TChunkId, TChunkStorePtr> storeMap;

            auto addStore = [&] (IStorePtr store) {
                if (store->GetType() != EStoreType::Chunk)
                    return;

                if (store->GetMaxKey() <= partition->GetPivotKey() ||
                    store->GetMinKey() >= partition->GetNextPivotKey())
                    return;

                const auto& chunkId = store->GetId();
                YCHECK(storeMap.insert(std::make_pair(chunkId, store->AsChunk())).second);
                ToProto(req->add_chunk_ids(), chunkId);
            };

            auto addStores = [&] (const yhash_set<IStorePtr>& stores) {
                for (auto store : stores) {
                    addStore(store);
                }
            };

            addStores(partition->Stores());
            addStores(tablet->GetEden()->Stores());

            LOG_INFO("Locating partition chunks (ChunkCount: %v)",
                storeMap.size());

            auto rsp = WaitFor(req->Invoke());
            THROW_ERROR_EXCEPTION_IF_FAILED(*rsp);

            LOG_INFO("Partition chunks located");

            nodeDirectory->MergeFrom(rsp->node_directory());

            for (const auto& chunkInfo : rsp->chunks()) {
                auto chunkId = FromProto<TChunkId>(chunkInfo.chunk_id());
                auto storeIt = storeMap.find(chunkId);
                YCHECK(storeIt != storeMap.end());
                auto store = storeIt->second;
                auto chunkSpec = New<TRefCountedChunkSpec>();
                chunkSpec->mutable_chunk_id()->CopyFrom(chunkInfo.chunk_id());
                chunkSpec->mutable_replicas()->MergeFrom(chunkInfo.replicas());
                chunkSpec->mutable_chunk_meta()->CopyFrom(store->GetChunkMeta());
                fetcher->AddChunk(chunkSpec);
            }
        }

        {
            auto result = WaitFor(fetcher->Fetch());
            THROW_ERROR_EXCEPTION_IF_FAILED(result);
        }

        auto samples = fetcher->GetSamples();
        samples.erase(
            std::remove_if(
                samples.begin(),
                samples.end(),
                [&] (const TOwningKey& key) {
                    return key <= partition->GetPivotKey() || key >= partition->GetNextPivotKey();
                }),
            samples.end());

        std::sort(samples.begin(), samples.end());
        return samples;
    }


    static NLog::TLogger BuildLogger(TPartition* partition)
    {
        NLog::TLogger logger(TabletNodeLogger);
        logger.AddTag("TabletId: %v, PartitionKeys: %v .. %v",
            partition->GetTablet()->GetId(),
            partition->GetPivotKey(),
            partition->GetNextPivotKey());
        return logger;
    }

};

void StartPartitionBalancer(
    TPartitionBalancerConfigPtr config,
    NCellNode::TBootstrap* bootstrap)
{
    New<TPartitionBalancer>(config, bootstrap)->Start();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
