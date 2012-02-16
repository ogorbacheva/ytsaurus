#pragma once

#include "common.h"
#include "chunk_store.h"
#include "chunk_cache.h"
#include "session_manager.h"

#include <ytlib/ytree/ypath_service.h>

namespace NYT {
namespace NChunkHolder {

////////////////////////////////////////////////////////////////////////////////

NYTree::TYPathServicePtr CreateStoredChunkMapService(
    TChunkStore* chunkStore);

NYTree::TYPathServicePtr CreateCachedChunkMapService(
    TChunkCache* chunkCache);

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkHolder
} // namespace NYT
