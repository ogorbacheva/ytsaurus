#pragma once

#include <yt/core/rpc/protocol_version.h>

namespace NYT::NApi::NRpcProxy {

////////////////////////////////////////////////////////////////////////////////

NRpc::TProtocolVersion GetCurrentProtocolVersion();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
