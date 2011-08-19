#include "replicator.h"

#include "../misc/assert.h"
#include "../chunk_client/remote_chunk_writer.h"

namespace NYT {
namespace NChunkHolder {

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ChunkHolderLogger;

////////////////////////////////////////////////////////////////////////////////

TJob::TJob(
    IInvoker::TPtr serviceInvoker,
    TChunkStore::TPtr chunkStore,
    TBlockStore::TPtr blockStore,
    const TJobId& jobId,
    TChunk::TPtr chunk,
    const yvector<Stroka>& targetAddresses)
    : ChunkStore(chunkStore)
    , BlockStore(blockStore)
    , JobId(jobId)
    , State(EJobState::Running)
    , Chunk(chunk)
    , TargetAddresses(targetAddresses)
    , CancelableInvoker(New<TCancelableInvoker>(serviceInvoker))
{
    // TODO: provide proper configuration
    Writer = ~New<TRemoteChunkWriter>(
        TRemoteChunkWriter::TConfig(),
        chunk->GetId(),
        targetAddresses);
}

TJobId TJob::GetJobId() const
{
    return JobId;
}

NYT::NChunkHolder::EJobState TJob::GetState() const
{
    return State;
}

yvector<Stroka> TJob::GetTargetAddresses() const
{
    return TargetAddresses;
}

TChunk::TPtr TJob::GetChunk() const
{
    return Chunk;
}

void TJob::Start()
{
    ChunkStore->GetChunkMeta(Chunk)->Subscribe(
        FromMethod(
            &TJob::OnGotMeta,
            TPtr(this))
        ->Via(~CancelableInvoker));
}

void TJob::OnGotMeta(TChunkMeta::TPtr meta)
{
    Meta = meta;
    ReplicateBlock(0);
}

void TJob::Stop()
{
    CancelableInvoker->Cancel();
    Writer->Cancel();
}

// TODO: handle errors
bool TJob::ReplicateBlock(int blockIndex)
{
    if (blockIndex >= Meta->GetBlockCount()) {
        LOG_DEBUG("All blocks are enqueued for replication (JobId: %s)",
            ~JobId.ToString());

        Writer->AsyncClose()->Subscribe(
            FromMethod(
                &TJob::OnWriterClosed,
                TPtr(this))
            ->Via(~CancelableInvoker));
        return false;
    }

    TBlockId blockId(Chunk->GetId(), blockIndex);

    LOG_DEBUG("Retrieving block for replication (JobId: %s, BlockIndex: %d)",
        ~JobId.ToString(), 
        blockIndex);

    BlockStore->FindBlock(blockId)->Subscribe(
        FromMethod(
            &TJob::OnBlockLoaded,
            TPtr(this),
            blockIndex)
        ->Via(~CancelableInvoker));
    return true;
}

void TJob::OnBlockLoaded(TCachedBlock::TPtr cachedBlock, int blockIndex)
{
    // ToDo: exception handling
    TAsyncResult<TVoid>::TPtr ready;
    if (Writer->AsyncAddBlock(cachedBlock->GetData(), &ready)) {
        LOG_DEBUG("Block is enqueued to replication writer (JobId: %s, BlockIndex: %d)",
            ~JobId.ToString(),
            blockIndex);

        ReplicateBlock(blockIndex + 1);
    } else {
        LOG_DEBUG("Replication writer window overflow (JobId: %s, BlockIndex: %d)",
            ~JobId.ToString(),
            blockIndex);

        ready->Subscribe(
            FromMethod(
                &TJob::OnBlockLoaded,
                TPtr(this),
                cachedBlock,
                blockIndex)
            ->ToParamAction<TVoid>()
            ->Via(~CancelableInvoker));
    }
}

void TJob::OnWriterClosed(IChunkWriter::EResult result)
{
    //ToDo: replace assert with proper error handling
    YASSERT(result == IChunkWriter::EResult::OK);

    LOG_DEBUG("Replication job completed (JobId: %s)",
        ~JobId.ToString());

    State = EJobState::Completed;
}

////////////////////////////////////////////////////////////////////////////////

TReplicator::TReplicator(
    TChunkStore::TPtr chunkStore,
    TBlockStore::TPtr blockStore,
    IInvoker::TPtr serviceInvoker)
    : ChunkStore(chunkStore)
    , BlockStore(blockStore)
    , ServiceInvoker(serviceInvoker)
{ }

TJob::TPtr TReplicator::StartJob(
    const TJobId& jobId,
    TChunk::TPtr chunk,
    const yvector<Stroka>& targetAddresses)
{
    TJob::TPtr job = New<TJob>(
        ServiceInvoker,
        ChunkStore,
        BlockStore,
        jobId,
        chunk,
        targetAddresses);
    YVERIFY(Jobs.insert(MakePair(jobId, job)).Second());
    job->Start();

    LOG_INFO("Replication job started (JobId: %s, TargetAddresses: [%s], ChunkId: %s)",
        ~jobId.ToString(),
        ~JoinStroku(targetAddresses, ", "),
        ~chunk->GetId().ToString());
    
    return job;
}

void TReplicator::StopJob(TJob::TPtr job)
{
    job->Stop();
    YVERIFY(Jobs.erase(job->GetJobId()) == 1);
    
    LOG_INFO("Replication job stopped (JobId: %s, State: %s)",
        ~job->GetJobId().ToString(),
        ~job->GetState().ToString());
}

TJob::TPtr TReplicator::FindJob(const TJobId& jobId)
{
    TJobMap::iterator it = Jobs.find(jobId);
    if (it == Jobs.end())
        return NULL;
    else
        return it->Second();
}

yvector<TJob::TPtr> TReplicator::GetAllJobs()
{
    yvector<TJob::TPtr> result;
    for (TJobMap::iterator it = Jobs.begin();
         it != Jobs.end();
         ++it)
    {
        result.push_back(it->Second());
    }
    return result;
}

void TReplicator::StopAllJobs()
{
    for (TJobMap::iterator it = Jobs.begin();
        it != Jobs.end();
        ++it)
    {
        it->Second()->Stop();
    }
    Jobs.clear();

    LOG_INFO("All replication jobs stopped");
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkHolder
} // namespace NYT
