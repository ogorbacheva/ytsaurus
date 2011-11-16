#pragma once

#include "common.h"
#include "chunk_manager.h"

#include "../cypress/cypress_manager.h"
#include "../cypress/node.h"

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

NCypress::INodeTypeHandler::TPtr CreateChunkMapTypeHandler(
    NCypress::TCypressManager* cypressManager,
    TChunkManager* chunkManager);

NCypress::INodeTypeHandler::TPtr CreateChunkListMapTypeHandler(
    NCypress::TCypressManager* cypressManager,
    TChunkManager* chunkManager);

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
