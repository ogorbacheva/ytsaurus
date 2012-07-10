﻿#include "stdafx.h"
#include "encoding_writer.h"

#include "config.h"
#include "private.h"
#include "async_writer.h"

namespace NYT {
namespace NChunkClient {

///////////////////////////////////////////////////////////////////////////////

TEncodingWriter::TEncodingWriter(TEncodingWriterConfigPtr config, IAsyncWriterPtr asyncWriter)
    : Config(config)
    , AsyncWriter(asyncWriter)
    , Semaphore(Config->WindowSize)
    , Codec(GetCodec(Config->CodecId))
    , UncompressedSize_(0)
    , CompressedSize_(0)
    , CompressionRatio_(config->DefaultCompressionRatio)
    , CompressNext(BIND(&TEncodingWriter::Compress, MakeWeak(this)).Via(WriterThread->GetInvoker()))
    , WritePending(
        BIND(
            &TEncodingWriter::WritePendingBlocks, 
            MakeWeak(this))
        .Via(WriterThread->GetInvoker()))
{}

void TEncodingWriter::WriteBlock(const TSharedRef& block)
{
    Semaphore.Acquire(block.Size());
    auto invokeCompression = CompressionTasks.IsEmpty();

    CompressionTasks.Enqueue(BIND(
        &TEncodingWriter::DoCompressBlock, 
        MakeStrong(this),
        block));

    if (invokeCompression) {
        CompressNext.Run();
    }
}

void TEncodingWriter::WriteBlock(std::vector<TSharedRef>&& vectorizedBlock)
{
    FOREACH(const auto& part, vectorizedBlock) {
        Semaphore.Acquire(part.Size());
    }

    auto invokeCompression = CompressionTasks.IsEmpty();

    CompressionTasks.Enqueue(BIND(
        &TEncodingWriter::DoCompressVector, 
        MakeWeak(this),
        MoveRV(vectorizedBlock)));

    if (invokeCompression) {
        CompressNext.Run();
    }
}

void TEncodingWriter::Compress()
{
    TClosure task;
    if (CompressionTasks.Dequeue(&task)) {
        task.Run();
        if (!CompressionTasks.IsEmpty()) {
            CompressNext.Run();
        }
    }
}

void TEncodingWriter::DoCompressBlock(const TSharedRef& block)
{
    auto compressedBlock = Codec->Compress(block);

    UncompressedSize_ += block.Size();
    CompressedSize_ += compressedBlock.Size();

    int delta = block.Size();
    delta -= compressedBlock.Size();

    ProcessCompressedBlock(compressedBlock, delta);
}

void TEncodingWriter::DoCompressVector(const std::vector<TSharedRef>& vectorizedBlock)
{
    auto compressedBlock = Codec->Compress(vectorizedBlock);

    auto oldSize = GetUncompressedSize();
    FOREACH(const auto& part, vectorizedBlock) {
        UncompressedSize_ += part.Size();
    }

    CompressedSize_ += compressedBlock.Size();

    int delta = UncompressedSize_ - oldSize;
    delta -= compressedBlock.Size();

    ProcessCompressedBlock(compressedBlock, delta);
}

void TEncodingWriter::ProcessCompressedBlock(const TSharedRef& block, int delta)
{
    CompressionRatio_ = double(CompressedSize_) / UncompressedSize_;

    if (delta > 0) {
        Semaphore.Release(delta);
    } else {
        Semaphore.Acquire(-delta);
    }

    PendingBlocks.push_back(block);

    if (PendingBlocks.size() == 1) {
        AsyncWriter->GetReadyEvent().Subscribe(WritePending);
    }
}

void TEncodingWriter::WritePendingBlocks(TError error)
{
    if (error.IsOK()) {
        State.Fail(error);
        return;
    }

    while (!PendingBlocks.empty()) {
        auto& front = PendingBlocks.front();
        if (AsyncWriter->TryWriteBlock(front)) {
            Semaphore.Release(front.Size());
            PendingBlocks.pop_front();
        } else {
            AsyncWriter->GetReadyEvent().Subscribe(WritePending);
        };
    }
}

bool TEncodingWriter::IsReady() const
{
    return Semaphore.IsReady() && State.IsActive();
}

TAsyncError TEncodingWriter::GetReadyEvent()
{
    if (!Semaphore.IsReady()) {
        State.StartOperation();

        auto this_ = MakeStrong(this);
        Semaphore.GetReadyEvent().Subscribe(BIND([=] () {
            this_->State.FinishOperation();
        }));
    }

    return State.GetOperationError();
}

TAsyncError TEncodingWriter::AsyncFlush()
{
    State.StartOperation();

    auto this_ = MakeStrong(this);
    Semaphore.GetFreeEvent().Subscribe(BIND([=] () {
        this_->State.FinishOperation();
    }));

    return State.GetOperationError();
}


///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
