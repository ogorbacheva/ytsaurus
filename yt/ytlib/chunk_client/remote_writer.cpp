#include "stdafx.h"
#include "remote_writer.h"
#include "config.h"
#include "dispatcher.h"
#include "private.h"
#include "chunk_meta_extensions.h"

#include <ytlib/misc/serialize.h>
#include <ytlib/misc/metric.h>
#include <ytlib/misc/string.h>
#include <ytlib/misc/protobuf_helpers.h>
#include <ytlib/misc/periodic_invoker.h>
#include <ytlib/misc/semaphore.h>
#include <ytlib/misc/thread_affinity.h>
#include <ytlib/misc/async_stream_state.h>

#include <ytlib/logging/tagged_logger.h>

#include <ytlib/actions/parallel_awaiter.h>

#include <ytlib/chunk_client/data_node_service.pb.h>

namespace NYT {
namespace NChunkClient {

using namespace NRpc;

///////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ChunkWriterLogger;

///////////////////////////////////////////////////////////////////////////////

class TRemoteWriter::TImpl
    : public TRefCounted
{
public:
    TImpl(
        TRemoteWriterConfigPtr config,
        const TChunkId& chunkId,
        const std::vector<TNodeDescriptor>& targets);

    ~TImpl();

    void Open();

    bool TryWriteBlock(const TSharedRef& block);
    TAsyncError GetReadyEvent();

    TAsyncError AsyncClose(const NChunkClient::NProto::TChunkMeta& chunkMeta);

    const NChunkClient::NProto::TChunkInfo& GetChunkInfo() const;
    std::vector<int> GetWrittenIndexes() const;
    const TChunkId& GetChunkId() const;

    Stroka GetDebugInfo();

private:
    //! A group is a bunch of blocks that is sent in a single RPC request.
    class TGroup;
    typedef TIntrusivePtr<TGroup> TGroupPtr;

    struct TNode;
    typedef TIntrusivePtr<TNode> TNodePtr;
    typedef TWeakPtr<TNode> TNodeWeakPtr;

    typedef ydeque<TGroupPtr> TWindow;

    typedef NChunkClient::TDataNodeServiceProxy TProxy;

    TRemoteWriterConfigPtr Config;
    TNodeDirectoryPtr NodeDirectory;
    TChunkId ChunkId;
    std::vector<TNodeDescriptor> Targets;

    TAsyncStreamState State;

    bool IsOpen;
    bool IsInitComplete;
    bool IsClosing;

    //! This flag is raised whenever #Close is invoked.
    //! All access to this flag happens from #WriterThread.
    bool IsCloseRequested;
    NChunkClient::NProto::TChunkMeta ChunkMeta;

    TWindow Window;
    TAsyncSemaphore WindowSlots;

    std::vector<TNodePtr> Nodes;

    //! Number of nodes that are still alive.
    int AliveNodeCount;

    //! A new group of blocks that is currently being filled in by the client.
    //! All access to this field happens from client thread.
    TGroupPtr CurrentGroup;

    //! Number of blocks that are already added via #AddBlock.
    int BlockCount;

    //! Returned from node in Finish.
    NChunkClient::NProto::TChunkInfo ChunkInfo;

    TMetric StartChunkTiming;
    TMetric PutBlocksTiming;
    TMetric SendBlocksTiming;
    TMetric FlushBlockTiming;
    TMetric FinishChunkTiming;

    NLog::TTaggedLogger Logger;

    void DoClose();

    void AddGroup(TGroupPtr group);

    void RegisterReadyEvent(TFuture<void> windowReady);

    void OnNodeFailed(TNodePtr node, const TError& error);

    void ShiftWindow();

    TProxy::TInvFlushBlock FlushBlock(TNodePtr node, int blockIndex);

    void OnBlockFlushed(TNodePtr node, int blockIndex, TProxy::TRspFlushBlockPtr rsp);

    void OnWindowShifted(int blockIndex);

    TProxy::TInvStartChunk StartChunk(TNodePtr node);

    void OnChunkStarted(TNodePtr node, TProxy::TRspStartChunkPtr rsp);

    void OnSessionStarted();

    void CloseSession();

    TProxy::TInvFinishChunk FinishChunk(TNodePtr node);

    void OnChunkFinished(TNodePtr node, TProxy::TRspFinishChunkPtr rsp);

    void OnSessionFinished();

    void SendPing(TNodeWeakPtr node);
    void StartPing(TNodePtr node);
    void CancelPing(TNodePtr node);
    void CancelAllPings();

    template <class TResponse>
    void CheckResponse(
        TNodePtr node,
        TCallback<void(TIntrusivePtr<TResponse>)> onSuccess,
        TMetric* metric,
        TIntrusivePtr<TResponse> rsp);

    void AddBlock(const TSharedRef& block);

    DECLARE_THREAD_AFFINITY_SLOT(WriterThread);
};

///////////////////////////////////////////////////////////////////////////////

struct TRemoteWriter::TImpl::TNode
    : public TRefCounted
{
    int Index;
    TError Error;
    TNodeDescriptor Descriptor;
    TProxy Proxy;
    TPeriodicInvokerPtr PingInvoker;

    TNode(int index, const TNodeDescriptor& descriptor)
        : Index(index)
        , Descriptor(descriptor)
        , Proxy(NodeChannelCache->GetChannel(descriptor.Address))
    { }

    bool IsAlive() const
    {
        return Error.IsOK();
    }

    void MarkFailed(const TError& error)
    {
        Error = error;
    }
};

///////////////////////////////////////////////////////////////////////////////

class TRemoteWriter::TImpl::TGroup
    : public TRefCounted
{
public:
    TGroup(
        int nodeCount,
        int startBlockIndex,
        TRemoteWriter::TImpl* writer);

    void AddBlock(const TSharedRef& block);
    void Process();
    bool IsWritten() const;
    i64 GetSize() const;

    /*!
     * \note Thread affinity: any.
     */
    int GetStartBlockIndex() const;

    /*!
     * \note Thread affinity: any.
     */
    int GetEndBlockIndex() const;

    /*!
     * \note Thread affinity: WriterThread.
     */
    bool IsFlushing() const;

    /*!
     * \note Thread affinity: WriterThread.
     */
    void SetFlushing();

private:
    bool IsFlushing_;
    std::vector<bool> IsSentTo;

    std::vector<TSharedRef> Blocks;
    int StartBlockIndex;

    i64 Size;

    TWeakPtr<TRemoteWriter::TImpl> Writer;

    NLog::TTaggedLogger& Logger;

    /*!
     * \note Thread affinity: WriterThread.
     */
    void PutGroup();

    /*!
     * \note Thread affinity: WriterThread.
     */
    TProxy::TInvPutBlocks PutBlocks(TNodePtr node);

    /*!
     * \note Thread affinity: WriterThread.
     */
    void OnPutBlocks(TNodePtr node, TProxy::TRspPutBlocksPtr rsp);

    /*!
     * \note Thread affinity: WriterThread.
     */
    void SendGroup(TNodePtr srcNode);

    /*!
     * \note Thread affinity: WriterThread.
     */
    TProxy::TInvSendBlocks SendBlocks(TNodePtr srcNode, TNodePtr dstNode);

    /*!
     * \note Thread affinity: WriterThread.
     */
    void CheckSendResponse(
        TNodePtr srcNode,
        TNodePtr dstNode,
        TProxy::TRspSendBlocksPtr rsp);

    /*!
     * \note Thread affinity: WriterThread.
     */
    void OnSentBlocks(TNodePtr srcNode, TNodePtr dstNode, TProxy::TRspSendBlocksPtr rsp);
};

///////////////////////////////////////////////////////////////////////////////

TRemoteWriter::TImpl::TGroup::TGroup(int nodeCount,
    int startBlockIndex,
    TImpl* writer)
    : IsFlushing_(false)
    , IsSentTo(nodeCount, false)
    , StartBlockIndex(startBlockIndex)
    , Size(0)
    , Writer(writer)
    , Logger(writer->Logger)
{ }

void TRemoteWriter::TImpl::TGroup::AddBlock(const TSharedRef& block)
{
    Blocks.push_back(block);
    Size += block.Size();
}

int TRemoteWriter::TImpl::TGroup::GetStartBlockIndex() const
{
    return StartBlockIndex;
}

int TRemoteWriter::TImpl::TGroup::GetEndBlockIndex() const
{
    return StartBlockIndex + Blocks.size() - 1;
}

i64 TRemoteWriter::TImpl::TGroup::GetSize() const
{
    return Size;
}

bool TRemoteWriter::TImpl::TGroup::IsWritten() const
{
    auto writer = Writer.Lock();
    YCHECK(writer);

    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    for (int nodeIndex = 0; nodeIndex < IsSentTo.size(); ++nodeIndex) {
        if (writer->Nodes[nodeIndex]->IsAlive() && !IsSentTo[nodeIndex]) {
            return false;
        }
    }
    return true;
}

void TRemoteWriter::TImpl::TGroup::PutGroup()
{
    auto writer = Writer.Lock();
    YCHECK(writer);

    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    int nodeIndex = 0;
    while (!writer->Nodes[nodeIndex]->IsAlive()) {
        ++nodeIndex;
        YCHECK(nodeIndex < writer->Nodes.size());
    }

    auto node = writer->Nodes[nodeIndex];
    auto awaiter = New<TParallelAwaiter>(TDispatcher::Get()->GetWriterInvoker());
    auto onSuccess = BIND(
        &TGroup::OnPutBlocks,
        MakeWeak(this),
        node);
    auto onResponse = BIND(
        &TRemoteWriter::TImpl::CheckResponse<TProxy::TRspPutBlocks>,
        Writer,
        node,
        onSuccess,
        &writer->PutBlocksTiming);
    awaiter->Await(PutBlocks(node), onResponse);
    awaiter->Complete(BIND(
        &TRemoteWriter::TImpl::TGroup::Process,
        MakeWeak(this)));
}

TRemoteWriter::TImpl::TProxy::TInvPutBlocks
TRemoteWriter::TImpl::TGroup::PutBlocks(TNodePtr node)
{
    auto writer = Writer.Lock();
    YCHECK(writer);

    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    auto req = node->Proxy.PutBlocks();
    ToProto(req->mutable_chunk_id(), writer->ChunkId);
    req->set_start_block_index(StartBlockIndex);
    req->Attachments().insert(req->Attachments().begin(), Blocks.begin(), Blocks.end());
    req->set_enable_caching(writer->Config->EnableNodeCaching);

    LOG_DEBUG("Putting blocks %d-%d to %s",
        StartBlockIndex,
        GetEndBlockIndex(),
        ~node->Descriptor.Address);

    return req->Invoke();
}

void TRemoteWriter::TImpl::TGroup::OnPutBlocks(TNodePtr node, TProxy::TRspPutBlocksPtr rsp)
{
    auto writer = Writer.Lock();
    if (!writer)
        return;

    UNUSED(rsp);
    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    IsSentTo[node->Index] = true;

    LOG_DEBUG("Blocks %d-%d are put to %s",
        StartBlockIndex,
        GetEndBlockIndex(),
        ~node->Descriptor.Address);
}

void TRemoteWriter::TImpl::TGroup::SendGroup(TNodePtr srcNode)
{
    auto writer = Writer.Lock();
    YCHECK(writer);

    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    for (int dstNodeIndex = 0; dstNodeIndex < IsSentTo.size(); ++dstNodeIndex) {
        auto dstNode = writer->Nodes[dstNodeIndex];
        if (dstNode->IsAlive() && !IsSentTo[dstNodeIndex]) {
            auto awaiter = New<TParallelAwaiter>(TDispatcher::Get()->GetWriterInvoker());
            auto onResponse = BIND(
                &TGroup::CheckSendResponse,
                MakeWeak(this),
                srcNode,
                dstNode);
            awaiter->Await(SendBlocks(srcNode, dstNode), onResponse);
            awaiter->Complete(BIND(&TGroup::Process, MakeWeak(this)));
            break;
        }
    }
}

TRemoteWriter::TImpl::TProxy::TInvSendBlocks
TRemoteWriter::TImpl::TGroup::SendBlocks(
    TNodePtr srcNode,
    TNodePtr dstNode)
{
    auto writer = Writer.Lock();
    YCHECK(writer);

    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    LOG_DEBUG("Sending blocks %d-%d from %s to %s",
        StartBlockIndex,
        GetEndBlockIndex(),
        ~srcNode->Descriptor.Address,
        ~dstNode->Descriptor.Address);

    auto req = srcNode->Proxy.SendBlocks();

    // Set double timeout for SendBlocks since executing it implies another (src->dst) RPC call.
    req->SetTimeout(writer->Config->NodeRpcTimeout + writer->Config->NodeRpcTimeout);
    ToProto(req->mutable_chunk_id(), writer->ChunkId);
    req->set_start_block_index(StartBlockIndex);
    req->set_block_count(Blocks.size());
    ToProto(req->mutable_target(), dstNode->Descriptor);
    return req->Invoke();
}

void TRemoteWriter::TImpl::TGroup::CheckSendResponse(
    TNodePtr srcNode,
    TNodePtr dstNode,
    TRemoteWriter::TImpl::TProxy::TRspSendBlocksPtr rsp)
{
    auto writer = Writer.Lock();
    if (!writer)
        return;

    const auto& error = rsp->GetError();
    if (error.GetCode() == EErrorCode::PipelineFailed) {
        writer->OnNodeFailed(dstNode, error);
        return;
    }

    auto onSuccess = BIND(
        &TGroup::OnSentBlocks,
        Unretained(this), // No need for a smart pointer here -- we're invoking action directly.
        srcNode,
        dstNode);

    writer->CheckResponse<TRemoteWriter::TImpl::TProxy::TRspSendBlocks>(
        srcNode,
        onSuccess,
        &writer->SendBlocksTiming,
        rsp);
}

void TRemoteWriter::TImpl::TGroup::OnSentBlocks(
    TNodePtr srcNode,
    TNodePtr dstNode,
    TProxy::TRspSendBlocksPtr rsp)
{
    auto writer = Writer.Lock();
    YCHECK(writer);

    UNUSED(rsp);
    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    LOG_DEBUG("Blocks %d-%d are sent from %s to %s",
        StartBlockIndex,
        GetEndBlockIndex(),
        ~srcNode->Descriptor.Address,
        ~dstNode->Descriptor.Address);

    IsSentTo[dstNode->Index] = true;
}

bool TRemoteWriter::TImpl::TGroup::IsFlushing() const
{
    auto writer = Writer.Lock();
    YCHECK(writer);

    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    return IsFlushing_;
}

void TRemoteWriter::TImpl::TGroup::SetFlushing()
{
    auto writer = Writer.Lock();
    YCHECK(writer);

    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    IsFlushing_ = true;
}

void TRemoteWriter::TImpl::TGroup::Process()
{
    auto writer = Writer.Lock();
    if (!writer)
        return;

    VERIFY_THREAD_AFFINITY(writer->WriterThread);

    if (!writer->State.IsActive()) {
        return;
    }

    YCHECK(writer->IsInitComplete);

    LOG_DEBUG("Processing blocks %d-%d",
        StartBlockIndex,
        GetEndBlockIndex());

    TNodePtr nodeWithBlocks;
    bool emptyHolderFound = false;
    for (int nodeIndex = 0; nodeIndex < IsSentTo.size(); ++nodeIndex) {
        auto node = writer->Nodes[nodeIndex];
        if (node->IsAlive()) {
            if (IsSentTo[nodeIndex]) {
                nodeWithBlocks = node;
            } else {
                emptyHolderFound = true;
            }
        }
    }

    if (!emptyHolderFound) {
        writer->ShiftWindow();
    } else if (!nodeWithBlocks) {
        PutGroup();
    } else {
        SendGroup(nodeWithBlocks);
    }
}

///////////////////////////////////////////////////////////////////////////////

TRemoteWriter::TImpl::TImpl(
    TRemoteWriterConfigPtr config,
    const TChunkId& chunkId,
    const std::vector<TNodeDescriptor>& targets)
    : Config(config)
    , ChunkId(chunkId)
    , Targets(targets)
    , IsOpen(false)
    , IsInitComplete(false)
    , IsClosing(false)
    , IsCloseRequested(false)
    , WindowSlots(config->SendWindowSize)
    , AliveNodeCount(targets.size())
    , CurrentGroup(New<TGroup>(AliveNodeCount, 0, this))
    , BlockCount(0)
    , StartChunkTiming(0, 1000, 20)
    , PutBlocksTiming(0, 1000, 20)
    , SendBlocksTiming(0, 1000, 20)
    , FlushBlockTiming(0, 1000, 20)
    , FinishChunkTiming(0, 1000, 20)
    , Logger(ChunkWriterLogger)
{
    YCHECK(AliveNodeCount > 0);

    Logger.AddTag(Sprintf("ChunkId: %s", ~ToString(ChunkId)));

    for (int index = 0; index < static_cast<int>(targets.size()); ++index) {
        auto replica = targets[index];
        auto node = New<TNode>(index, Targets[index]);
        node->Proxy.SetDefaultTimeout(Config->NodeRpcTimeout);
        node->PingInvoker = New<TPeriodicInvoker>(
            TDispatcher::Get()->GetWriterInvoker(),
            BIND(&TRemoteWriter::TImpl::SendPing, MakeWeak(this), MakeWeak(~node)),
            Config->NodePingInterval);
        Nodes.push_back(node);
    }
}

TRemoteWriter::TImpl::~TImpl()
{
    VERIFY_THREAD_AFFINITY_ANY();

    // Just a quick check.
    if (!State.IsActive())
        return;

    LOG_INFO("Writer canceled");
    State.Cancel(TError("Writer canceled"));
}

void TRemoteWriter::TImpl::Open()
{
    std::vector<Stroka> targetAddresses;
    FOREACH (const auto& target, Targets) {
        targetAddresses.push_back(target.Address);
    }

    LOG_INFO("Opening writer (Targets: [%s], EnableCaching: %s)",
        ~NYT::JoinToString(targetAddresses),
        ~FormatBool(Config->EnableNodeCaching));

    auto awaiter = New<TParallelAwaiter>(TDispatcher::Get()->GetWriterInvoker());
    FOREACH (auto node, Nodes) {
        auto onSuccess = BIND(
            &TRemoteWriter::TImpl::OnChunkStarted,
            MakeWeak(this),
            node);
        auto onResponse = BIND(
            &TImpl::CheckResponse<TProxy::TRspStartChunk>,
            MakeWeak(this),
            node,
            onSuccess,
            &StartChunkTiming);
        awaiter->Await(StartChunk(node), onResponse);
    }
    awaiter->Complete(BIND(&TImpl::OnSessionStarted, MakeWeak(this)));

    IsOpen = true;
}

void TRemoteWriter::TImpl::ShiftWindow()
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    if (!State.IsActive()) {
        YCHECK(Window.empty());
        return;
    }

    int lastFlushableBlock = -1;
    for (auto it = Window.begin(); it != Window.end(); ++it) {
        auto group = *it;
        if (!group->IsFlushing()) {
            if (group->IsWritten()) {
                lastFlushableBlock = group->GetEndBlockIndex();
                group->SetFlushing();
            } else {
                break;
            }
        }
    }

    if (lastFlushableBlock < 0)
        return;

    auto awaiter = New<TParallelAwaiter>(TDispatcher::Get()->GetWriterInvoker());
    FOREACH (auto node, Nodes) {
        if (node->IsAlive()) {
            auto onSuccess = BIND(
                &TImpl::OnBlockFlushed,
                MakeWeak(this),
                node,
                lastFlushableBlock);
            auto onResponse = BIND(
                &TImpl::CheckResponse<TProxy::TRspFlushBlock>,
                MakeWeak(this),
                node,
                onSuccess,
                &FlushBlockTiming);
            awaiter->Await(FlushBlock(node, lastFlushableBlock), onResponse);
        }
    }

    awaiter->Complete(BIND(
        &TImpl::OnWindowShifted,
        MakeWeak(this),
        lastFlushableBlock));
}

TRemoteWriter::TImpl::TProxy::TInvFlushBlock
TRemoteWriter::TImpl::FlushBlock(TNodePtr node, int blockIndex)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    LOG_DEBUG("Flushing block %d at %s",
        blockIndex,
        ~node->Descriptor.Address);

    auto req = node->Proxy.FlushBlock();
    ToProto(req->mutable_chunk_id(), ChunkId);
    req->set_block_index(blockIndex);
    return req->Invoke();
}

void TRemoteWriter::TImpl::OnBlockFlushed(TNodePtr node, int blockIndex, TProxy::TRspFlushBlockPtr rsp)
{
    UNUSED(rsp);
    VERIFY_THREAD_AFFINITY(WriterThread);

    LOG_DEBUG("Block %d is flushed at %s",
        blockIndex,
        ~node->Descriptor.Address);
}

void TRemoteWriter::TImpl::OnWindowShifted(int lastFlushedBlock)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    if (Window.empty()) {
        // This happens when FlushBlocks responses are reordered
        // (i.e. a larger BlockIndex is flushed before a smaller one)
        // We should prevent repeated calls to CloseSession.
        return;
    }

    while (!Window.empty()) {
        auto group = Window.front();
        if (group->GetEndBlockIndex() > lastFlushedBlock)
            return;

        LOG_DEBUG("Window %d-%d shifted (Size: %" PRId64 ")",
            group->GetStartBlockIndex(),
            group->GetEndBlockIndex(),
            group->GetSize());

        WindowSlots.Release(group->GetSize());
        Window.pop_front();
    }

    if (State.IsActive() && IsCloseRequested) {
        CloseSession();
    }
}

void TRemoteWriter::TImpl::AddGroup(TGroupPtr group)
{
    VERIFY_THREAD_AFFINITY(WriterThread);
    YCHECK(!IsCloseRequested);

    if (!State.IsActive())
        return;

    LOG_DEBUG("Added block group (Group: %p, BlockIndexes: %d-%d)",
        ~group,
        group->GetStartBlockIndex(),
        group->GetEndBlockIndex());

    Window.push_back(group);

    if (IsInitComplete) {
        group->Process();
    }
}

void TRemoteWriter::TImpl::OnNodeFailed(TNodePtr node, const TError& error)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    if (!node->IsAlive())
        return;

    auto wrappedError = TError("Node failed: %s",
        ~node->Descriptor.Address)
        << error;
    LOG_ERROR(wrappedError);

    node->MarkFailed(wrappedError);
    --AliveNodeCount;

    if (State.IsActive() && AliveNodeCount == 0) {
        TError cumulativeError(
            NChunkClient::EErrorCode::AllTargetNodesFailed,
            "All target nodes have failed");
        FOREACH (const auto node, Nodes) {
            YCHECK(!node->IsAlive());
            cumulativeError.InnerErrors().push_back(node->Error);
        }
        LOG_WARNING(cumulativeError, "Chunk writer failed");
        CancelAllPings();
        State.Fail(cumulativeError);
    }
}

template <class TResponse>
void TRemoteWriter::TImpl::CheckResponse(
    TNodePtr node,
    TCallback<void(TIntrusivePtr<TResponse>)> onSuccess,
    TMetric* metric,
    TIntrusivePtr<TResponse> rsp)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    if (!rsp->IsOK()) {
        OnNodeFailed(node, rsp->GetError());
        return;
    }

    metric->AddDelta(rsp->GetStartTime());
    onSuccess.Run(rsp);
}

TRemoteWriter::TImpl::TProxy::TInvStartChunk
TRemoteWriter::TImpl::StartChunk(TNodePtr node)
{
    LOG_DEBUG("Starting chunk session at %s", ~node->Descriptor.Address);

    auto req = node->Proxy.StartChunk();
    ToProto(req->mutable_chunk_id(), ChunkId);
    return req->Invoke();
}

void TRemoteWriter::TImpl::OnChunkStarted(TNodePtr node, TProxy::TRspStartChunkPtr rsp)
{
    UNUSED(rsp);
    VERIFY_THREAD_AFFINITY(WriterThread);

    LOG_DEBUG("Chunk session started at %s", ~node->Descriptor.Address);

    StartPing(node);
}

void TRemoteWriter::TImpl::OnSessionStarted()
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    // Check if the session is not canceled yet.
    if (!State.IsActive()) {
        return;
    }

    LOG_INFO("Writer is ready");

    IsInitComplete = true;
    FOREACH (auto& group, Window) {
        group->Process();
    }

    // Possible for an empty chunk.
    if (Window.empty() && IsCloseRequested) {
        CloseSession();
    }
}

void TRemoteWriter::TImpl::CloseSession()
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    YCHECK(IsCloseRequested);

    LOG_INFO("Closing writer");

    auto awaiter = New<TParallelAwaiter>(TDispatcher::Get()->GetWriterInvoker());
    FOREACH (auto node, Nodes) {
        if (node->IsAlive()) {
            auto onSuccess = BIND(
                &TImpl::OnChunkFinished,
                MakeWeak(this),
                node);
            auto onResponse = BIND(
                &TImpl::CheckResponse<TProxy::TRspFinishChunk>,
                MakeWeak(this),
                node,
                onSuccess,
                &FinishChunkTiming);
            awaiter->Await(FinishChunk(node), onResponse);
        }
    }
    awaiter->Complete(BIND(&TImpl::OnSessionFinished, MakeWeak(this)));
}

void TRemoteWriter::TImpl::OnChunkFinished(TNodePtr node, TProxy::TRspFinishChunkPtr rsp)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    auto& chunkInfo = rsp->chunk_info();
    LOG_DEBUG("Chunk session is finished at %s (Size: %" PRId64 ")",
        ~node->Descriptor.Address,
        chunkInfo.size());

    // If ChunkInfo is set.
    if (ChunkInfo.has_size()) {
        if (ChunkInfo.meta_checksum() != chunkInfo.meta_checksum() ||
            ChunkInfo.size() != chunkInfo.size())
        {
            LOG_FATAL("Mismatched chunk info reported by node (Address: %s, ExpectedInfo: {%s}, ReceivedInfo: {%s})",
                ~node->Descriptor.Address,
                ~ChunkInfo.DebugString(),
                ~chunkInfo.DebugString());
        }
    } else {
        ChunkInfo = chunkInfo;
    }
}

TRemoteWriter::TImpl::TProxy::TInvFinishChunk
TRemoteWriter::TImpl::FinishChunk(TNodePtr node)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    LOG_DEBUG("Finishing chunk session at %s", ~node->Descriptor.Address);

    auto req = node->Proxy.FinishChunk();
    ToProto(req->mutable_chunk_id(), ChunkId);
    *req->mutable_chunk_meta() = ChunkMeta;
    req->set_block_count(BlockCount);
    return req->Invoke();
}

void TRemoteWriter::TImpl::OnSessionFinished()
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    YCHECK(Window.empty());

    if (State.IsActive()) {
        State.Close();
    }

    CancelAllPings();

    LOG_INFO("Writer closed");

    State.FinishOperation();
}

void TRemoteWriter::TImpl::SendPing(TNodeWeakPtr node)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    auto node_ = node.Lock();
    if (!node_) {
        return;
    }

    LOG_DEBUG("Sending ping to %s", ~node_->Descriptor.Address);

    auto req = node_->Proxy.PingSession();
    ToProto(req->mutable_chunk_id(), ChunkId);
    req->Invoke();

    node_->PingInvoker->ScheduleNext();
}

void TRemoteWriter::TImpl::StartPing(TNodePtr node)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    node->PingInvoker->Start();
}

void TRemoteWriter::TImpl::CancelPing(TNodePtr node)
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    node->PingInvoker->Stop();
}

void TRemoteWriter::TImpl::CancelAllPings()
{
    VERIFY_THREAD_AFFINITY(WriterThread);

    FOREACH (auto node, Nodes) {
        CancelPing(node);
    }
}

bool TRemoteWriter::TImpl::TryWriteBlock(const TSharedRef& block)
{
    YCHECK(IsOpen);
    YCHECK(!IsClosing);
    YCHECK(!State.IsClosed());

    if (!WindowSlots.IsReady())
        return false;

    WindowSlots.Acquire(block.Size());
    TDispatcher::Get()->GetWriterInvoker()->Invoke(BIND(
        &TImpl::AddBlock,
        MakeWeak(this),
        block));

    return true;
}

TAsyncError TRemoteWriter::TImpl::GetReadyEvent()
{
    YCHECK(IsOpen);
    YCHECK(!IsClosing);
    YCHECK(!State.HasRunningOperation());
    YCHECK(!State.IsClosed());

    if (!WindowSlots.IsReady()) {
        State.StartOperation();

        auto this_ = MakeStrong(this);
        WindowSlots.GetReadyEvent().Subscribe(BIND([=] () {
            this_->State.FinishOperation(TError());
        }));
    }

    return State.GetOperationError();
}

void TRemoteWriter::TImpl::AddBlock(const TSharedRef& block)
{
    VERIFY_THREAD_AFFINITY(WriterThread);
    YCHECK(!IsCloseRequested);

    if (!State.IsActive())
        return;

    CurrentGroup->AddBlock(block);

    LOG_DEBUG("Added block %d (Group: %p, Size: %" PRISZT ")",
        BlockCount,
        ~CurrentGroup,
        block.Size());

    ++BlockCount;

    if (CurrentGroup->GetSize() >= Config->GroupSize) {
        AddGroup(CurrentGroup);
        // Construct a new (empty) group.
        CurrentGroup = New<TGroup>(Nodes.size(), BlockCount, this);
    }
}

void TRemoteWriter::TImpl::DoClose()
{
    VERIFY_THREAD_AFFINITY(WriterThread);
    YCHECK(!IsCloseRequested);

    LOG_DEBUG("Writer close requested");

    if (!State.IsActive()) {
        State.FinishOperation();
        return;
    }

    if (CurrentGroup->GetSize() > 0) {
        AddGroup(CurrentGroup);
    }

    IsCloseRequested = true;

    if (Window.empty() && IsInitComplete) {
        CloseSession();
    }
}

TAsyncError TRemoteWriter::TImpl::AsyncClose(const NChunkClient::NProto::TChunkMeta& chunkMeta)
{
    YCHECK(IsOpen);
    YCHECK(!IsClosing);
    YCHECK(!State.HasRunningOperation());
    YCHECK(!State.IsClosed());

    IsClosing = true;
    ChunkMeta = chunkMeta;

    LOG_DEBUG("Requesting writer to close");
    State.StartOperation();

    TDispatcher::Get()->GetWriterInvoker()->Invoke(
        BIND(&TImpl::DoClose, MakeWeak(this)));

    return State.GetOperationError();
}

Stroka TRemoteWriter::TImpl::GetDebugInfo()
{
    return Sprintf(
        "ChunkId: %s; "
        "StartChunk: (%s); "
        "FinishChunk timing: (%s); "
        "PutBlocks timing: (%s); "
        "SendBlocks timing: (%s); "
        "FlushBlocks timing: (%s); ",
        ~ToString(ChunkId),
        ~StartChunkTiming.GetDebugInfo(),
        ~FinishChunkTiming.GetDebugInfo(),
        ~PutBlocksTiming.GetDebugInfo(),
        ~SendBlocksTiming.GetDebugInfo(),
        ~FlushBlockTiming.GetDebugInfo());
}

const NChunkClient::NProto::TChunkInfo& TRemoteWriter::TImpl::GetChunkInfo() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return ChunkInfo;
}

std::vector<int> TRemoteWriter::TImpl::GetWrittenIndexes() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    std::vector<int> result;
    FOREACH (auto node, Nodes) {
        if (node->IsAlive()) {
            result.push_back(node->Index);
        }
    }
    return result;
}

const TChunkId& TRemoteWriter::TImpl::GetChunkId() const
{
    return ChunkId;
}

///////////////////////////////////////////////////////////////////////////////

TRemoteWriter::TRemoteWriter(
    TRemoteWriterConfigPtr config,
    const TChunkId& chunkId,
    const std::vector<TNodeDescriptor>& targets)
    : Impl(New<TImpl>(
        config,
        chunkId,
        targets))
{ }

TRemoteWriter::~TRemoteWriter()
{ }

void TRemoteWriter::Open()
{
    Impl->Open();
}

bool TRemoteWriter::TryWriteBlock(const TSharedRef& block)
{
    return Impl->TryWriteBlock(block);
}

TAsyncError TRemoteWriter::GetReadyEvent()
{
    return Impl->GetReadyEvent();
}

TAsyncError TRemoteWriter::AsyncClose(const NChunkClient::NProto::TChunkMeta& chunkMeta)
{
    return Impl->AsyncClose(chunkMeta);
}

const NChunkClient::NProto::TChunkInfo& TRemoteWriter::GetChunkInfo() const
{
    return Impl->GetChunkInfo();
}

std::vector<int> TRemoteWriter::GetWrittenIndexes() const
{
    return Impl->GetWrittenIndexes();
}

const TChunkId& TRemoteWriter::GetChunkId() const
{
    return Impl->GetChunkId();
}

Stroka TRemoteWriter::GetDebugInfo()
{
    return Impl->GetDebugInfo();
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
