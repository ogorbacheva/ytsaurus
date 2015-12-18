#pragma once

#include "public.h"

#include <yt/core/rpc/public.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateJobProberService(TJobProxyPtr jobProxy);

////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT