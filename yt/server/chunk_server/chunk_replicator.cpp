#include "stdafx.h"
#include "chunk_replicator.h"
#include "chunk_placement.h"
#include "job.h"
#include "chunk.h"
#include "chunk_list.h"
#include "chunk_tree_traversing.h"
#include "private.h"

#include <ytlib/misc/foreach.h>
#include <ytlib/misc/serialize.h>
#include <ytlib/misc/string.h>
#include <ytlib/misc/small_vector.h>
#include <ytlib/misc/protobuf_helpers.h>

#include <ytlib/node_tracker_client/node_directory.h>
#include <ytlib/node_tracker_client/helpers.h>

#include <ytlib/erasure/codec.h>

#include <ytlib/profiling/profiler.h>
#include <ytlib/profiling/timing.h>

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/config.h>
#include <server/cell_master/meta_state_facade.h>

#include <server/chunk_server/chunk_manager.h>
#include <server/chunk_server/node_directory_builder.h>

#include <server/node_tracker_server/node_tracker.h>
#include <server/node_tracker_server/node.h>

#include <server/cypress_server/node.h>

namespace NYT {
namespace NChunkServer {

using namespace NCellMaster;
using namespace NObjectClient;
using namespace NProfiling;
using namespace NChunkClient;
using namespace NNodeTrackerClient;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ChunkServerLogger;
static NProfiling::TProfiler& Profiler = ChunkServerProfiler;

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(const TReplicaStatistics& statistics)
{
    return Sprintf("%d+%d+%d-%d",
        statistics.StoredCount,
        statistics.CachedCount,
        statistics.ReplicationJobCount,
        statistics.RemovalJobCount);
}

////////////////////////////////////////////////////////////////////////////////

TChunkReplicator::TChunkReplicator(
    TChunkManagerConfigPtr config,
    TBootstrap* bootstrap,
    TChunkPlacementPtr chunkPlacement)
    : Config(config)
    , Bootstrap(bootstrap)
    , ChunkPlacement(chunkPlacement)
    , ChunkRefreshDelay(DurationToCpuDuration(config->ChunkRefreshDelay))
{
    YCHECK(config);
    YCHECK(bootstrap);
    YCHECK(chunkPlacement);
}

void TChunkReplicator::Initialize()
{
    RefreshInvoker = New<TPeriodicInvoker>(
        Bootstrap->GetMetaStateFacade()->GetEpochInvoker(EStateThreadQueue::ChunkMaintenance),
        BIND(&TChunkReplicator::OnRefresh, MakeWeak(this)),
        Config->ChunkRefreshPeriod);
    RefreshInvoker->Start();

    RFUpdateInvoker = New<TPeriodicInvoker>(
        Bootstrap->GetMetaStateFacade()->GetEpochInvoker(EStateThreadQueue::ChunkMaintenance),
        BIND(&TChunkReplicator::OnRFUpdate, MakeWeak(this)),
        Config->ChunkRFUpdatePeriod,
        EPeriodicInvokerMode::Manual);
    RFUpdateInvoker->Start();

    auto nodeTracker = Bootstrap->GetNodeTracker();
    FOREACH (auto* node, nodeTracker->GetNodes()) {
        OnNodeRegistered(node);
    }

    auto chunkManager = Bootstrap->GetChunkManager();
    FOREACH (auto* chunk, chunkManager->GetChunks()) {
        ScheduleChunkRefresh(chunk);
        ScheduleRFUpdate(chunk);
    }
}

TJobPtr TChunkReplicator::FindJob(const TJobId& id)
{
    auto it = JobMap.find(id);
    return it == JobMap.end() ? nullptr : it->second;
}

TJobListPtr TChunkReplicator::FindJobList(const TChunkId& id)
{
    auto it = JobListMap.find(id);
    return it == JobListMap.end() ? nullptr : it->second;
}

void TChunkReplicator::ScheduleJobs(
    TNode* node,
    const std::vector<TJobPtr>& runningJobs,
    std::vector<TJobPtr>* jobsToStart,
    std::vector<TJobPtr>* jobsToAbort,
    std::vector<TJobPtr>* jobsToRemove)
{
    ProcessExistingJobs(
        node,
        runningJobs,
        jobsToAbort,
        jobsToRemove);

    if (IsEnabled()) {
        ScheduleNewJobs(node, jobsToStart);
    }

    FOREACH (auto job, *jobsToStart) {
        RegisterJob(job);
    }

    FOREACH (auto job, *jobsToRemove) {
        UnregisterJob(job);
    }
}

void TChunkReplicator::OnNodeRegistered(TNode* node)
{
    node->ChunksToRemove().clear();

    FOREACH (auto& chunksToReplicate, node->ChunksToReplicate()) {
        chunksToReplicate.clear();
    }

    FOREACH (auto replica, node->StoredReplicas()) {
        ScheduleChunkRefresh(replica.GetPtr());
    }
}

void TChunkReplicator::OnNodeUnregistered(TNode* node)
{
    // Make a copy, UnregisterJob will modify the collection.
    auto jobs = node->Jobs();
    FOREACH (auto job, node->Jobs()) {
        UnregisterJob(job);
    }
}

void TChunkReplicator::OnChunkRemoved(TChunk* chunk)
{
    LostChunks_.erase(chunk);
    LostVitalChunks_.erase(chunk);
    UnderreplicatedChunks_.erase(chunk);
    OverreplicatedChunks_.erase(chunk);
    DataMissingChunks_.erase(chunk);
    ParityMissingChunks_.erase(chunk);

    const auto& chunkId = chunk->GetId();
    auto jobList = FindJobList(chunkId);
    if (jobList) {
        // Make a copy, UnregisterJob will modify the collection.
        auto jobs = jobList->Jobs();
        FOREACH (auto job, jobs) {
            UnregisterJob(job);
        }
    }
}

void TChunkReplicator::ScheduleChunkRemoval(TNode* node, const TChunkId& chunkId)
{
    node->ChunksToRemove().insert(chunkId);
    FOREACH (auto& chunksToReplicate, node->ChunksToReplicate()) {
        chunksToReplicate.erase(chunkId);
    }
}

void TChunkReplicator::ScheduleChunkRemoval(TNode* node, TChunkPtrWithIndex chunkWithIndex)
{
    ScheduleChunkRemoval(node, EncodeChunkId(chunkWithIndex));
}

void TChunkReplicator::ProcessExistingJobs(
    TNode* node,
    const std::vector<TJobPtr>& currentJobs,
    std::vector<TJobPtr>* jobsToAbort,
    std::vector<TJobPtr>* jobsToRemove)
{
    const auto& address = node->GetAddress();

    auto chunkManager = Bootstrap->GetChunkManager();
    FOREACH (const auto& job, currentJobs) {
        const auto& jobId = job->GetJobId();
        auto* chunk = chunkManager->FindChunk(job->GetChunkId());

        switch (job->GetState()) {
            case EJobState::Running:
                if (TInstant::Now() - job->GetStartTime() > Config->ChunkReplicator->JobTimeout) {
                    jobsToAbort->push_back(job);
                    LOG_WARNING("Job timed out (JobId: %s, Address: %s, Duration: %s)",
                        ~ToString(jobId),
                        ~address,
                        ~ToString(TInstant::Now() - job->GetStartTime()));
                } else {
                    LOG_INFO("Job is running (JobId: %s, Address: %s)",
                        ~ToString(jobId),
                        ~address);
                }
                break;

            case EJobState::Completed:
            case EJobState::Failed:
            case EJobState::Aborted: {
                jobsToRemove->push_back(job);

                if (chunk) {
                    ScheduleChunkRefresh(chunk);
                }

                switch (job->GetState()) {
                    case EJobState::Completed:
                        LOG_INFO("Job completed (JobId: %s, Address: %s)",
                            ~ToString(jobId),
                            ~address);
                        break;

                    case EJobState::Failed:
                        LOG_WARNING(job->Error(), "Job failed (JobId: %s, Address: %s)",
                            ~ToString(jobId),
                            ~address);
                        break;

                    case EJobState::Aborted:
                        LOG_WARNING(job->Error(), "Job aborted (JobId: %s, Address: %s)",
                            ~ToString(jobId),
                            ~address);
                        break;

                    default:
                        YUNREACHABLE();
                }
                break;
            }


            default:
                YUNREACHABLE();
        }
    }

    // Check for missing jobs
    yhash_set<TJobPtr> currentJobSet(currentJobs.begin(), currentJobs.end());
    std::vector<TJobPtr> missingJobs;
    FOREACH (const auto& job, node->Jobs()) {
        if (currentJobSet.find(job) == currentJobSet.end()) {
            missingJobs.push_back(job);
            LOG_WARNING("Job is missing (JobId: %s, Address: %s)",
                ~ToString(job->GetJobId()),
                ~address);
        }
    }
    FOREACH (const auto& job, missingJobs) {
        UnregisterJob(job);
    }
}

TChunkReplicator::EScheduleFlags TChunkReplicator::ScheduleReplicationJob(
    TNode* sourceNode,
    const TChunkId& chunkId,
    TJobPtr* job)
{
    auto chunkManager = Bootstrap->GetChunkManager();
    auto* chunk = chunkManager->FindChunk(chunkId);
    if (!IsObjectAlive(chunk)) {
        return EScheduleFlags::Purged;
    }

    if (chunk->GetRefreshScheduled()) {
        return EScheduleFlags::Purged;
    }

    auto statistics = GetReplicaStatistics(chunk);

    int replicasNeeded = statistics.ReplicationFactor - (statistics.StoredCount + statistics.ReplicationJobCount);
    if (replicasNeeded <= 0) {
        LOG_TRACE("Chunk %s we're about to replicate has enough replicas",
            ~ToString(chunkId));
        return EScheduleFlags::Purged;
    }

    auto targets = ChunkPlacement->GetReplicationTargets(chunk, replicasNeeded);
    if (targets.empty()) {
        LOG_TRACE("No suitable target nodes for replication (ChunkId: %s)",
            ~ToString(chunkId));
        return EScheduleFlags::None;
    }

    std::vector<Stroka> targetAddresses;
    FOREACH (auto* target, targets) {
        ChunkPlacement->OnSessionHinted(target);
        targetAddresses.push_back(target->GetAddress());
    }

    *job = TJob::CreateReplicate(chunkId, sourceNode, targetAddresses);

    LOG_INFO("Replication job scheduled (JobId: %s, Address: %s, ChunkId: %s, TargetAddresses: [%s])",
        ~ToString((*job)->GetJobId()),
        ~sourceNode->GetAddress(),
        ~ToString(chunkId),
        ~JoinToString(targetAddresses));

    return
        targets.size() == replicasNeeded
        ? EScheduleFlags(EScheduleFlags::Purged | EScheduleFlags::Scheduled)
        : EScheduleFlags(EScheduleFlags::Scheduled);
}

TChunkReplicator::EScheduleFlags TChunkReplicator::ScheduleBalancingJob(
    TNode* sourceNode,
    TChunkPtrWithIndex chunkWithIndex,
    double maxFillCoeff,
    TJobPtr* job)
{
    auto* chunk = chunkWithIndex.GetPtr();
    const auto& chunkId = chunk->GetId();

    if (chunk->GetRefreshScheduled()) {
        return EScheduleFlags::None;
    }

    auto* targetNode = ChunkPlacement->GetBalancingTarget(chunkWithIndex, maxFillCoeff);
    if (!targetNode) {
        LOG_DEBUG("No suitable target nodes for balancing (ChunkId: %s)",
            ~ToString(chunkWithIndex));
        return EScheduleFlags::None;
    }

    ChunkPlacement->OnSessionHinted(targetNode);

    *job = TJob::CreateReplicate(chunkId, sourceNode, targetNode->GetAddress());

    LOG_INFO("Balancing job scheduled (JobId: %s, Address: %s, ChunkId: %s, TargetAddress: %s)",
        ~ToString((*job)->GetJobId()),
        ~sourceNode->GetAddress(),
        ~ToString(chunkId),
        ~targetNode->GetAddress());

    return EScheduleFlags(EScheduleFlags::Purged | EScheduleFlags::Scheduled);
}

TChunkReplicator::EScheduleFlags TChunkReplicator::ScheduleRemovalJob(
    TNode* node,
    const TChunkId& chunkId,
    TJobPtr* job)
{
    auto chunkManager = Bootstrap->GetChunkManager();
    auto* chunk = chunkManager->FindChunk(chunkId);
    if (chunk && chunk->GetRefreshScheduled()) {
        LOG_TRACE("Chunk %s we're about to remove is scheduled for another refresh",
            ~ToString(chunkId));
        return EScheduleFlags::None;
    }

    *job = TJob::CreateRemove(chunkId, node);

    LOG_INFO("Job %s is scheduled on %s: chunk %s will be removed",
        ~ToString((*job)->GetJobId()),
        ~node->GetAddress(),
        ~ToString(chunkId));

    return EScheduleFlags(EScheduleFlags::Purged | EScheduleFlags::Scheduled);
}

void TChunkReplicator::ScheduleNewJobs(
    TNode* node,
    std::vector<TJobPtr>* jobsToStart)
{
    auto scheduleJob = [&] (TJobPtr job) {
        jobsToStart->push_back(job);
        node->ResourceUsage() += job->ResourceLimits();
    };

    // Schedule replication jobs.
    FOREACH (auto& chunksToReplicate, node->ChunksToReplicate()) {
        auto it = chunksToReplicate.begin();
        while (it != chunksToReplicate.end()) {
            if (node->ResourceUsage().replication_slots() >= node->ResourceLimits().replication_slots())
                break;

            auto jt = it++;
            const auto& chunkId = *jt;

            TJobPtr job;
            auto flags = ScheduleReplicationJob(node, chunkId, &job);
            if (flags & EScheduleFlags::Scheduled) {
                scheduleJob(job);
            }
            if (flags & EScheduleFlags::Purged) {
                chunksToReplicate.erase(jt);
            }
        }
    }

    // Schedule balancing jobs.
    double sourceFillCoeff = ChunkPlacement->GetFillCoeff(node);
    double targetFillCoeff = sourceFillCoeff - Config->ChunkReplicator->MinBalancingFillCoeffDiff;
    if (node->ResourceUsage().replication_slots() < node->ResourceLimits().replication_slots() &&
        sourceFillCoeff > Config->ChunkReplicator->MinBalancingFillCoeff &&
        ChunkPlacement->HasBalancingTargets(targetFillCoeff))
    {
        int maxJobs = std::max(0, node->ResourceLimits().replication_slots() - node->ResourceUsage().replication_slots());
        auto chunksToBalance = ChunkPlacement->GetBalancingChunks(node, maxJobs);
        FOREACH (auto chunkWithIndex, chunksToBalance) {
            if (node->ResourceUsage().replication_slots() >= node->ResourceLimits().replication_slots())
                break;

            TJobPtr job;
            auto flags = ScheduleBalancingJob(node, chunkWithIndex, targetFillCoeff, &job);
            if (flags & EScheduleFlags::Scheduled) {
                scheduleJob(job);
            }
        }
    }

    // Schedule removal jobs.
    {
        auto& chunksToRemove = node->ChunksToRemove();
        auto it = chunksToRemove.begin();
        while (it != chunksToRemove.end()) {
            auto jt = it++;
            const auto& chunkId = *jt;

            if (node->ResourceUsage().removal_slots() >= node->ResourceLimits().removal_slots())
                break;

            TJobPtr job;
            auto flags = ScheduleRemovalJob(node, chunkId, &job);
            if (flags & EScheduleFlags::Scheduled) {
                scheduleJob(job);
            }
            if (flags & EScheduleFlags::Purged) {
                chunksToRemove.erase(jt);
            }
        }
    }
}

TReplicaStatistics TChunkReplicator::GetReplicaStatistics(const TChunk* chunk)
{
    TReplicaStatistics result;

    result.ReplicationFactor = chunk->GetReplicationFactor();
    result.StoredCount = static_cast<int>(chunk->StoredReplicas().size());
    result.CachedCount = ~chunk->CachedReplicas() ? static_cast<int>(chunk->CachedReplicas()->size()) : 0;
    result.ReplicationJobCount = 0;
    result.RemovalJobCount = 0;

    if (result.StoredCount == 0) {
        return result;
    }

    auto chunkManager = Bootstrap->GetChunkManager();
    auto jobList = FindJobList(chunk->GetId());
    if (!jobList) {
        return result;
    }

    TSmallSet<Stroka, TypicalReplicationFactor> storedAddresses;
    FOREACH (auto replica, chunk->StoredReplicas()) {
        storedAddresses.insert(replica.GetPtr()->GetAddress());
    }

    FOREACH (const auto& job, jobList->Jobs()) {
        switch (job->GetType()) {
            case EJobType::ReplicateChunk: {
                FOREACH (const auto& address, job->TargetAddresses()) {
                    if (!storedAddresses.count(address)) {
                        ++result.ReplicationJobCount;
                    }
                }
                break;
            }

            case EJobType::RemoveChunk: {
                if (storedAddresses.count(job->GetNode()->GetAddress())) {
                    ++result.RemovalJobCount;
                }
                break;
            }

            default:
                YUNREACHABLE();
        }
    }

    return result;
}

void TChunkReplicator::Refresh(TChunk* chunk)
{
    if (!chunk->IsConfirmed())
        return;

    ResetChunkStatus(chunk);
    if (chunk->IsErasure()) {
        ComputeErasureChunkStatus(chunk);
    } else {
        ComputeRegularChunkStatus(chunk);
    }
}

void TChunkReplicator::ResetChunkStatus(TChunk* chunk)
{
    auto chunkManager = Bootstrap->GetChunkManager();
    FOREACH (auto nodeWithIndex, chunk->StoredReplicas()) {
        auto* node = nodeWithIndex.GetPtr();
        TChunkPtrWithIndex chunkWithIndex(chunk, nodeWithIndex.GetIndex());
        auto chunkId = EncodeChunkId(chunkWithIndex);
        FOREACH (auto& chunksToReplicate, node->ChunksToReplicate()) {
            chunksToReplicate.erase(chunkId);
        }
        node->ChunksToRemove().erase(chunkId);
    }

    LostChunks_.erase(chunk);
    LostVitalChunks_.erase(chunk);
    OverreplicatedChunks_.erase(chunk);
    UnderreplicatedChunks_.erase(chunk);
}

void TChunkReplicator::ComputeRegularChunkStatus(TChunk* chunk)
{
    const auto& chunkId = chunk->GetId();
    auto statistics = GetReplicaStatistics(chunk);
    if (statistics.StoredCount == 0) {
        // Lost!
        YCHECK(LostChunks_.insert(chunk).second);
        if (chunk->GetVital()) {
            YCHECK(LostVitalChunks_.insert(chunk).second);
        }

        LOG_TRACE("Chunk %s is lost", ~ToString(chunkId));
    } else if (statistics.StoredCount - statistics.RemovalJobCount > statistics.ReplicationFactor) {
        // Overreplicated chunk.
        YCHECK(OverreplicatedChunks_.insert(chunk).second);

        // NB: Never start removal jobs if new replicas are on their way.
        if (statistics.ReplicationJobCount > 0) {
            LOG_WARNING("Chunk %s is over-replicated: %s replicas exist but only %d needed, waiting for pending replications to complete",
                ~ToString(chunkId),
                ~ToString(statistics),
                statistics.ReplicationFactor);
            return;
        }

        int redundantCount = statistics.StoredCount - statistics.RemovalJobCount - statistics.ReplicationFactor;
        auto nodes = ChunkPlacement->GetRemovalTargets(TChunkPtrWithIndex(chunk), redundantCount);

        TSmallVector<Stroka, TypicalReplicationFactor> addresses;
        FOREACH (auto* node, nodes) {
            node->ChunksToRemove().insert(chunkId);
            addresses.push_back(node->GetAddress());
        }

        LOG_INFO("Removal of overreplicated chunk scheduled (ChunkId %s, RedundantCount: %d, TargetAddresses: [%s])",
            ~ToString(chunkId),
            redundantCount,
            ~JoinToString(addresses));
    } else if (statistics.StoredCount + statistics.ReplicationJobCount < statistics.ReplicationFactor) {
        // Underreplicated chunk.
        YCHECK(UnderreplicatedChunks_.insert(chunk).second);

        // NB: Never start replication jobs when removal jobs are in progress.
        if (statistics.RemovalJobCount > 0) {
            LOG_DEBUG("Chunk %s is under-replicated: %s replicas exist but %d needed, waiting for pending removals to complete",
                ~ToString(chunkId),
                ~ToString(statistics),
                statistics.ReplicationFactor);
            return;
        }

        auto* node = ChunkPlacement->GetReplicationSource(chunk);

        int priority = ComputeReplicationPriority(statistics);
        node->ChunksToReplicate()[priority].insert(chunkId);

        LOG_INFO("Chunk %s is under-replicated: %s replicas exist but %d needed, replication is scheduled on %s",
            ~ToString(chunkId),
            ~ToString(statistics),
            statistics.ReplicationFactor,
            ~node->GetAddress());
    } else {
        LOG_TRACE("Chunk %s is OK: %s replicas exist and %d needed",
            ~ToString(chunkId),
            ~ToString(statistics),
            statistics.ReplicationFactor);
    }
}

void TChunkReplicator::ComputeErasureChunkStatus(TChunk* chunk)
{
    const auto& chunkId = chunk->GetId();
    auto statistics = GetReplicaStatistics(chunk);

    // Check data and parity parts.
    NErasure::TBlockIndexSet replicaIndexSet(0);
    int replicaCount[NErasure::MaxTotalBlockCount] = {};
    TSmallVector<int, NErasure::MaxTotalBlockCount> overreplicatedIndexes;
    FOREACH (auto replica, chunk->StoredReplicas()) {
        int index = replica.GetIndex();
        if (++replicaCount[index] > 1) {
            overreplicatedIndexes.push_back(index);
        }
        replicaIndexSet |= (1 << index);
    }

    auto* codec = NErasure::GetCodec(chunk->GetErasureCodec());
    int dataBlockCount = codec->GetDataBlockCount();
    int partityBlockCount = codec->GetParityBlockCount();

    auto dataIndexSet = NErasure::TBlockIndexSet((1 << dataBlockCount) - 1);
    auto parityIndexSet = NErasure::TBlockIndexSet(((1 << partityBlockCount) - 1) << dataBlockCount);

    if ((replicaIndexSet & dataIndexSet) != dataIndexSet) {
        // Data is missing.
        YCHECK(DataMissingChunks_.insert(chunk).second);
    }

    if ((replicaIndexSet & parityIndexSet) != parityIndexSet) {
       // Parity is missing.
        YCHECK(ParityMissingChunks_.insert(chunk).second);
    }

    if (replicaIndexSet != dataIndexSet | parityIndexSet) {
        // Something is damaged.
        // TODO(babenko): eliminate this
        NErasure::TBlockIndexList list;
        for (int i = 0; i < dataBlockCount + partityBlockCount; ++i) {
            if (replicaIndexSet & (1 << i)) {
                list.push_back(i);
            }
        }
        if (!codec->CanRepair(list)) {
            // Lost!
            YCHECK(LostChunks_.insert(chunk).second);
            if (chunk->GetVital()) {
                YCHECK(LostVitalChunks_.insert(chunk).second);
            }

            LOG_TRACE("Chunk %s is lost", ~ToString(chunkId));
        }
    }

    // Check for overreplicated parts.
    FOREACH (int index, overreplicatedIndexes) {
        TChunkPtrWithIndex chunkWithIndex(chunk, index);
        int redundantCount = replicaCount[index] - 1;
        auto nodes = ChunkPlacement->GetRemovalTargets(chunkWithIndex, redundantCount);

        TSmallVector<Stroka, TypicalReplicationFactor> addresses;
        FOREACH (auto* node, nodes) {
            node->ChunksToRemove().insert(chunkId);
            addresses.push_back(node->GetAddress());
        }

        LOG_INFO("Removal of overreplicated erasure chunk part scheduled (ChunkId: %s, RedundantCount: %d, TargetAddresses: [%s])",
            ~ToString(chunkWithIndex),
            redundantCount,
            ~JoinToString(addresses));
    }
}

int TChunkReplicator::ComputeReplicationPriority(const TReplicaStatistics& statistics)
{
    YASSERT(statistics.StoredCount > 0);
    return std::min(statistics.StoredCount, ReplicationPriorityCount) - 1;
}

void TChunkReplicator::ScheduleChunkRefresh(const TChunkId& chunkId)
{
    auto chunkManager = Bootstrap->GetChunkManager();
    auto* chunk = chunkManager->FindChunk(chunkId);
    if (IsObjectAlive(chunk)) {
        ScheduleChunkRefresh(chunk);
    }
}

void TChunkReplicator::ScheduleChunkRefresh(TChunk* chunk)
{
    if (!IsObjectAlive(chunk) || chunk->GetRefreshScheduled())
        return;

    TRefreshEntry entry;
    entry.Chunk = chunk;
    entry.When = GetCpuInstant() + ChunkRefreshDelay;
    RefreshList.push_back(entry);
    chunk->SetRefreshScheduled(true);

    auto objectManager = Bootstrap->GetObjectManager();
    objectManager->LockObject(chunk);
}

void TChunkReplicator::OnRefresh()
{
    if (RefreshList.empty()) {
        return;
    }

    auto objectManager = Bootstrap->GetObjectManager();

    int count = 0;
    PROFILE_TIMING ("/incremental_refresh_time") {
        auto chunkManager = Bootstrap->GetChunkManager();
        auto now = GetCpuInstant();
        for (int i = 0; i < Config->MaxChunksPerRefresh; ++i) {
            if (RefreshList.empty())
                break;

            const auto& entry = RefreshList.front();
            if (entry.When > now)
                break;

            auto* chunk = entry.Chunk;
            RefreshList.pop_front();
            chunk->SetRefreshScheduled(false);
            ++count;

            if (IsObjectAlive(chunk)) {
                Refresh(chunk);
            }

            objectManager->UnlockObject(chunk);
        }
    }

    LOG_DEBUG("Incremental chunk refresh completed, %d chunks processed",
        count);
}

bool TChunkReplicator::IsEnabled()
{
    // This method also logs state changes.

    auto chunkManager = Bootstrap->GetChunkManager();
    auto nodeTracker = Bootstrap->GetNodeTracker();

    auto config = Config->ChunkReplicator;
    if (config->MinOnlineNodeCount) {
        int needOnline = config->MinOnlineNodeCount.Get();
        int gotOnline = nodeTracker->GetOnlineNodeCount();
        if (gotOnline < needOnline) {
            if (!LastEnabled || LastEnabled.Get()) {
                LOG_INFO("Chunk replicator disabled: too few online nodes, needed >= %d but got %d",
                    needOnline,
                    gotOnline);
                LastEnabled = false;
            }
            return false;
        }
    }

    int chunkCount = chunkManager->GetChunkCount();
    int lostChunkCount = chunkManager->LostChunks().size();
    if (config->MaxLostChunkFraction && chunkCount > 0) {
        double needFraction = config->MaxLostChunkFraction.Get();
        double gotFraction = (double) lostChunkCount / chunkCount;
        if (gotFraction > needFraction) {
            if (!LastEnabled || LastEnabled.Get()) {
                LOG_INFO("Chunk replicator disabled: too many lost chunks, needed <= %lf but got %lf",
                    needFraction,
                    gotFraction);
                LastEnabled = false;
            }
            return false;
        }
    }

    if (!LastEnabled || !LastEnabled.Get()) {
        LOG_INFO("Chunk replicator enabled");
        LastEnabled = true;
    }

    return true;
}

int TChunkReplicator::GetRefreshListSize() const
{
    return static_cast<int>(RefreshList.size());
}

int TChunkReplicator::GetRFUpdateListSize() const
{
    return static_cast<int>(RFUpdateList.size());
}

void TChunkReplicator::ScheduleRFUpdate(TChunkTree* chunkTree)
{
    switch (chunkTree->GetType()) {
        case EObjectType::Chunk:
            ScheduleRFUpdate(chunkTree->AsChunk());
            break;
        case EObjectType::ChunkList:
            ScheduleRFUpdate(chunkTree->AsChunkList());
            break;
        default:
            YUNREACHABLE();
    }
}

void TChunkReplicator::ScheduleRFUpdate(TChunkList* chunkList)
{
    class TVisitor
        : public IChunkVisitor
    {
    public:
        TVisitor(
            NCellMaster::TBootstrap* bootstrap,
            TChunkReplicatorPtr replicator,
            TChunkList* root)
            : Bootstrap(bootstrap)
            , Replicator(std::move(replicator))
            , Root(root)
        { }

        void Run()
        {
            TraverseChunkTree(Bootstrap, this, Root);
        }

    private:
        TBootstrap* Bootstrap;
        TChunkReplicatorPtr Replicator;
        TChunkList* Root;

        virtual bool OnChunk(
            TChunk* chunk,
            const NChunkClient::NProto::TReadLimit& startLimit,
            const NChunkClient::NProto::TReadLimit& endLimit) override
        {
            UNUSED(startLimit);
            UNUSED(endLimit);

            Replicator->ScheduleRFUpdate(chunk);
            return true;
        }

        virtual void OnError(const TError& error) override
        {
            LOG_ERROR(error, "Error traversing chunk tree for RF update");
        }

        virtual void OnFinish() override
        { }

    };

    New<TVisitor>(Bootstrap, this, chunkList)->Run();
}

void TChunkReplicator::ScheduleRFUpdate(TChunk* chunk)
{
    if (!IsObjectAlive(chunk) || chunk->GetRFUpdateScheduled())
        return;

    RFUpdateList.push_back(chunk);
    chunk->SetRFUpdateScheduled(true);

    auto objectManager = Bootstrap->GetObjectManager();
    objectManager->LockObject(chunk);
}

void TChunkReplicator::OnRFUpdate()
{
    if (RFUpdateList.empty() ||
        !Bootstrap->GetMetaStateFacade()->GetManager()->HasActiveQuorum())
    {
        RFUpdateInvoker->ScheduleNext();
        return;
    }

    // Extract up to GCObjectsPerMutation objects and post a mutation.
    auto chunkManager = Bootstrap->GetChunkManager();
    auto objectManager = Bootstrap->GetObjectManager();
    NProto::TMetaReqUpdateChunkReplicationFactor request;

    PROFILE_TIMING ("/rf_update_time") {
        for (int i = 0; i < Config->MaxChunksPerRFUpdate; ++i) {
            if (RFUpdateList.empty())
                break;

            auto* chunk = RFUpdateList.front();
            RFUpdateList.pop_front();
            chunk->SetRFUpdateScheduled(false);

            if (IsObjectAlive(chunk)) {
                int replicationFactor = ComputeReplicationFactor(chunk);
                if (chunk->GetReplicationFactor() != replicationFactor) {
                    auto* update = request.add_updates();
                    ToProto(update->mutable_chunk_id(), chunk->GetId());
                    update->set_replication_factor(replicationFactor);
                }
            }

            objectManager->UnlockObject(chunk);
        }
    }

    if (request.updates_size() == 0) {
        RFUpdateInvoker->ScheduleNext();
        return;
    }

    LOG_DEBUG("Starting RF update for %d chunks", request.updates_size());

    auto invoker = Bootstrap->GetMetaStateFacade()->GetEpochInvoker();
    chunkManager
        ->CreateUpdateChunkReplicationFactorMutation(request)
        ->OnSuccess(BIND(&TChunkReplicator::OnRFUpdateCommitSucceeded, MakeWeak(this)).Via(invoker))
        ->OnError(BIND(&TChunkReplicator::OnRFUpdateCommitFailed, MakeWeak(this)).Via(invoker))
        ->PostCommit();
}

void TChunkReplicator::OnRFUpdateCommitSucceeded()
{
    LOG_DEBUG("RF update commit succeeded");

    RFUpdateInvoker->ScheduleOutOfBand();
    RFUpdateInvoker->ScheduleNext();
}

void TChunkReplicator::OnRFUpdateCommitFailed(const TError& error)
{
    LOG_WARNING(error, "RF update commit failed");

    RFUpdateInvoker->ScheduleNext();
}

int TChunkReplicator::ComputeReplicationFactor(const TChunk* chunk)
{
    int result = chunk->GetReplicationFactor();

    // Unique number used to distinguish already visited chunk lists.
    auto mark = TChunkList::GenerateVisitMark();

    // BFS queue. Try to avoid allocations.
    TSmallVector<TChunkList*, 64> queue;
    size_t frontIndex = 0;

    auto enqueue = [&] (TChunkList* chunkList) {
        if (chunkList->GetVisitMark() != mark) {
            chunkList->SetVisitMark(mark);
            queue.push_back(chunkList);
        }
    };

    // Put seeds into the queue.
    FOREACH (auto* parent, chunk->Parents()) {
        auto* adjustedParent = FollowParentLinks(parent);
        if (adjustedParent) {
            enqueue(adjustedParent);
        }
    }

    // The main BFS loop.
    while (frontIndex < queue.size()) {
        auto* chunkList = queue[frontIndex++];

        // Examine owners, if any.
        FOREACH (const auto* owningNode, chunkList->OwningNodes()) {
            result = std::max(result, owningNode->GetOwningReplicationFactor());
        }

        // Proceed to parents.
        FOREACH (auto* parent, chunkList->Parents()) {
            auto* adjustedParent = FollowParentLinks(parent);
            if (adjustedParent) {
                enqueue(adjustedParent);
            }
        }
    }

    return result;
}

TChunkList* TChunkReplicator::FollowParentLinks(TChunkList* chunkList)
{
    while (chunkList->OwningNodes().empty()) {
        const auto& parents = chunkList->Parents();
        size_t parentCount = parents.size();
        if (parentCount == 0) {
            return nullptr;
        }
        if (parentCount > 1) {
            break;
        }
        chunkList = *parents.begin();
    }
    return chunkList;
}

void TChunkReplicator::RegisterJob(TJobPtr job)
{
    LOG_INFO("Job registered (JobId: %s, JobType: %s, Address: %s)",
        ~ToString(job->GetJobId()),
        ~job->GetType().ToString(),
        ~job->GetNode()->GetAddress());

    YCHECK(JobMap.insert(std::make_pair(job->GetJobId(), job)).second);
    
    job->GetNode()->AddJob(job);

    const auto& chunkId = job->GetChunkId();
    auto jobList = FindJobList(chunkId);
    if (!jobList) {
        jobList = New<TJobList>(chunkId);
        YCHECK(JobListMap.insert(std::make_pair(chunkId, jobList)).second);
    }
    jobList->AddJob(job);
}

void TChunkReplicator::UnregisterJob(TJobPtr job)
{
    LOG_INFO("Job unregistered (JobId: %s, Address: %s)",
        ~ToString(job->GetJobId()),
        ~job->GetNode()->GetAddress());
    
    YCHECK(JobMap.erase(job->GetJobId()) == 1);

    job->GetNode()->RemoveJob(job);

    const auto& chunkId = job->GetChunkId();
    auto jobList = FindJobList(chunkId);
    YCHECK(jobList);
    jobList->RemoveJob(job);
    if (jobList->Jobs().empty()) {
        YCHECK(JobListMap.erase(chunkId) == 1);
    }

    ScheduleChunkRefresh(job->GetChunkId());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
