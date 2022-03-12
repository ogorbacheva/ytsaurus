#pragma once

#include "public.h"
#include "chunk_meta_extensions.h"
#include "columnar_chunk_meta.h"

#include <yt/yt/ytlib/chunk_client/chunk_meta_extensions.h>

#include <yt/yt/ytlib/chunk_client/public.h>
#include <yt/yt/ytlib/node_tracker_client/public.h>
#include <yt/yt/ytlib/new_table_client/prepared_meta.h>

#include <yt/yt/core/misc/memory_usage_tracker.h>
#include <yt/yt/core/misc/atomic_ptr.h>

#include <yt/yt/core/actions/future.h>

#include <memory>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TCachedVersionedChunkMeta
    : public TColumnarChunkMeta
{
public:
    DEFINE_BYREF_RO_PROPERTY(NTableClient::NProto::THunkChunkRefsExt, HunkChunkRefsExt);
    DEFINE_BYREF_RO_PROPERTY(NTableClient::NProto::THunkChunkMetasExt, HunkChunkMetasExt);

    static TCachedVersionedChunkMetaPtr Create(
        bool preparedColumnarMeta,
        const IMemoryUsageTrackerPtr& memoryTracker,
        const NChunkClient::TRefCountedChunkMetaPtr& chunkMeta);

    bool IsColumnarMetaPrepared() const;

    i64 GetMemoryUsage() const override;

    TIntrusivePtr<NNewTableClient::TPreparedChunkMeta> GetPreparedChunkMeta();

    int GetChunkKeyColumnCount() const;

private:
    TCachedVersionedChunkMeta(
        bool prepareColumnarMeta,
        const IMemoryUsageTrackerPtr& memoryTracker,
        const NChunkClient::NProto::TChunkMeta& chunkMeta);

    const bool ColumnarMetaPrepared_;

    TMemoryUsageTrackerGuard MemoryTrackerGuard_;

    TAtomicPtr<NNewTableClient::TPreparedChunkMeta> PreparedMeta_;
    size_t PreparedMetaSize_ = 0;

    DECLARE_NEW_FRIEND();
};

DEFINE_REFCOUNTED_TYPE(TCachedVersionedChunkMeta)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
