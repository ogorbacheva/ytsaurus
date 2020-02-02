#pragma once

#include "public.h"

#include <yt/core/logging/log.h>

#include <yt/core/profiling/profiler.h>

namespace NYT::NObjectServer {

////////////////////////////////////////////////////////////////////////////////

extern const NLogging::TLogger ObjectServerLogger;
extern const NProfiling::TProfiler ObjectServerProfiler;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(TCacheProfilingCounters)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NObjectServer

