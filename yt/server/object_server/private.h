#pragma once

#include "public.h"

#include <core/logging/log.h>

#include <core/profiling/profiler.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

extern const NLogging::TLogger ObjectServerLogger;
extern const NProfiling::TProfiler ObjectServerProfiler;

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT

