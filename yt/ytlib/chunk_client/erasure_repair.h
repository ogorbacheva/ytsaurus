#pragma once

#include "public.h"

#include <yt/core/erasure/public.h>

#include <yt/core/actions/callback.h>

namespace NYT {
namespace NChunkClient {

////////////////////////////////////////////////////////////////////////////////

TFuture<void> RepairErasedParts(
    NErasure::ICodec* codec,
    const NErasure::TPartIndexList& erasedIndices,
    const std::vector<IChunkReaderPtr>& readers,
    const std::vector<IChunkWriterPtr>& writers,
    const TWorkloadDescriptor& workloadDescriptor);

////////////////////////////////////////////////////////////////////////////////

IChunkReaderPtr CreateRepairingErasureReader(
    NErasure::ICodec* codec,
    const NErasure::TPartIndexList& erasedIndices,
    // This list must consist of readers for all data parts and repair parts sorted by part index.
    const std::vector<IChunkReaderPtr>& readers);

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
