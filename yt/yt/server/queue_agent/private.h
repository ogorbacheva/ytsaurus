#pragma once

#include <yt/yt/core/logging/log.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NQueueAgent {

////////////////////////////////////////////////////////////////////////////////

inline const NLogging::TLogger QueueAgentLogger("QueueAgent");
inline const NProfiling::TProfiler QueueAgentProfiler("/queue_agent");

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TQueueAgent)
DECLARE_REFCOUNTED_CLASS(TQueueAgentConfig)
DECLARE_REFCOUNTED_CLASS(TQueueAgentServerConfig)

////////////////////////////////////////////////////////////////////////////////

using TAgentId = TString;

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TQueueTable)
DECLARE_REFCOUNTED_CLASS(TStateTracker)

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(IQueueController)

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EQueueType,
    //! Regular ordered dynamic table.
    ((OrderedDynamicTable)        (1))
)

////////////////////////////////////////////////////////////////////////////////

struct TQueueTableRow;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueueAgent
