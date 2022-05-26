#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

namespace NYT::NCellServer {

////////////////////////////////////////////////////////////////////////////////

inline const NLogging::TLogger CellServerLogger("CellServer");

constexpr int MaxAreaCount = 100;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellServer
