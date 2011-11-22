#include "stdafx.h"
#include "snapshot_downloader.h"
#include "snapshot.h"
#include "meta_state_manager_rpc.h"

#include "../actions/action_util.h"
#include "../actions/future.h"

#include <util/system/fs.h>

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = MetaStateLogger;

////////////////////////////////////////////////////////////////////////////////

TSnapshotDownloader::TSnapshotDownloader(
    const TConfig& config,
    TCellManager::TPtr cellManager)
    : Config(config)
    , CellManager(cellManager)
{
    YASSERT(~cellManager != NULL);
}

TSnapshotDownloader::EResult TSnapshotDownloader::GetSnapshot(
    i32 segmentId,
    TSnapshotWriter::TPtr snapshotWriter)
{
    YASSERT(~snapshotWriter != NULL);

    TSnapshotInfo snapshotInfo = GetSnapshotInfo(segmentId);
    TPeerId sourceId = snapshotInfo.SourceId;
    if (sourceId == NElection::InvalidPeerId) {
        return EResult::SnapshotNotFound;
    }
    
    EResult result = DownloadSnapshot(segmentId, snapshotInfo, snapshotWriter);
    if (result != EResult::OK) {
        return result;
    }

    return EResult::OK;
}

TSnapshotDownloader::TSnapshotInfo TSnapshotDownloader::GetSnapshotInfo(i32 snapshotId)
{
    auto asyncResult = New< TFuture<TSnapshotInfo> >();
    auto awaiter = New<TParallelAwaiter>();

    for (TPeerId peerId = 0; peerId < CellManager->GetPeerCount(); ++peerId) {
        if (peerId == CellManager->GetSelfId()) continue;

        LOG_INFO("Requesting snapshot info from peer %d", peerId);

        auto proxy = CellManager->GetMasterProxy<TProxy>(peerId);
        proxy->SetTimeout(Config.LookupTimeout);

        auto request = proxy->GetSnapshotInfo();
        request->SetSnapshotId(snapshotId);
        awaiter->Await(request->Invoke(), FromMethod(
            &TSnapshotDownloader::OnResponse,
            awaiter, asyncResult, peerId));
    }

    awaiter->Complete(FromMethod(
        &TSnapshotDownloader::OnComplete,
        snapshotId,
        asyncResult));

    return asyncResult->Get();
}

void TSnapshotDownloader::OnResponse(
    TProxy::TRspGetSnapshotInfo::TPtr response,
    TParallelAwaiter::TPtr awaiter,
    TFuture<TSnapshotInfo>::TPtr asyncResult,
    TPeerId peerId)
{
    if (!response->IsOK()) {
        // We have no snapshot id to log it here
        LOG_INFO("Error %s requesting snapshot info from peer %d",
            ~response->GetError().ToString(),
            peerId);
        return;
    }
    
    i64 length = response->GetLength();
    ui64 checksum = response->GetChecksum();
    i32 prevRecordCount = response->GetPrevRecordCount();
    
    LOG_INFO("Got snapshot info from peer %d (Length: %" PRId64 ", Checksum: %" PRIx64 ")",
        peerId,
        length,
        checksum);

    asyncResult->Set(TSnapshotInfo(peerId, length, prevRecordCount, checksum));
    awaiter->Cancel();
}

void TSnapshotDownloader::OnComplete(
    i32 segmentId,
    TFuture<TSnapshotInfo>::TPtr asyncResult)
{
    LOG_INFO("Could not get snapshot %d info from masters", segmentId);

    asyncResult->Set(TSnapshotInfo(NElection::InvalidPeerId, -1, 0, 0));
}

TSnapshotDownloader::EResult TSnapshotDownloader::DownloadSnapshot(
    i32 segmentId,
    TSnapshotInfo snapshotInfo,
    TSnapshotWriter::TPtr snapshotWriter)
{
    YASSERT(snapshotInfo.Length >= 0);
    
    TPeerId sourceId = snapshotInfo.SourceId;
    try {
        snapshotWriter->OpenRaw(snapshotInfo.PrevRecordCount);
    } catch (const yexception& ex) {
        LOG_ERROR("Could not open snapshot writer\n%s", ex.what());
        return EResult::IOError;
    }

    TOutputStream& output = snapshotWriter->GetRawStream();
    EResult result = WriteSnapshot(segmentId, snapshotInfo.Length, sourceId, output);
    if (result != EResult::OK) {
        return result;
    }

    try {
        snapshotWriter->Close();
    } catch (const yexception& ex) {
        LOG_ERROR("Could not close snapshot writer\n%s", ex.what());
        return EResult::IOError;
    }

    // snapshotWriter can't calculate checksum in raw mode.
    /*if (snapshotWriter->GetChecksum() != snapshotInfo.Checksum) {
        LOG_ERROR(
            "Incorrect checksum in snapshot %d from peer %d, "
            "expected %" PRIx64 ", got %" PRIx64,
            segmentId, sourceId, snapshotInfo.Checksum, snapshotWriter->GetChecksum());
        return EResult::IncorrectChecksum;
    }*/
    
    return EResult::OK;
}

TSnapshotDownloader::EResult TSnapshotDownloader::WriteSnapshot(
    i32 snapshotId,
    i64 snapshotLength,
    i32 sourceId,
    TOutputStream& output)
{
    LOG_INFO("Started downloading snapshot (SnapshotId: %d, Length: %" PRId64 ", PeerId: %d)",
            snapshotId,
            snapshotLength,
            sourceId);

    auto proxy = CellManager->GetMasterProxy<TProxy>(sourceId);
    proxy->SetTimeout(Config.ReadTimeout);

    i64 downloadedLength = 0;
    while (downloadedLength < snapshotLength) {
        auto request = proxy->ReadSnapshot();
        request->SetSnapshotId(snapshotId);
        request->SetOffset(downloadedLength);
        i32 blockSize = Min(Config.BlockSize, (i32)(snapshotLength - downloadedLength));
        request->SetLength(blockSize);
        auto response = request->Invoke()->Get();

        if (!response->IsOK()) {
            auto error = response->GetError();
            if (error.IsServiceError()) {
                switch (TProxy::EErrorCode(error.GetCode())) {
                    case TProxy::EErrorCode::InvalidSegmentId:
                        LOG_WARNING(
                            "Peer %d does not have snapshot %d anymore",
                            sourceId,
                            snapshotId);
                        return EResult::SnapshotUnavailable;

                    case TProxy::EErrorCode::IOError:
                        LOG_WARNING(
                            "IO error occurred on peer %d during downloading snapshot %d",
                            sourceId,
                            snapshotId);
                        return EResult::RemoteError;

                    default:
                        LOG_FATAL("Unknown error code %s received from peer %d",
                            ~error.ToString(),
                            sourceId);
                        break;
                }
            } else {
                LOG_WARNING("RPC error %s reading snapshot from peer %d",
                    ~error.ToString(),
                    sourceId);
                return EResult::RemoteError;
            }
        }
        
        const yvector<TSharedRef>& attachments = response->Attachments();
        TRef block(attachments.at(0));

        if (static_cast<i32>(block.Size()) != blockSize) {
            LOG_WARNING("Snapshot block of wrong size received (Offset: %" PRId64 ", Size: %d, ExpectedSize: %d)",
                downloadedLength,
                static_cast<i32>(block.Size()),
                blockSize);
            // continue anyway
        } else {
            LOG_DEBUG("Snapshot block received (Offset: %" PRId64 ", Size: %d)",
                downloadedLength,
                blockSize);
        }

        try {
            output.Write(block.Begin(), block.Size());
        } catch (const yexception& ex) {
            LOG_ERROR("Exception occurred while writing to output\n%s",
                ex.what());
            return EResult::IOError;
        }

        downloadedLength += block.Size();
    }

    LOG_INFO("Finished downloading snapshot");

    return EResult::OK;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
