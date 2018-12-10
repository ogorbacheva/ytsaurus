#pragma once

#include "public.h"

#include <yt/client/api/public.h>

namespace NYT::NApi::NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

NApi::IConnectionPtr CreateConnection(
    TConnectionConfigPtr config);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
