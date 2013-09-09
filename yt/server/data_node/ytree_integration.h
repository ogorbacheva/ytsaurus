#pragma once

#include "public.h"

#include <core/ytree/public.h>

namespace NYT {
namespace NDataNode {

////////////////////////////////////////////////////////////////////////////////

NYTree::IYPathServicePtr CreateStoredChunkMapService(
    TChunkStorePtr chunkStore);

NYTree::IYPathServicePtr CreateCachedChunkMapService(
    TChunkCachePtr chunkCache);

///////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
