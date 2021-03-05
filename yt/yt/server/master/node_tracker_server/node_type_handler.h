#pragma once

#include "public.h"

#include <yt/yt/server/master/cell_master/public.h>

#include <yt/yt/server/master/object_server/public.h>

#include <yt/yt/server/lib/hydra/public.h>

namespace NYT::NNodeTrackerServer {

////////////////////////////////////////////////////////////////////////////////

NObjectServer::IObjectTypeHandlerPtr CreateNodeTypeHandler(
    NCellMaster::TBootstrap* bootstrap,
    NHydra::TEntityMap<TNode>* map);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerServer
