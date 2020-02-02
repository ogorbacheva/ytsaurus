#pragma once

#include "public.h"

#include <yt/server/node/cell_node/public.h>

#include <yt/core/rpc/public.h>

namespace NYT::NTabletNode {

////////////////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateTabletCellService(NCellNode::TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode
