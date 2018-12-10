#pragma once

#include "public.h"

#include <yt/core/rpc/public.h>

namespace NYT::NHydra {

////////////////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateLocalSnapshotService(
    const NElection::TCellId& cellId,
    TFileSnapshotStorePtr fileStore);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
