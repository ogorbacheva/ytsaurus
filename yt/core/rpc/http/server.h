#pragma once

#include "public.h"

#include <yt/core/http/public.h>

namespace NYT::NRpc::NHttp {

////////////////////////////////////////////////////////////////////////////////

NRpc::IServerPtr CreateServer(NYT::NHttp::IServerPtr httpServer);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc::NHttp
