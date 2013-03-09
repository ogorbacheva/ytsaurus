#ifndef CHUNK_SEQUENCE_WRITER_BASE_INL_H_
#error "Direct inclusion of this file is not allowed, include chunk_sequence_writer_base.h"
#endif
#undef CHUNK_SEQUENCE_WRITER_BASE_INL_H_

#include "private.h"
#include "schema.h"

#include <ytlib/misc/string.h>
#include <ytlib/misc/address.h>

#include <ytlib/transaction_client/transaction_ypath_proxy.h>

#include <ytlib/chunk_client/chunk_list_ypath_proxy.h>
#include <ytlib/chunk_client/chunk_ypath_proxy.h>
#include <ytlib/chunk_client/dispatcher.h>
#include <ytlib/chunk_client/chunk_replica.h>
#include <ytlib/chunk_client/node_directory.h>

#include <ytlib/cypress_client/cypress_ypath_proxy.h>

#include <ytlib/meta_state/rpc_helpers.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

template <class TChunkWriter>
TChunkSequenceWriterBase<TChunkWriter>::TChunkSequenceWriterBase(
    TTableWriterConfigPtr config,
    TTableWriterOptionsPtr options,
    NRpc::IChannelPtr masterChannel,
    const NObjectClient::TTransactionId& transactionId,
    const NChunkClient::TChunkListId& parentChunkList)
    : Config(config)
    , Options(options)
    , ReplicationFactor(Options->ReplicationFactor)
    , UploadReplicationFactor(std::min(options->ReplicationFactor, Config->UploadReplicationFactor))
    , MasterChannel(masterChannel)
    , TransactionId(transactionId)
    , ParentChunkListId(parentChunkList)
    , NodeDirectory(New<NChunkClient::TNodeDirectory>())
    , RowCount(0)
    , Progress(0)
    , CompleteChunkSize(0)
    , CloseChunksAwaiter(New<TParallelAwaiter>(NChunkClient::TDispatcher::Get()->GetWriterInvoker()))
    , Logger(TableWriterLogger)
{
    YCHECK(config);
    YCHECK(masterChannel);

    Logger.AddTag(Sprintf("TransactionId: %s", ~ToString(TransactionId)));
}

template <class TChunkWriter>
TChunkSequenceWriterBase<TChunkWriter>::~TChunkSequenceWriterBase()
{ }

template <class TChunkWriter>
bool TChunkSequenceWriterBase<TChunkWriter>::TryWriteRow(const TRow& row)
{
    if (!CurrentSession.ChunkWriter) {
        return false;
    }

    if (!CurrentSession.ChunkWriter->TryWriteRow(row)) {
        return false;
    }

    OnRowWritten();

    return true;
}

template <class TChunkWriter>
bool TChunkSequenceWriterBase<TChunkWriter>::TryWriteRowUnsafe(const TRow& row)
{
    if (!CurrentSession.ChunkWriter) {
        return false;
    }

    if (!CurrentSession.ChunkWriter->TryWriteRowUnsafe(row)) {
        return false;
    }

    OnRowWritten();

    return true;
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::CreateNextSession()
{
    YCHECK(!NextSession);

    NextSession = NewPromise<TSession>();

    LOG_DEBUG("Creating chunk (ReplicationFactor: %d, UploadReplicationFactor: %d)",
        ReplicationFactor,
        UploadReplicationFactor);

    NObjectClient::TObjectServiceProxy objectProxy(MasterChannel);

    auto req = NObjectClient::TMasterYPathProxy::CreateObject();
    ToProto(req->mutable_transaction_id(), TransactionId);
    req->set_type(NObjectClient::EObjectType::Chunk);
    req->set_account(Options->Account);
    NMetaState::GenerateRpcMutationId(req);

    auto* reqExt = req->MutableExtension(NChunkClient::NProto::TReqCreateChunkExt::create_chunk);
    if (Config->PreferLocalHost) {
        reqExt->set_preferred_host_name(TAddressResolver::Get()->GetLocalHostName());
    }
    reqExt->set_replication_factor(Options->ReplicationFactor);
    reqExt->set_upload_replication_factor(UploadReplicationFactor);
    reqExt->set_movable(Config->ChunksMovable);
    reqExt->set_vital(Config->ChunksVital);

    objectProxy.Execute(req).Subscribe(
        BIND(&TChunkSequenceWriterBase::OnChunkCreated, MakeWeak(this))
            .Via(NChunkClient::TDispatcher::Get()->GetWriterInvoker()));
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::OnChunkCreated(
    NObjectClient::TMasterYPathProxy::TRspCreateObjectPtr rsp)
{
    VERIFY_THREAD_AFFINITY_ANY();
    YCHECK(NextSession);

    if (!State.IsActive()) {
        return;
    }

    if (!rsp->IsOK()) {
        auto wrappedError = TError(
            EErrorCode::MasterCommunicationFailed,
            "Error creating chunk") << *rsp;
        State.Fail(wrappedError);
        return;
    }

    auto chunkId = FromProto<NChunkClient::TChunkId>(rsp->object_id());
    const auto& rspExt = rsp->GetExtension(NChunkClient::NProto::TRspCreateChunkExt::create_chunk);
    
    NodeDirectory->MergeFrom(rspExt.node_directory());

    TSession session;
    session.Replicas = FromProto<NChunkClient::TChunkReplica>(rspExt.replicas());
    if (session.Replicas.size() < UploadReplicationFactor) {
        State.Fail(TError("Not enough data nodes available: %d received, %d needed",
            static_cast<int>(session.Replicas.size()),
            UploadReplicationFactor));
        return;
    }

    LOG_DEBUG("Chunk created (ChunkId: %s)",
        ~ToString(chunkId));

    auto targets = NodeDirectory->GetDescriptors(session.Replicas);
    session.RemoteWriter = New<NChunkClient::TRemoteWriter>(Config, chunkId, targets);
    session.RemoteWriter->Open();

    PrepareChunkWriter(&session);

    NextSession.Set(session);
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::SetProgress(double progress)
{
    Progress = progress;
}

template <class TChunkWriter>
TAsyncError TChunkSequenceWriterBase<TChunkWriter>::AsyncOpen()
{
    YCHECK(!State.HasRunningOperation());

    CreateNextSession();

    State.StartOperation();
    NextSession.Subscribe(BIND(
        &TChunkSequenceWriterBase::InitCurrentSession,
        MakeWeak(this)));

    return State.GetOperationError();
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::InitCurrentSession(TSession nextSession)
{
    VERIFY_THREAD_AFFINITY_ANY();

    CurrentSession = nextSession;

    NextSession.Reset();
    CreateNextSession();

    State.FinishOperation();
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::OnRowWritten()
{
    VERIFY_THREAD_AFFINITY_ANY();

    ++RowCount;

    if (CurrentSession.ChunkWriter->GetMetaSize() > Config->MaxMetaSize) {
        LOG_DEBUG("Switching to next chunk: meta is too large (ChunkMetaSize: %" PRId64 ")",
            CurrentSession.ChunkWriter->GetMetaSize());

        SwitchSession();
        return;
    }

    if (CurrentSession.ChunkWriter->GetCurrentSize() > Config->DesiredChunkSize) {
        i64 currentDataSize = CompleteChunkSize + CurrentSession.ChunkWriter->GetCurrentSize();
        i64 expectedInputSize = static_cast<i64>(currentDataSize * std::max(0.0, 1.0 - Progress));

        if (expectedInputSize > Config->DesiredChunkSize ||
            CurrentSession.ChunkWriter->GetCurrentSize() > 2 * Config->DesiredChunkSize)
        {
            LOG_DEBUG("Switching to next chunk: too much data (CurrentSessionSize: %" PRId64 ", ExpectedInputSize: %" PRId64 ")",
                CurrentSession.ChunkWriter->GetCurrentSize(),
                expectedInputSize);

            SwitchSession();
            return;
        }
    }
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::SwitchSession()
{
    State.StartOperation();
    YCHECK(NextSession);
    // We're not waiting for the chunk to close.
    FinishCurrentSession();
    NextSession.Subscribe(BIND(
        &TChunkSequenceWriterBase::InitCurrentSession,
        MakeWeak(this)));
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::FinishCurrentSession()
{
    if (CurrentSession.IsNull())
        return;

    if (CurrentSession.ChunkWriter->GetCurrentSize() > 0) {
        LOG_DEBUG("Finishing chunk (ChunkId: %s)",
            ~ToString(CurrentSession.RemoteWriter->GetChunkId()));

        int chunkIndex = 0;
        {
            // Create a placeholder in WrittenChunks (to be filled later in OnChunkClosed).
            TGuard<TSpinLock> guard(WrittenChunksGuard);
            chunkIndex = WrittenChunks.size();
            WrittenChunks.push_back(NProto::TInputChunk());
        }

        auto finishResult = NewPromise<TError>();
        CloseChunksAwaiter->Await(finishResult.ToFuture(), BIND(
            &TChunkSequenceWriterBase::OnChunkFinished,
            MakeWeak(this),
            CurrentSession.RemoteWriter->GetChunkId()));

        CurrentSession.ChunkWriter->AsyncClose().Subscribe(BIND(
            &TChunkSequenceWriterBase::OnChunkClosed,
            MakeWeak(this),
            chunkIndex,
            CurrentSession,
            finishResult));

    } else {
        LOG_DEBUG("Canceling empty chunk (ChunkId: %s)",
            ~ToString(CurrentSession.RemoteWriter->GetChunkId()));
    }

    CurrentSession.Reset();
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::OnChunkClosed(
    int chunkIndex,
    TSession currentSession,
    TAsyncErrorPromise finishResult,
    TError error)
{
    VERIFY_THREAD_AFFINITY_ANY();

    if (!error.IsOK()) {
        auto wrappedError = TError("Error closing chunk") << error;
        finishResult.Set(wrappedError);
        return;
    }

    auto remoteWriter = currentSession.RemoteWriter;
    auto chunkWriter = currentSession.ChunkWriter;

    CompleteChunkSize += chunkWriter->GetCurrentSize();

    LOG_DEBUG("Chunk closed (ChunkId: %s)",
        ~ToString(remoteWriter->GetChunkId()));

    std::vector<NChunkClient::TChunkReplica> replicas;
    FOREACH (int index, remoteWriter->GetWrittenIndexes()) {
        replicas.push_back(currentSession.Replicas[index]);
    }

    NObjectClient::TObjectServiceProxy objectProxy(MasterChannel);
    auto batchReq = objectProxy.ExecuteBatch();
    {
        auto req = NChunkClient::TChunkYPathProxy::Confirm(
            NCypressClient::FromObjectId(remoteWriter->GetChunkId()));
        NMetaState::GenerateRpcMutationId(req);
        *req->mutable_chunk_info() = remoteWriter->GetChunkInfo();
        ToProto(req->mutable_replicas(), replicas);
        *req->mutable_chunk_meta() = chunkWriter->GetMasterMeta();

        batchReq->AddRequest(req);
    }
    {
        // Initialize the entry earlier prepared in FinishCurrentSession.
        TGuard<TSpinLock> guard(WrittenChunksGuard);
        auto& inputChunk = WrittenChunks[chunkIndex];
        ToProto(inputChunk.mutable_chunk_id(), currentSession.RemoteWriter->GetChunkId());
        ToProto(inputChunk.mutable_replicas(), replicas);
        *inputChunk.mutable_extensions() = chunkWriter->GetSchedulerMeta().extensions();
    }

    batchReq->Invoke().Subscribe(BIND(
        &TChunkSequenceWriterBase::OnChunkConfirmed,
        MakeWeak(this),
        remoteWriter->GetChunkId(),
        finishResult));
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::OnChunkConfirmed(
    NChunkClient::TChunkId chunkId,
    TAsyncErrorPromise finishResult,
    NObjectClient::TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
{
    VERIFY_THREAD_AFFINITY_ANY();

    auto error = batchRsp->GetCumulativeError();
    if (!error.IsOK()) {
        auto wrappedError = TError(
            EErrorCode::MasterCommunicationFailed,
            "Error confirming chunk %s",
            ~ToString(chunkId)) << error;

        finishResult.Set(wrappedError);
        return;
    }

    LOG_DEBUG("Chunk confirmed (ChunkId: %s)",
        ~ToString(chunkId));

    finishResult.Set(TError());
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::OnChunkFinished(
    NChunkClient::TChunkId chunkId,
    TError error)
{
    VERIFY_THREAD_AFFINITY_ANY();

    if (!error.IsOK()) {
        State.Fail(error);
        return;
    }

    LOG_DEBUG("Chunk successfully closed and registered (ChunkId: %s)",
        ~ToString(chunkId));
}

template <class TChunkWriter>
TAsyncError TChunkSequenceWriterBase<TChunkWriter>::AsyncClose()
{
    YCHECK(!State.HasRunningOperation());

    State.StartOperation();
    FinishCurrentSession();

    CloseChunksAwaiter->Complete(BIND(
        &TChunkSequenceWriterBase::AttachChunks,
        MakeWeak(this)));

    return State.GetOperationError();
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::AttachChunks()
{
    if (!State.IsActive()) {
        return;
    }

    NObjectClient::TObjectServiceProxy objectProxy(MasterChannel);
    auto batchReq = objectProxy.ExecuteBatch();

    FOREACH (const auto& inputChunk, WrittenChunks) {
        auto req = NChunkClient::TChunkListYPathProxy::Attach(
            NCypressClient::FromObjectId(ParentChunkListId));
        *req->add_children_ids() = inputChunk.chunk_id();
        NMetaState::GenerateRpcMutationId(req);
        batchReq->AddRequest(req);
    }

    batchReq->Invoke().Subscribe(BIND(
        &TChunkSequenceWriterBase::OnClose,
        MakeWeak(this)));
}

template <class TChunkWriter>
void TChunkSequenceWriterBase<TChunkWriter>::OnClose(
    NObjectClient::TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
{
    if (!State.IsActive()) {
        return;
    }

    auto error = batchRsp->GetCumulativeError();
    if (!error.IsOK()) {
        auto wrappedError = TError(
            EErrorCode::MasterCommunicationFailed,
            "Error attaching chunks to chunk list %s",
            ~ToString(ParentChunkListId))
            << error;
        State.Fail(wrappedError);
        return;
    }

    LOG_DEBUG("Chunk sequence writer closed");

    State.Close();
    State.FinishOperation();
}

template <class TChunkWriter>
const std::vector<NProto::TInputChunk>& TChunkSequenceWriterBase<TChunkWriter>::GetWrittenChunks() const
{
    return WrittenChunks;
}

template <class TChunkWriter>
NChunkClient::TNodeDirectoryPtr TChunkSequenceWriterBase<TChunkWriter>::GetNodeDirectory() const
{
    return NodeDirectory;
}

template <class TChunkWriter>
i64 TChunkSequenceWriterBase<TChunkWriter>::GetRowCount() const
{
    return RowCount;
}

template <class TChunkWriter>
const TNullable<TKeyColumns>& TChunkSequenceWriterBase<TChunkWriter>::GetKeyColumns() const
{
    return Options->KeyColumns;
}

template <class TChunkWriter>
TAsyncError TChunkSequenceWriterBase<TChunkWriter>::GetReadyEvent()
{
    if (State.HasRunningOperation()) {
        return State.GetOperationError();
    }

    YCHECK(CurrentSession.ChunkWriter);
    return CurrentSession.ChunkWriter->GetReadyEvent();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
} // namespace NTableClient
