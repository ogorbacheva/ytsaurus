#pragma once

#include "public.h"

#include <yt/core/misc/nullable.h>
#include <yt/core/misc/property.h>

#include <yt/core/ypath/public.h>

#include <atomic>

namespace NYT {
namespace NProfiling {

////////////////////////////////////////////////////////////////////////////////

TTagIdList  operator +  (const TTagIdList& a, const TTagIdList& b);
TTagIdList& operator += (TTagIdList& a, const TTagIdList& b);

////////////////////////////////////////////////////////////////////////////////

/*!
 *  - Simple: Measures the interval between start and stop.
 *  This timer creates a single bucket that stores the above interval.
 *
 *  - Sequential: Measures intervals between checkpoints
 *  (start being the first checkpoint) and also the total time (between start and stop).
 *  This timer creates a bucket per each checkpoint plus "total" bucket.
 *
 *  - Parallel: Measures intervals between start and checkpoints
 *  and also the total time (between start and stop).
 *  This timer creates a bucket per each checkpoint plus "total" bucket.
 */
DEFINE_ENUM(ETimerMode,
    (Simple)
    (Sequential)
    (Parallel)
);

//! Timing state.
/*!
 *  Keeps the timing start time and the last checkpoint time.
 *
 *  \note Not thread-safe.
 */
struct TTimer
{
    TTimer();
    TTimer(
        const NYPath::TYPath& path,
        TCpuInstant start,
        ETimerMode mode,
        const TTagIdList& tagIds);

    NYPath::TYPath Path;
    //! Start time.
    TCpuInstant Start;
    //! Last checkpoint time (0 if no checkpoint has occurred yet).
    TCpuInstant LastCheckpoint;
    ETimerMode Mode;
    TTagIdList TagIds;

};

////////////////////////////////////////////////////////////////////////////////

//! Base class for all counters.
/*!
 *  Maintains the profiling path and timing information.
 */
struct TCounterBase
{
    TCounterBase(
        const NYPath::TYPath& path = "",
        const TTagIdList& tagIds = EmptyTagIds,
        TDuration interval = TDuration::MilliSeconds(100));
    TCounterBase(const TCounterBase& other);
    TCounterBase& operator = (const TCounterBase& other);

    TSpinLock SpinLock;
    NYPath::TYPath Path;
    TTagIdList TagIds;
    //! Interval between samples (in ticks).
    TCpuDuration Interval;
    //! The time when the next sample must be queued (in ticks).
    TCpuInstant Deadline;
};

////////////////////////////////////////////////////////////////////////////////

/*!
 * - All: The counter creates three buckets with suffixes "min", "max", and "avg"
 *   and enqueues appropriate aggregates.
 *
 * - Min, Max, Avg: The counter creates a single bucket and enqueues the corresponding
 *   aggregate.
 */
DEFINE_ENUM(EAggregateMode,
    (All)
    (Min)
    (Max)
    (Avg)
);

//! Measures aggregates.
/*!
 *  Used to measure aggregates (min, max, avg) of a rapidly changing value.
 *  The values are aggregated over the time periods specified in the constructor.
 *
 *  \note Thread-safe.
 */
struct TAggregateCounter
    : public TCounterBase
{
    TAggregateCounter(
        const NYPath::TYPath& path = "",
        const TTagIdList& tagIds = EmptyTagIds,
        EAggregateMode mode = EAggregateMode::Max,
        TDuration interval = TDuration::MilliSeconds(1000));
    TAggregateCounter(const TAggregateCounter& other);
    TAggregateCounter& operator = (const TAggregateCounter& other);

    void Reset();

    EAggregateMode Mode;
    TValue Current;
    TValue Min;
    TValue Max;
    TValue Sum;
    int SampleCount;
};

////////////////////////////////////////////////////////////////////////////////

//! A rudimentary but much cheaper version of TAggregateCounter capable of
//! maintaining just the value itself but not any of its aggregates.
struct TSimpleCounter
    : public TCounterBase
{
    TSimpleCounter(
        const NYPath::TYPath& path = "",
        const TTagIdList& tagIds = EmptyTagIds,
        TDuration interval = TDuration::MilliSeconds(100));
    TSimpleCounter(const TSimpleCounter& other);
    TSimpleCounter& operator = (const TSimpleCounter& other);

    std::atomic<TValue> Current;
};

////////////////////////////////////////////////////////////////////////////////

//! Provides a client API for profiling infrastructure.
/*!
 *  A profiler maintains a path prefix that is added automatically all enqueued samples.
 *  It allows new samples to be enqueued and time measurements to be performed.
 */
class TProfiler
{
public:
    //! Constructs a disabled profiler.
    TProfiler();

    //! Constructs a new profiler for a given prefix.
    /*!
     *  By default the profiler is enabled.
     */
    TProfiler(
        const NYPath::TYPath& pathPrefix,
        const TTagIdList& tagIds = EmptyTagIds,
        bool selfProfiling = false);

    //! Path prefix for each enqueued sample.
    DEFINE_BYVAL_RW_PROPERTY(NYPath::TYPath, PathPrefix);

    //! If |false| then no samples are emitted.
    DEFINE_BYVAL_RW_PROPERTY(bool, Enabled);

    //! Tags to append to each enqueued sample.
    DEFINE_BYREF_RW_PROPERTY(TTagIdList, TagIds);


    //! Enqueues a new sample with tags.
    void Enqueue(
        const NYPath::TYPath& path,
        TValue value,
        const TTagIdList& tagIds = EmptyTagIds) const;


    //! Starts time measurement.
    TTimer TimingStart(
        const NYPath::TYPath& path,
        const TTagIdList& tagIds = EmptyTagIds,
        ETimerMode mode = ETimerMode::Simple) const;


    //! Marks a checkpoint and enqueues the corresponding sample.
    /*!
     *  Returns the time passed from the previous duration.
     *
     *  If #timer is in Simple mode then it is automatically
     *  switched to Sequential mode.
     */
    TDuration TimingCheckpoint(TTimer& timer, const TStringBuf& key) const;

    //! Same as above but uses tags instead of keys.
    TDuration TimingCheckpoint(TTimer& timer, const TTagIdList& checkpointTagIds) const;


    //! Stops time measurement and enqueues the "total" sample.
    //! Returns the total duration.
    TDuration TimingStop(TTimer& timer, const TStringBuf& key) const;

    //! Same as above but uses tags instead of keys.
    TDuration TimingStop(TTimer& timer, const TTagIdList& totalTagIds) const;

    //! Same as above but neither tags the point nor changes the path.
    TDuration TimingStop(TTimer& timer) const;


    //! Updates the counter value and possibly enqueues samples.
    void Update(TAggregateCounter& counter, TValue value) const;

    //! Increments the counter value and possibly enqueues aggregate samples.
    //! Returns the incremented value.
    TValue Increment(TAggregateCounter& counter, TValue delta = 1) const;

    //! Updates the counter value and possibly enqueues samples.
    void Update(TSimpleCounter& counter, TValue value) const;

    //! Increments the counter value and possibly enqueues a sample.
    //! Returns the incremented value.
    TValue Increment(TSimpleCounter& counter, TValue delta = 1) const;

private:
    bool SelfProfiling_;


    bool IsCounterEnabled(const TCounterBase& counter) const;

    void DoUpdate(TAggregateCounter& counter, TValue value) const;

    void OnUpdated(TSimpleCounter& counter) const;

    TDuration DoTimingCheckpoint(
        TTimer& timer,
        const TNullable<TStringBuf>& key,
        const TNullable<TTagIdList>& checkpointTagIds) const;

    TDuration DoTimingStop(
        TTimer& timer,
        const TNullable<TStringBuf>& key,
        const TNullable<TTagIdList>& totalTagIds) const;

};

////////////////////////////////////////////////////////////////////////////////

//! A helper guard for measuring time intervals.
/*!
 *  \note
 *  Keep implementation in header to ensure inlining.
 */
class TTimingGuard
    : private TNonCopyable
{
public:
    TTimingGuard(
        const TProfiler* profiler,
        const NYPath::TYPath& path,
        const TTagIdList& tagIds = EmptyTagIds)
        : Profiler_(profiler)
        , Timer_(Profiler_->TimingStart(path, tagIds))
    { }

    TTimingGuard(TTimingGuard&& other)
        : Profiler_(other.Profiler_)
        , Timer_(other.Timer_)
    {
        other.Profiler_ = nullptr;
    }

    ~TTimingGuard()
    {
        // Don't measure anything during exception unwinding.
        if (!std::uncaught_exception() && Profiler_) {
            Profiler_->TimingStop(Timer_);
        }
    }

    void Checkpoint(const TStringBuf& key)
    {
        Profiler_->TimingCheckpoint(Timer_, key);
    }

    //! Needed for PROFILE_TIMING.
    operator bool() const
    {
        return false;
    }

private:
    const TProfiler* Profiler_;
    TTimer Timer_;

};

////////////////////////////////////////////////////////////////////////////////

//! Measures execution time of the statement that immediately follows this macro.
#define PROFILE_TIMING(...) \
    if (auto PROFILE_TIMING__Guard = NYT::NProfiling::TTimingGuard(&Profiler, __VA_ARGS__)) \
    { YUNREACHABLE(); } \
    else

//! Must be used inside #PROFILE_TIMING block to mark a checkpoint.
#define PROFILE_TIMING_CHECKPOINT(key) \
    PROFILE_TIMING__Guard.Checkpoint(key)

////////////////////////////////////////////////////////////////////////////////

TCpuInstant GetCpuInstant();
TValue CpuDurationToValue(TCpuDuration duration);

//! A helper guard for measuring aggregated time intervals.
/*!
 *  \note
 *  Keep implementation in header to ensure inlining.
 */
class TAggregatedTimingGuard
    : private TNonCopyable
{
public:
    TAggregatedTimingGuard(const TProfiler* profiler, TAggregateCounter* counter)
        : Profiler_(profiler)
        , Counter_(counter)
        , Start_(GetCpuInstant())
    {
        YASSERT(profiler);
        YASSERT(counter);
    }

    TAggregatedTimingGuard(TAggregatedTimingGuard&& other)
        : Profiler_(other.Profiler_)
        , Counter_(other.Counter_)
        , Start_(other.Start_)
    {
        other.Profiler_ = nullptr;
    }

    ~TAggregatedTimingGuard()
    {
        // Don't measure anything during exception unwinding.
        if (!std::uncaught_exception() && Profiler_) {
            auto stop = GetCpuInstant();
            auto value = CpuDurationToValue(stop - Start_);
            Profiler_->Update(*Counter_, value);
        }
    }

    operator bool() const
    {
        return false;
    }

private:
    const TProfiler* Profiler_;
    TAggregateCounter* Counter_;
    TCpuInstant Start_;

};

////////////////////////////////////////////////////////////////////////////////

//! Measures aggregated execution time of the statement that immediately follows this macro.
#define PROFILE_AGGREGATED_TIMING(counter) \
    if (auto PROFILE_TIMING__Guard = NYT::NProfiling::TAggregatedTimingGuard(&Profiler, &(counter))) \
    { YUNREACHABLE(); } \
    else

////////////////////////////////////////////////////////////////////////////////

} // namespace NProfiling
} // namespace NYT

