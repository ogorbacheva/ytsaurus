#pragma once

#include "public.h"
#include "chunk_pool.h"

namespace NYT {
namespace NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

i64 GetFootprintMemorySize();
i64 GetLFAllocBufferSize();

i64 GetInputIOMemorySize(
    NScheduler::TJobIOConfigPtr ioConfig,
    const TChunkStripeStatistics& stat);

i64 GetSortInputIOMemorySize(const TChunkStripeStatistics& stat);

i64 GetIntermediateOutputIOMemorySize(NScheduler::TJobIOConfigPtr ioConfig);

i64 GetOutputWindowMemorySize(NScheduler::TJobIOConfigPtr ioConfig);

////////////////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT

