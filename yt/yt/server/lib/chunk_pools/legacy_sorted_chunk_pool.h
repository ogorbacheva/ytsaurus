#pragma once

#include "sorted_chunk_pool.h"

namespace NYT::NChunkPools {

////////////////////////////////////////////////////////////////////////////////

ISortedChunkPoolPtr CreateLegacySortedChunkPool(
    const TSortedChunkPoolOptions& options,
    IChunkSliceFetcherFactoryPtr chunkSliceFetcherFactory,
    TInputStreamDirectory dataSourceDirectory);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkPools
