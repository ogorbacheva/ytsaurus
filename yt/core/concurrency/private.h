#pragma once

#include <yt/core/logging/log.h>

#include <yt/core/misc/public.h>

#include <yt/core/profiling/public.h>

namespace NYT::NConcurrency {

////////////////////////////////////////////////////////////////////////////////

class TEventCount;

DECLARE_REFCOUNTED_CLASS(TSchedulerThread)
DECLARE_REFCOUNTED_CLASS(TSingleQueueSchedulerThread)
DECLARE_REFCOUNTED_CLASS(TInvokerQueue)
DECLARE_REFCOUNTED_CLASS(TFairShareInvokerQueue)
DECLARE_REFCOUNTED_STRUCT(IFairShareCallbackQueue);

DEFINE_ENUM(EBeginExecuteResult,
    (Success)
    (QueueEmpty)
    (Terminated)
);

struct TEnqueuedAction
{
    bool Finished = true;
    NProfiling::TCpuInstant EnqueuedAt = 0;
    NProfiling::TCpuInstant StartedAt = 0;
    NProfiling::TCpuInstant FinishedAt = 0;
    TClosure Callback;
};

////////////////////////////////////////////////////////////////////////////////

extern const NLogging::TLogger ConcurrencyLogger;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
