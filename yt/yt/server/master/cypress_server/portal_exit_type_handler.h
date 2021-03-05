#pragma once

#include "public.h"

#include <yt/yt/server/master/cell_master/public.h>

namespace NYT::NCypressServer {

////////////////////////////////////////////////////////////////////////////////

INodeTypeHandlerPtr CreatePortalExitTypeHandler(NCellMaster::TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressServer
