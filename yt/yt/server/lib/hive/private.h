#pragma once

#include "public.h"

#include <yt/server/lib/hydra/public.h>

#include <yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NHiveServer {

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENTITY_TYPE(TCommit, TTransactionId, ::THash<TTransactionId>)
class TAbort;

////////////////////////////////////////////////////////////////////////////////

extern const NLogging::TLogger HiveServerLogger;
extern const NProfiling::TRegistry HiveServerProfiler;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHiveServer
