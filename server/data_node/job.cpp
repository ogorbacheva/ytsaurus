#include "job.h"
#include "private.h"
#include "chunk_block_manager.h"
#include "chunk.h"
#include "chunk_store.h"
#include "config.h"
#include "journal_chunk.h"
#include "journal_dispatcher.h"
#include "location.h"
#include "master_connector.h"

#include <yt/server/cell_node/bootstrap.h>
#include <yt/server/cell_node/config.h>

#include <yt/server/hydra/changelog.h>

#include <yt/server/job_agent/job.h>

#include <yt/ytlib/api/client.h>

#include <yt/ytlib/chunk_client/block_cache.h>
#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/ytlib/chunk_client/chunk_writer.h>
#include <yt/ytlib/chunk_client/erasure_reader.h>
#include <yt/ytlib/chunk_client/erasure_repair.h>
#include <yt/ytlib/chunk_client/job.pb.h>
#include <yt/ytlib/chunk_client/replication_reader.h>
#include <yt/ytlib/chunk_client/replication_writer.h>

#include <yt/ytlib/job_tracker_client/job.pb.h>

#include <yt/ytlib/node_tracker_client/helpers.h>
#include <yt/ytlib/node_tracker_client/node_directory.h>

#include <yt/ytlib/object_client/helpers.h>

#include <yt/core/actions/cancelable_context.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/erasure/codec.h>

#include <yt/core/logging/log.h>

#include <yt/core/misc/protobuf_helpers.h>
#include <yt/core/misc/string.h>

namespace NYT {
namespace NDataNode {

using namespace NObjectClient;
using namespace NNodeTrackerClient;
using namespace NJobTrackerClient;
using namespace NJobAgent;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NCellNode;
using namespace NNodeTrackerClient::NProto;
using namespace NJobTrackerClient::NProto;
using namespace NConcurrency;
using namespace NYson;

using NNodeTrackerClient::TNodeDescriptor;
using NYT::ToProto;
using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

class TChunkJobBase
    : public IJob
{
public:
    DEFINE_SIGNAL(void(const TNodeResources& resourcesDelta), ResourcesUpdated);

public:
    TChunkJobBase(
        const TJobId& jobId,
        const TJobSpec& jobSpec,
        const TNodeResources& resourceLimits,
        TDataNodeConfigPtr config,
        TBootstrap* bootstrap)
        : JobId_(jobId)
        , JobSpec_(jobSpec)
        , Config_(config)
        , Bootstrap_(bootstrap)
        , ResourceLimits_(resourceLimits)
    {
        Logger.AddTag("JobId: %v, JobType: %v",
            JobId_,
            GetType());
    }

    virtual void Start() override
    {
        JobState_ = EJobState::Running;
        JobPhase_ = EJobPhase::Running;
        JobFuture_ = BIND(&TChunkJobBase::GuardedRun, MakeStrong(this))
            .AsyncVia(Bootstrap_->GetControlInvoker())
            .Run();
    }

    virtual void Abort(const TError& error) override
    {
        switch (JobState_) {
            case EJobState::Waiting:
                SetAborted(error);
                return;

            case EJobState::Running:
                JobFuture_.Cancel();
                SetAborted(error);
                return;

            default:
                return;
        }
    }

    virtual const TJobId& GetId() const override
    {
        return JobId_;
    }

    virtual const TOperationId& GetOperationId() const override
    {
        return NullOperationId;
    }

    virtual EJobType GetType() const override
    {
        return EJobType(JobSpec_.type());
    }

    virtual const TJobSpec& GetSpec() const override
    {
        return JobSpec_;
    }

    virtual EJobState GetState() const override
    {
        return JobState_;
    }

    virtual EJobPhase GetPhase() const override
    {
        return JobPhase_;
    }

    virtual TNodeResources GetResourceUsage() const override
    {
        return ResourceLimits_;
    }

    virtual void SetResourceUsage(const TNodeResources& /*newUsage*/) override
    {
        Y_UNREACHABLE();
    }

    virtual TJobResult GetResult() const override
    {
        return Result_;
    }

    virtual void SetResult(const TJobResult& /*result*/) override
    {
        Y_UNREACHABLE();
    }

    virtual double GetProgress() const override
    {
        return Progress_;
    }

    virtual void SetProgress(double value) override
    {
        Progress_ = value;
    }

    virtual TYsonString GetStatistics() const override
    {
        return TYsonString();
    }

    virtual void SetStatistics(const TYsonString& /*statistics*/) override
    {
        Y_UNREACHABLE();
    }

    virtual TNullable<TDuration> GetPrepareDuration() const override
    {
        return Null;
    }

    virtual TNullable<TDuration> GetDownloadDuration() const override
    {
        return Null;
    }

    virtual TNullable<TDuration> GetExecDuration() const override
    {
        return Null;
    }

    virtual TInstant GetStatisticsLastSendTime() const override
    {
        Y_UNREACHABLE();
    }

    virtual void ResetStatisticsLastSendTime() override
    {
        Y_UNREACHABLE();
    }

    virtual std::vector<TChunkId> DumpInputContext() override
    {
        THROW_ERROR_EXCEPTION("Input context dumping is not supported");
    }

    virtual TString GetStderr() override
    {
        THROW_ERROR_EXCEPTION("Getting stderr is not supported");
    }

    virtual TYsonString StraceJob() override
    {
        THROW_ERROR_EXCEPTION("Stracing is not supported");
    }

    virtual void SignalJob(const TString& /*signalName*/) override
    {
        THROW_ERROR_EXCEPTION("Signaling is not supported");
    }

    virtual TYsonString PollJobShell(const TYsonString& /*parameters*/) override
    {
        THROW_ERROR_EXCEPTION("Job shell is not supported");
    }

    virtual void Interrupt() override
    {
        THROW_ERROR_EXCEPTION("Interrupting is not supported");
    }

    virtual void OnJobPrepared() override
    {
        Y_UNREACHABLE();
    }

    virtual void ReportStatistics(TJobStatistics&&) override
    {
        Y_UNREACHABLE();
    }

protected:
    const TJobId JobId_;
    const TJobSpec JobSpec_;
    const TDataNodeConfigPtr Config_;
    TBootstrap* const Bootstrap_;

    TNodeResources ResourceLimits_;

    NLogging::TLogger Logger = DataNodeLogger;

    EJobState JobState_ = EJobState::Waiting;
    EJobPhase JobPhase_ = EJobPhase::Created;

    double Progress_ = 0.0;

    TFuture<void> JobFuture_;

    TJobResult Result_;


    virtual void DoRun() = 0;


    void GuardedRun()
    {
        LOG_INFO("Job started");
        try {
            DoRun();
        } catch (const std::exception& ex) {
            SetFailed(ex);
            return;
        }
        SetCompleted();
    }

    void SetCompleted()
    {
        LOG_INFO("Job completed");
        Progress_ = 1.0;
        DoSetFinished(EJobState::Completed, TError());
    }

    void SetFailed(const TError& error)
    {
        LOG_ERROR(error, "Job failed");
        DoSetFinished(EJobState::Failed, error);
    }

    void SetAborted(const TError& error)
    {
        LOG_INFO(error, "Job aborted");
        DoSetFinished(EJobState::Aborted, error);
    }

    IChunkPtr GetLocalChunkOrThrow(const TChunkId& chunkId, int mediumIndex)
    {
        auto chunkStore = Bootstrap_->GetChunkStore();
        return chunkStore->GetChunkOrThrow(chunkId, mediumIndex);
    }

private:
    void DoSetFinished(EJobState finalState, const TError& error)
    {
        if (JobState_ != EJobState::Running && JobState_ != EJobState::Waiting)
            return;

        JobPhase_ = EJobPhase::Finished;
        JobState_ = finalState;
        ToProto(Result_.mutable_error(), error);
        auto deltaResources = ZeroNodeResources() - ResourceLimits_;
        ResourceLimits_ = ZeroNodeResources();
        JobFuture_.Reset();
        ResourcesUpdated_.Fire(deltaResources);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TChunkRemovalJob
    : public TChunkJobBase
{
public:
    TChunkRemovalJob(
        const TJobId& jobId,
        const TJobSpec& jobSpec,
        const TNodeResources& resourceLimits,
        TDataNodeConfigPtr config,
        TBootstrap* bootstrap)
        : TChunkJobBase(
            jobId,
            std::move(jobSpec),
            resourceLimits,
            config,
            bootstrap)
        , JobSpecExt_(JobSpec_.GetExtension(TRemoveChunkJobSpecExt::remove_chunk_job_spec_ext))
    { }

private:
    const TRemoveChunkJobSpecExt JobSpecExt_;


    virtual void DoRun() override
    {
        auto chunkId = FromProto<TChunkId>(JobSpecExt_.chunk_id());
        int mediumIndex = JobSpecExt_.medium_index();

        LOG_INFO("Chunk removal job started (ChunkId: %v, MediumIndex: %v)",
            chunkId,
            mediumIndex);

        auto chunk = GetLocalChunkOrThrow(chunkId, mediumIndex);
        auto chunkStore = Bootstrap_->GetChunkStore();
        WaitFor(chunkStore->RemoveChunk(chunk))
            .ThrowOnError();

        // Wait for the removal notification to be delivered to master.
        // Cf. YT-6532.
        // Once we switch from push replication to pull, this code is likely
        // to appear in TReplicateChunkJob as well.
        LOG_INFO("Waiting for heartbeat barrier");
        const auto& masterConnector = Bootstrap_->GetMasterConnector();
        WaitFor(masterConnector->GetHeartbeatBarrier(CellTagFromId(chunkId)))
            .ThrowOnError();
    }
};

////////////////////////////////////////////////////////////////////////////////

class TChunkReplicationJob
    : public TChunkJobBase
{
public:
    TChunkReplicationJob(
        const TJobId& jobId,
        const TJobSpec& jobSpec,
        const TNodeResources& resourceLimits,
        TDataNodeConfigPtr config,
        TBootstrap* bootstrap)
        : TChunkJobBase(
            jobId,
            std::move(jobSpec),
            resourceLimits,
            config,
            bootstrap)
        , JobSpecExt_(JobSpec_.GetExtension(TReplicateChunkJobSpecExt::replicate_chunk_job_spec_ext))
    { }

private:
    const TReplicateChunkJobSpecExt JobSpecExt_;

    virtual void DoRun() override
    {
        auto chunkId = FromProto<TChunkId>(JobSpecExt_.chunk_id());
        int sourceMediumIndex = JobSpecExt_.source_medium_index();
        auto targetReplicas = FromProto<TChunkReplicaList>(JobSpecExt_.target_replicas());
        auto nodeDirectory = New<NNodeTrackerClient::TNodeDirectory>();
        nodeDirectory->MergeFrom(JobSpecExt_.node_directory());

        LOG_INFO("Chunk replication job started (ChunkId: %v, SourceMediumIndex: %v, TargetReplicas: %v)",
            chunkId,
            sourceMediumIndex,
            MakeFormattableRange(targetReplicas, TChunkReplicaAddressFormatter(nodeDirectory)));

        // Find chunk on the highest priority medium.
        auto chunk = GetLocalChunkOrThrow(chunkId, AllMediaIndex);

        LOG_INFO("Fetching chunk meta");

        auto asyncMeta = chunk->ReadMeta(Config_->ReplicationWriter->WorkloadDescriptor);
        auto meta = WaitFor(asyncMeta)
            .ValueOrThrow();

        LOG_INFO("Chunk meta fetched");

        auto options = New<TRemoteWriterOptions>();
        options->AllowAllocatingNewTargetNodes = false;

        auto writer = CreateReplicationWriter(
            Config_->ReplicationWriter,
            options,
            chunkId,
            targetReplicas,
            nodeDirectory,
            Bootstrap_->GetMasterClient(),
            GetNullBlockCache(),
            Bootstrap_->GetReplicationOutThrottler());

        WaitFor(writer->Open())
            .ThrowOnError();

        int currentBlockIndex = 0;
        int blockCount = GetBlockCount(chunkId, *meta);
        while (currentBlockIndex < blockCount) {
            TBlockReadOptions options;
            options.WorkloadDescriptor = Config_->ReplicationWriter->WorkloadDescriptor;
            options.BlockCache = Bootstrap_->GetBlockCache();

            auto chunkBlockManager = Bootstrap_->GetChunkBlockManager();
            auto asyncReadBlocks = chunkBlockManager->ReadBlockRange(
                chunkId,
                currentBlockIndex,
                blockCount - currentBlockIndex,
                options);

            auto readBlocks = WaitFor(asyncReadBlocks)
                .ValueOrThrow();

            std::vector<TSharedRef> writeBlocks;
            for (const auto& block : readBlocks) {
                if (!block)
                    break;
                writeBlocks.push_back(block);
            }

            LOG_DEBUG("Enqueuing blocks for replication (Blocks: %v-%v)",
                currentBlockIndex,
                currentBlockIndex + writeBlocks.size() - 1);

            auto writeResult = writer->WriteBlocks(writeBlocks);
            if (!writeResult) {
                WaitFor(writer->GetReadyEvent())
                    .ThrowOnError();
            }

            currentBlockIndex += writeBlocks.size();
        }

        LOG_DEBUG("All blocks are enqueued for replication");

        WaitFor(writer->Close(*meta))
            .ThrowOnError();
    }

    static int GetBlockCount(const TChunkId& chunkId, const TChunkMeta& meta)
    {
        switch (TypeFromId(DecodeChunkId(chunkId).Id)) {
            case EObjectType::Chunk:
            case EObjectType::ErasureChunk: {
                auto blocksExt = GetProtoExtension<TBlocksExt>(meta.extensions());
                return blocksExt.blocks_size();
            }

            case EObjectType::JournalChunk: {
                auto miscExt = GetProtoExtension<TMiscExt>(meta.extensions());
                if (!miscExt.sealed()) {
                    THROW_ERROR_EXCEPTION("Cannot replicate an unsealed chunk %v",
                        chunkId);
                }
                return miscExt.row_count();
            }

            default:
                Y_UNREACHABLE();
        }
    }

};

////////////////////////////////////////////////////////////////////////////////

class TChunkRepairJob
    : public TChunkJobBase
{
public:
    TChunkRepairJob(
        const TJobId& jobId,
        const TJobSpec& jobSpec,
        const TNodeResources& resourceLimits,
        TDataNodeConfigPtr config,
        TBootstrap* bootstrap)
        : TChunkJobBase(
            jobId,
            std::move(jobSpec),
            resourceLimits,
            config,
            bootstrap)
        , JobSpecExt_(JobSpec_.GetExtension(TRepairChunkJobSpecExt::repair_chunk_job_spec_ext))
    { }

private:
    const TRepairChunkJobSpecExt JobSpecExt_;


    virtual void DoRun() override
    {
        auto chunkId = FromProto<TChunkId>(JobSpecExt_.chunk_id());
        auto codecId = NErasure::ECodec(JobSpecExt_.erasure_codec());
        auto* codec = NErasure::GetCodec(codecId);
        auto sourceReplicas = FromProto<TChunkReplicaList>(JobSpecExt_.source_replicas());
        auto targetReplicas = FromProto<TChunkReplicaList>(JobSpecExt_.target_replicas());

        NErasure::TPartIndexList erasedPartIndexes;
        for (auto replica : targetReplicas) {
            erasedPartIndexes.push_back(replica.GetReplicaIndex());
        }

        // Compute repair plan.
        auto repairPartIndexes = codec->GetRepairIndices(erasedPartIndexes);
        if (!repairPartIndexes) {
            THROW_ERROR_EXCEPTION("Codec is unable to repair the chunk");
        }

        LOG_INFO("Chunk repair job started (ChunkId: %v, CodecId: %v, ErasedPartIndexes: %v, RepairPartIndexes: %v, SourceReplicas: %v, TargetReplicas: %v)",
            chunkId,
            codecId,
            erasedPartIndexes,
            *repairPartIndexes,
            sourceReplicas,
            targetReplicas);

        auto nodeDirectory = New<NNodeTrackerClient::TNodeDirectory>();
        nodeDirectory->MergeFrom(JobSpecExt_.node_directory());

        std::vector<IChunkReaderPtr> readers;
        for (int partIndex : *repairPartIndexes) {
            TChunkReplicaList partReplicas;
            for (auto replica : sourceReplicas) {
                if (replica.GetReplicaIndex() == partIndex) {
                    partReplicas.push_back(replica);
                }
            }
            YCHECK(!partReplicas.empty());

            auto partId = ErasurePartIdFromChunkId(chunkId, partIndex);
            auto reader = CreateReplicationReader(
                Config_->RepairReader,
                New<TRemoteReaderOptions>(),
                Bootstrap_->GetMasterClient(),
                nodeDirectory,
                Bootstrap_->GetMasterConnector()->GetLocalDescriptor(),
                partId,
                partReplicas,
                Bootstrap_->GetBlockCache(),
                Bootstrap_->GetRepairInThrottler());
            readers.push_back(reader);
        }

        std::vector<IChunkWriterPtr> writers;
        for (int index = 0; index < static_cast<int>(erasedPartIndexes.size()); ++index) {
            int partIndex = erasedPartIndexes[index];
            auto partId = ErasurePartIdFromChunkId(chunkId, partIndex);
            auto options = New<TRemoteWriterOptions>();
            options->AllowAllocatingNewTargetNodes = false;
            auto writer = CreateReplicationWriter(
                Config_->RepairWriter,
                options,
                partId,
                TChunkReplicaList(1, targetReplicas[index]),
                nodeDirectory,
                Bootstrap_->GetMasterClient(),
                GetNullBlockCache(),
                Bootstrap_->GetRepairOutThrottler());
            writers.push_back(writer);
        }

        {
            auto onProgress = BIND(&TChunkRepairJob::SetProgress, MakeWeak(this))
                .Via(GetCurrentInvoker());

            auto result = RepairErasedParts(
                codec,
                erasedPartIndexes,
                readers,
                writers,
                Config_->RepairReader->WorkloadDescriptor);

            auto repairError = WaitFor(result);
            THROW_ERROR_EXCEPTION_IF_FAILED(repairError, "Error repairing chunk %v",
                chunkId);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

class TSealChunkJob
    : public TChunkJobBase
{
public:
    TSealChunkJob(
        const TJobId& jobId,
        TJobSpec&& jobSpec,
        const TNodeResources& resourceLimits,
        TDataNodeConfigPtr config,
        TBootstrap* bootstrap)
        : TChunkJobBase(
            jobId,
            std::move(jobSpec),
            resourceLimits,
            config,
            bootstrap)
        , JobSpecExt_(JobSpec_.GetExtension(TSealChunkJobSpecExt::seal_chunk_job_spec_ext))
    { }

private:
    const TSealChunkJobSpecExt JobSpecExt_;


    virtual void DoRun() override
    {
        auto chunkId = FromProto<TChunkId>(JobSpecExt_.chunk_id());
        int mediumIndex = JobSpecExt_.medium_index();
        auto sourceReplicas = FromProto<TChunkReplicaList>(JobSpecExt_.source_replicas());
        i64 sealRowCount = JobSpecExt_.row_count();

        LOG_INFO("Chunk seal job started (ChunkId: %v, MediumIndex: %v, SourceReplicas: %v, RowCount: %v)",
            chunkId,
            mediumIndex,
            sourceReplicas,
            sealRowCount);

        auto chunk = GetLocalChunkOrThrow(chunkId, mediumIndex);

        if (chunk->GetType() != EObjectType::JournalChunk) {
            THROW_ERROR_EXCEPTION("Cannot seal a non-journal chunk %v",
                chunkId);
        }

        auto journalChunk = chunk->AsJournalChunk();
        if (journalChunk->IsActive()) {
            THROW_ERROR_EXCEPTION("Cannot seal an active journal chunk %v",
                chunkId);
        }

        auto readGuard = TChunkReadGuard::TryAcquire(chunk);
        if (!readGuard) {
            THROW_ERROR_EXCEPTION("Cannot lock chunk %v",
                chunkId);
        }

        auto journalDispatcher = Bootstrap_->GetJournalDispatcher();
        auto location = journalChunk->GetStoreLocation();
        auto changelog = WaitFor(journalDispatcher->OpenChangelog(location, chunkId))
            .ValueOrThrow();

        if (journalChunk->HasAttachedChangelog()) {
            THROW_ERROR_EXCEPTION("Journal chunk %v is already being written",
                chunkId);
        }

        if (journalChunk->IsSealed()) {
            LOG_INFO("Chunk is already sealed");
            return;
        }

        TJournalChunkChangelogGuard changelogGuard(journalChunk, changelog);
        i64 currentRowCount = changelog->GetRecordCount();
        if (currentRowCount < sealRowCount) {
            LOG_INFO("Started downloading missing journal chunk rows (Rows: %v-%v)",
                currentRowCount,
                sealRowCount - 1);

            auto nodeDirectory = New<NNodeTrackerClient::TNodeDirectory>();
            nodeDirectory->MergeFrom(JobSpecExt_.node_directory());

            auto reader = CreateReplicationReader(
                Config_->SealReader,
                New<TRemoteReaderOptions>(),
                Bootstrap_->GetMasterClient(),
                nodeDirectory,
                Bootstrap_->GetMasterConnector()->GetLocalDescriptor(),
                chunkId,
                sourceReplicas,
                Bootstrap_->GetBlockCache(),
                Bootstrap_->GetReplicationInThrottler());

            while (currentRowCount < sealRowCount) {
                auto asyncBlocks  = reader->ReadBlocks(
                    Config_->RepairReader->WorkloadDescriptor,
                    currentRowCount,
                    sealRowCount - currentRowCount);
                auto blocks = WaitFor(asyncBlocks)
                    .ValueOrThrow();

                int blockCount = blocks.size();
                if (blockCount == 0) {
                    THROW_ERROR_EXCEPTION("Cannot download missing rows %v-%v to seal chunk %v",
                        currentRowCount,
                        sealRowCount - 1,
                        chunkId);
                }

                LOG_INFO("Journal chunk rows downloaded (Rows: %v-%v)",
                    currentRowCount,
                    currentRowCount + blockCount - 1);

                for (const auto& block : blocks) {
                    changelog->Append(block);
                }

                currentRowCount += blockCount;
            }

            WaitFor(changelog->Flush())
                .ThrowOnError();

            LOG_INFO("Finished downloading missing journal chunk rows");
        }

        LOG_INFO("Started sealing journal chunk (RowCount: %v)",
            sealRowCount);

        WaitFor(journalChunk->Seal())
            .ThrowOnError();

        LOG_INFO("Finished sealing journal chunk");

        auto chunkStore = Bootstrap_->GetChunkStore();
        chunkStore->UpdateExistingChunk(chunk);
    }
};

////////////////////////////////////////////////////////////////////////////////

IJobPtr CreateChunkJob(
    const TJobId& jobId,
    TJobSpec&& jobSpec,
    const TNodeResources& resourceLimits,
    TDataNodeConfigPtr config,
    TBootstrap* bootstrap)
{
    auto type = EJobType(jobSpec.type());
    switch (type) {
        case EJobType::ReplicateChunk:
            return New<TChunkReplicationJob>(
                jobId,
                std::move(jobSpec),
                resourceLimits,
                config,
                bootstrap);

        case EJobType::RemoveChunk:
            return New<TChunkRemovalJob>(
                jobId,
                std::move(jobSpec),
                resourceLimits,
                config,
                bootstrap);

        case EJobType::RepairChunk:
            return New<TChunkRepairJob>(
                jobId,
                std::move(jobSpec),
                resourceLimits,
                config,
                bootstrap);

        case EJobType::SealChunk:
            return New<TSealChunkJob>(
                jobId,
                std::move(jobSpec),
                resourceLimits,
                config,
                bootstrap);

        default:
            Y_UNREACHABLE();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT

