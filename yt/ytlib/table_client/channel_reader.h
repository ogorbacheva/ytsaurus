﻿#pragma once

#include "public.h"
#include <ytlib/chunk_client/schema.h>

#include <core/misc/ref.h>
#include <util/stream/mem.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TChannelReader
    : public virtual TRefCounted
{
public:
    explicit TChannelReader(const NChunkClient::TChannel& channel);
    void SetBlock(const TSharedRef& block);

    bool NextRow();
    bool NextColumn();

    TStringBuf GetColumn() const;
    const TStringBuf& GetValue() const;

private:
    const NChunkClient::TChannel Channel;

    TSharedRef CurrentBlock;

    std::vector<TMemoryInput> ColumnBuffers;

    int CurrentColumnIndex;
    TStringBuf CurrentColumn;
    TStringBuf CurrentValue;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT

