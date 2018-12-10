#pragma once

#include "public.h"

#include <yt/client/api/rpc_proxy/public.h>

#include <yt/core/rpc/public.h>

namespace NYT::NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateApiService(
    TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpcProxy
