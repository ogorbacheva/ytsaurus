#include "stdafx.h"
#include "location.h"
#include "private.h"
#include "chunk.h"
#include "reader_cache.h"
#include "config.h"
#include "bootstrap.h"

#include <ytlib/misc/fs.h>
#include <ytlib/chunk_client/format.h>

#include <util/folder/filelist.h>
#include <util/folder/dirut.h>

namespace NYT {
namespace NChunkHolder {

using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = DataNodeLogger;

////////////////////////////////////////////////////////////////////////////////

TLocation::TLocation(
    ELocationType type,
    const Stroka& id,
    TLocationConfigPtr config,
    TBootstrap* bootstrap)
    : Type(type)
    , Id(id)
    , Config(config)
    , Bootstrap(bootstrap)
    , CellGuid()
    , AvailableSpace(0)
    , UsedSpace(0)
    , SessionCount(0)
    , ReadQueue(New<TFairShareActionQueue>(2, Sprintf("Read:%s", ~Id)))
    , WriteQueue(New<TActionQueue>(Sprintf("Write:%s", ~Id)))
    , Logger(DataNodeLogger)
{
    Logger.AddTag(Sprintf("Path: %s", ~Config->Path));
}

TLocation::~TLocation()
{ }

ELocationType TLocation::GetType() const
{
    return Type;
}

Stroka TLocation::GetId() const
{
    return Id;
}

void TLocation::UpdateUsedSpace(i64 size)
{
    UsedSpace += size;
    AvailableSpace -= size;
}

i64 TLocation::GetAvailableSpace() const
{
    auto path = GetPath();
    
    try {
        AvailableSpace = NFS::GetAvailableSpace(path);
    } catch (const std::exception& ex) {
        LOG_FATAL("Failed to compute available space\n%s",
            ex.what());
    }

    i64 remainingQuota = Max(static_cast<i64>(0), GetQuota() - GetUsedSpace());

    AvailableSpace = Min(AvailableSpace, remainingQuota);

    return AvailableSpace;
}

TBootstrap* TLocation::GetBootstrap() const
{
    return Bootstrap;
}

i64 TLocation::GetUsedSpace() const
{
    return UsedSpace;
}

i64 TLocation::GetQuota() const
{
    return Config->Quota.Get(Max<i64>());
}

double TLocation::GetLoadFactor() const
{
    i64 used = GetUsedSpace();
    i64 quota = GetQuota();
    if (used >= quota) {
        return 1.0;
    } else {
        return (double) used / quota;
    }
}

Stroka TLocation::GetPath() const
{
    return Config->Path;
}

void TLocation::UpdateSessionCount(int delta)
{
    SessionCount += delta;
    LOG_DEBUG("Location session count updated (SessionCount: %d)",
        SessionCount);
}

int TLocation::GetSessionCount() const
{
    return SessionCount;
}

Stroka TLocation::GetChunkFileName(const TChunkId& chunkId) const
{
    ui8 firstHashByte = static_cast<ui8>(chunkId.Parts[0] & 0xff);
    return NFS::CombinePaths(
        GetPath(),
        Sprintf("%02x%s%s", firstHashByte, LOCSLASH_S, ~chunkId.ToString()));
}

bool TLocation::IsFull() const
{
    return GetAvailableSpace() < Config->LowWatermark;
}

bool TLocation::HasEnoughSpace(i64 size) const
{
    return GetAvailableSpace() - size >= Config->HighWatermark;
}

IInvokerPtr TLocation::GetDataReadInvoker()
{
    return ReadQueue->GetInvoker(0);
}

IInvokerPtr TLocation::GetMetaReadInvoker()
{
    return ReadQueue->GetInvoker(1);
}

IInvokerPtr TLocation::GetWriteInvoker()
{
    return WriteQueue->GetInvoker();
}

namespace {

void RemoveFile(const Stroka& fileName)
{
    if (!NFS::Remove(fileName)) {
        LOG_FATAL("Error deleting file %s", ~fileName.Quote());
    }
}

} // namespace

const TGuid& TLocation::GetCellGuid() 
{
    return CellGuid;
}

void TLocation::UpdateCellGuid(const TGuid& newCellGuid)
{
    CellGuid = newCellGuid;

    {
        auto cellGuidPath = NFS::CombinePaths(GetPath(), CellGuidFileName);
        TFileOutput cellGuidFile(cellGuidPath);
        cellGuidFile.Write(CellGuid.ToString());
    }

    LOG_INFO("Cell guid updated: %s", ~CellGuid.ToString());
}

std::vector<TChunkDescriptor> TLocation::Scan()
{
    auto path = GetPath();

    LOG_INFO("Scanning storage location");

    NFS::ForcePath(path);
    NFS::CleanTempFiles(path);

    yhash_set<Stroka> fileNames;
    yhash_set<TChunkId> chunkIds;

    TFileList fileList;
    fileList.Fill(path, TStringBuf(), TStringBuf(), Max<int>());
    i32 size = fileList.Size();
    for (i32 i = 0; i < size; ++i) {
        Stroka fileName = fileList.Next();
        if (fileName == CellGuidFileName)
            continue;

        TChunkId chunkId;
        auto strippedFileName = NFS::GetFileNameWithoutExtension(fileName);
        if (TChunkId::FromString(strippedFileName, &chunkId)) {
            fileNames.insert(NFS::NormalizePathSeparators(NFS::CombinePaths(path, fileName)));
            chunkIds.insert(chunkId);
        } else {
            LOG_ERROR("Unrecognized file: %s", ~fileName);
        }
    }

    std::vector<TChunkDescriptor> result;
    result.reserve(chunkIds.size());

    FOREACH (const auto& chunkId, chunkIds) {
        auto chunkDataFileName = GetChunkFileName(chunkId);
        auto chunkMetaFileName = chunkDataFileName + ChunkMetaSuffix;

        bool hasMeta = fileNames.find(NFS::NormalizePathSeparators(chunkMetaFileName)) != fileNames.end();
        bool hasData = fileNames.find(NFS::NormalizePathSeparators(chunkDataFileName)) != fileNames.end();

        YASSERT(hasMeta || hasData);

        if (hasMeta && hasData) {
            i64 chunkDataSize = NFS::GetFileSize(chunkDataFileName);
            i64 chunkMetaSize = NFS::GetFileSize(chunkMetaFileName);
            if (chunkMetaSize == 0) {
                LOG_FATAL("Chunk meta file is empty: %s", ~chunkMetaFileName);
            }
            TChunkDescriptor descriptor;
            descriptor.Id = chunkId;
            descriptor.Size = chunkDataSize + chunkMetaSize;
            result.push_back(descriptor);
        } else if (!hasMeta) {
            LOG_WARNING("Missing meta file, removing data file: %s", ~chunkDataFileName);
            RemoveFile(chunkDataFileName);
        } else if (!hasData) {
            LOG_WARNING("Missing data file, removing meta file: %s", ~chunkMetaFileName);
            RemoveFile(chunkMetaFileName);
        }
    }

    LOG_INFO("Done, %" PRISZT " chunks found", result.size());

    auto cellGuidPath = NFS::CombinePaths(path, CellGuidFileName);
    if (isexist(~cellGuidPath)) {
        TFileInput cellGuidFile(cellGuidPath);
        auto cellGuidString = cellGuidFile.ReadAll();
        if (TGuid::FromString(cellGuidString, &CellGuid)) {
            LOG_INFO("Cell guid: %s", ~cellGuidString);
        } else {
            LOG_FATAL("Failed to parse cell guid: %s", ~cellGuidString);
        }
    } else {
        LOG_INFO("Cell guid not found");
    }

    // Force subdirectories.
    for (int hashByte = 0; hashByte <= 0xff; ++hashByte) {
        NFS::ForcePath(NFS::CombinePaths(GetPath(), Sprintf("%02x", hashByte)));
    }

    return result;
}

TFuture<void> TLocation::ScheduleChunkRemoval(TChunk* chunk)
{
    auto id = chunk->GetId();
    Stroka fileName = GetChunkFileName(id);

    LOG_INFO("Chunk removal scheduled (ChunkId: %s)", ~id.ToString());

    auto promise = NewPromise<void>();
    GetWriteInvoker()->Invoke(BIND([=] () mutable {
        // TODO: retry on failure
        LOG_DEBUG("Started removing chunk files (ChunkId: %s)", ~id.ToString());
        RemoveFile(fileName);
        RemoveFile(fileName + ChunkMetaSuffix);
        LOG_DEBUG("Finished removing chunk files (ChunkId: %s)", ~id.ToString());
        promise.Set();
    }));

    return promise;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkHolder
} // namespace NYT
