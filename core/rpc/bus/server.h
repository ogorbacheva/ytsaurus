#pragma once

#include "public.h"

#include <yt/core/bus/public.h>

namespace NYT {
namespace NRpc {
namespace NBus {

////////////////////////////////////////////////////////////////////////////////

IServerPtr CreateBusServer(NYT::NBus::IBusServerPtr busServer);

////////////////////////////////////////////////////////////////////////////////

} // namespace NBus
} // namespace NRpc
} // namespace NYT
