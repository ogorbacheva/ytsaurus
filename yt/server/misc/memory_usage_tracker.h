#pragma once

#include "public.h"

#include <core/misc/error.h>

#include <core/profiling/profiler.h>

#include <core/logging/log.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class ECategory>
class TMemoryUsageTracker
{
public:
    TMemoryUsageTracker(
        i64 totalLimit,
        const std::vector<std::pair<ECategory, i64>>& limits,
        const NLogging::TLogger& logger = NLogging::TLogger(),
        const NProfiling::TProfiler& profiler = NProfiling::TProfiler());

    i64 GetTotalLimit() const;
    i64 GetTotalUsed() const;
    i64 GetTotalFree() const;

    i64 GetLimit(ECategory category) const;
    i64 GetUsed(ECategory category) const;
    i64 GetFree(ECategory category) const;

    // Always succeeds, can lead to an overcommit.
    void Acquire(ECategory category, i64 size);
    TError TryAcquire(ECategory category, i64 size);
    void Release(ECategory category, i64 size);

private:
    TSpinLock SpinLock_;

    const i64 TotalLimit_;

    NProfiling::TAggregateCounter TotalUsedCounter_;
    NProfiling::TAggregateCounter TotalFreeCounter_;

    struct TCategory
    {
        i64 Limit = std::numeric_limits<i64>::max();
        NProfiling::TAggregateCounter UsedCounter;
    };

    TEnumIndexedVector<TCategory, ECategory> Categories_;

    NLogging::TLogger Logger;
    NProfiling::TProfiler Profiler;


    void DoAcquire(ECategory category, i64 size);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define MEMORY_USAGE_TRACKER_INL_H_
#include "memory_usage_tracker-inl.h"
#undef MEMORY_USAGE_TRACKER_INL_H_
