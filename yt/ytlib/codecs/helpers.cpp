#include "stdafx.h"
#include "helpers.h"

#include <ytlib/misc/foreach.h>

namespace NYT {
namespace NCodec {

struct TCodecBlockTag { };

////////////////////////////////////////////////////////////////////////////////

size_t GetTotalSize(const std::vector<TSharedRef>& refs)
{
    size_t size = 0;
    FOREACH (const auto& ref, refs) {
        size += ref.Size();
    }
    return size;
}

TSharedRef MergeRefs(const std::vector<TSharedRef>& blocks)
{
    size_t size = GetTotalSize(blocks);
    struct TMergedBlockTag { };
    auto result = TSharedRef::Allocate<TMergedBlockTag>(size);
    size_t pos = 0;
    FOREACH (const auto& block, blocks) {
        std::copy(block.Begin(), block.End(), result.Begin() + pos);
        pos += block.Size();
    }
    return result;
}

TSharedRef Apply(TConverter converter, const TSharedRef& ref)
{
    ByteArraySource source(ref.Begin(), ref.Size());
    TBlob output;
    converter.Run(&source, &output);
    return TSharedRef::FromBlob<TCodecBlockTag>(std::move(output));
}

TSharedRef Apply(TConverter converter, const std::vector<TSharedRef>& refs)
{
    if (refs.size() == 1) {
        return Apply(converter, refs.front());
    }
    TVectorRefsSource source(refs);
    TBlob output;
    converter.Run(&source, &output);
    return TSharedRef::FromBlob<TCodecBlockTag>(std::move(output));
}

////////////////////////////////////////////////////////////////////////////////

TVectorRefsSource::TVectorRefsSource(const std::vector<TSharedRef>& blocks)
    : Blocks_(blocks)
    , Available_(GetTotalSize(blocks))
    , Index_(0)
    , Position_(0)
{
    SkipCompletedBlocks();
}

size_t TVectorRefsSource::Available() const
{
    return Available_;
}

const char* TVectorRefsSource::Peek(size_t* len)
{
    if (Index_ == Blocks_.size()) {
        *len = 0;
        return NULL;
    }
    *len = Blocks_[Index_].Size() - Position_;
    return Blocks_[Index_].Begin() + Position_;
}

void TVectorRefsSource::Skip(size_t n)
{
    while (n > 0 && Index_ < Blocks_.size()) {
        size_t toSkip = std::min(Blocks_[Index_].Size() - Position_, n);

        Position_ += toSkip;
        SkipCompletedBlocks();

        n -= toSkip;
        Available_ -= toSkip;
    }
}

void TVectorRefsSource::SkipCompletedBlocks()
{
    while (Index_ < Blocks_.size() && Position_ == Blocks_[Index_].Size()) {
        Index_ += 1;
        Position_ = 0;
    }
}

////////////////////////////////////////////////////////////////////////////////

TDynamicByteArraySink::TDynamicByteArraySink(std::vector<char>* output)
    : Output_(output)
{ }

void TDynamicByteArraySink::Append(const char* data, size_t n)
{
    size_t newSize = Output_->size() + n;
    if (newSize > Output_->capacity()) {
        Output_->reserve(std::max(Output_->capacity() * 2, newSize));
    }

    auto outputPointer = Output_->data() + Output_->size();
    Output_->resize(newSize);
    std::copy(data, data + n, outputPointer);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCodec
} // namespace NYT
