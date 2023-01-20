#pragma once

#include "ref_counted.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TChunkedMemoryPool;

DECLARE_REFCOUNTED_STRUCT(IMemoryChunkProvider)
DECLARE_REFCOUNTED_STRUCT(TSharedRangeHolder)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
