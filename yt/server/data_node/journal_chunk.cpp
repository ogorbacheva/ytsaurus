#include "stdafx.h"
#include "journal_chunk.h"
#include "journal_dispatcher.h"
#include "location.h"
#include "private.h"

#include <core/misc/fs.h>

#include <core/concurrency/thread_affinity.h>

#include <core/profiling/scoped_timer.h>

#include <ytlib/chunk_client/chunk_meta_extensions.h>

#include <server/hydra/changelog.h>
#include <server/hydra/private.h>

#include <server/cell_node/bootstrap.h>
#include <server/cell_node/config.h>

namespace NYT {
namespace NDataNode {

using namespace NHydra;
using namespace NCellNode;
using namespace NChunkClient;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = DataNodeLogger;
static auto& Profiler = DataNodeProfiler;

static NProfiling::TRateCounter DiskJournalReadThroughputCounter("/disk_journal_read_throughput");

////////////////////////////////////////////////////////////////////////////////

TJournalChunk::TJournalChunk(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkId& id,
    const TChunkInfo& info)
    : TChunk(
        bootstrap,
        location,
        id,
        info)
{
    Meta_ = New<TRefCountedChunkMeta>();
    Meta_->set_type(EChunkType::Journal);

    RecordCount_ = 0;
    Sealed_ = false;
}

IChunk::TAsyncGetMetaResult TJournalChunk::GetMeta(
    i64 /*priority*/,
    const std::vector<int>* tags /*= nullptr*/)
{
    // Update TJournalExt.
    TJournalExt journalExt;
    journalExt.set_record_count(RecordCount_);
    journalExt.set_sealed(Sealed_);
    SetProtoExtension(Meta_->mutable_extensions(), journalExt);

    return MakeFuture<TGetMetaResult>(FilterCachedMeta(tags));
}

TAsyncError TJournalChunk::ReadBlocks(
    int firstBlockIndex,
    int blockCount,
    i64 priority,
    std::vector<TSharedRef>* blocks)
{
    YCHECK(firstBlockIndex >= 0);
    YCHECK(blockCount >= 0);

    auto promise = NewPromise<TError>();

    auto callback = BIND(
        &TJournalChunk::DoReadBlocks,
        MakeStrong(this),
        firstBlockIndex,
        blockCount,
        promise,
        blocks);

    Location_
        ->GetDataReadInvoker()
        ->Invoke(callback, priority);

    return promise;
}

void TJournalChunk::DoReadBlocks(
    int firstBlockIndex,
    int blockCount,
    TPromise<TError> promise,
    std::vector<TSharedRef>* blocks)
{
    auto config = Bootstrap_->GetConfig();
    auto dispatcher = Bootstrap_->GetJournalDispatcher();

    try {
        auto changelog = dispatcher->GetChangelog(this);
    
        LOG_DEBUG("Started reading journal chunk blocks (BlockIds: %s:%d-%d, LocationId: %s)",
            ~ToString(Id_),
            firstBlockIndex,
            firstBlockIndex + blockCount - 1,
            ~Location_->GetId());

        NProfiling::TScopedTimer timer;

        auto readBlocks = changelog->Read(
            firstBlockIndex,
            blockCount,
            config->DataNode->JournalDispatcher->MaxBytesPerRead);

        auto readTime = timer.GetElapsed();

        i64 readSize = 0;
        for (int index = 0; index < readBlocks.size(); ++index) {
            auto& readBlock = readBlocks[index];
            readSize += readBlock.Size();
            auto& block = (*blocks)[index + firstBlockIndex];
            if (!block) {
                block = std::move(readBlock);
            }
        }

        LOG_DEBUG("Finished reading journal chunk blocks (BlockIds: %s:%d-%d, LocationId: %s, ActuallyReadBlocks: %d, ActuallyReadBytes: %" PRId64 ")",
            ~ToString(Id_),
            firstBlockIndex,
            firstBlockIndex + blockCount - 1,
            ~Location_->GetId(),
            static_cast<int>(readBlocks.size()),
            readSize);

        auto& locationProfiler = Location_->Profiler();
        locationProfiler.Enqueue("/journal_read_size", readSize);
        locationProfiler.Enqueue("/journal_read_time", readTime.MicroSeconds());
        locationProfiler.Enqueue("/journal_read_throughput", readSize * 1000000 / (1 + readTime.MicroSeconds()));
        DataNodeProfiler.Increment(DiskJournalReadThroughputCounter, readSize);

        promise.Set(TError());
    } catch (const std::exception& ex) {
        auto error = TError(
            NChunkClient::EErrorCode::IOError,
            "Error reading journal chunk %s",
            ~ToString(Id_))
            << ex;
        Location_->Disable();
        promise.Set(ex);
    }
}

void TJournalChunk::EvictFromCache()
{
    auto dispatcher = Bootstrap_->GetJournalDispatcher();
    dispatcher->EvictChangelog(this);
}

TFuture<void> TJournalChunk::RemoveFiles()
{
    auto dataFileName = GetFileName();
    auto indexFileName = dataFileName + IndexSuffix;
    auto id = Id_;
    auto location = Location_;

    return BIND([=] () {
        LOG_DEBUG("Started removing journal chunk files (ChunkId: %s)",
            ~ToString(id));

        try {
            NFS::Remove(dataFileName);
            NFS::Remove(indexFileName);
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "Error removing journal chunk files");
            location->Disable();
        }

        LOG_DEBUG("Finished removing journal chunk files (ChunkId: %s)",
            ~ToString(id));
    }).AsyncVia(location->GetWriteInvoker()).Run();
}

void TJournalChunk::SetRecordCount(int recordCount)
{
    VERIFY_THREAD_AFFINITY_ANY();

    RecordCount_ = recordCount;
}

void TJournalChunk::SetSealed(bool value)
{
    VERIFY_THREAD_AFFINITY_ANY();

    Sealed_ = value;
}

TNullable<TChunkDescriptor> TJournalChunk::TryGetDescriptor(
    const TChunkId& id,
    const Stroka& fileName)
{
    if (!NFS::Exists(fileName)) {
        auto indexFileName = fileName + IndexSuffix;
        if (NFS::Exists(indexFileName)) {
            LOG_WARNING("Missing data file, removing index file %s",
                ~indexFileName.Quote());
            NFS::Remove(indexFileName);
        }
        return Null;
    }

    TChunkDescriptor descriptor;
    descriptor.Id = id;
    descriptor.Info.set_disk_space(0);
    return descriptor;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
