#pragma once

#include <yt/yt/library/profiling/impl.h>
#include <yt/yt/library/profiling/summary.h>

namespace NYT::NProfiling {

////////////////////////////////////////////////////////////////////////////////

constexpr size_t CacheLineSize = 64;

////////////////////////////////////////////////////////////////////////////////

class TPerCpuCounter
    : public ICounterImpl
{
public:
    virtual void Increment(i64 delta) override;
    
    i64 GetValue();

private:
    struct alignas(CacheLineSize) TShard
    {
        std::atomic<i64> Value = 0;
    };

    std::array<TShard, TTscp::MaxProcessorId> Shards_;
};

static_assert(sizeof(TPerCpuCounter) == 64 + 64 * 64);

////////////////////////////////////////////////////////////////////////////////

class TPerCpuTimeCounter
    : public ITimeCounterImpl
{
public:
    virtual void Add(TDuration delta) override;
    
    virtual TDuration GetValue() override;

private:
    struct alignas(CacheLineSize) TShard
    {
        std::atomic<TDuration::TValue> Value = 0;
    };

    std::array<TShard, TTscp::MaxProcessorId> Shards_;
};

static_assert(sizeof(TPerCpuCounter) == 64 + 64 * 64);

////////////////////////////////////////////////////////////////////////////////

class TPerCpuGauge
    : public IGaugeImpl
{
public:
    virtual void Update(double value) override;

    double GetValue();

private:
    struct TWrite
    {
        double Value = 0.0;
        TCpuInstant Timestamp = 0;
    };

    struct alignas(CacheLineSize) TShard
    {
        std::atomic<TWrite> Value = {};
    };

    static_assert(std::atomic<TWrite>::is_always_lock_free);

    std::array<TShard, TTscp::MaxProcessorId> Shards_;
};

static_assert(sizeof(TPerCpuCounter) == 64 + 64 * 64);

////////////////////////////////////////////////////////////////////////////////

template <class T>
class TPerCpuSummary
    : public ISummaryImplBase<T>
{
public:
    virtual void Record(T value) override;

    virtual TSummarySnapshot<T> GetValue() override;
    virtual TSummarySnapshot<T> GetValueAndReset() override;

private:
    struct alignas(CacheLineSize) TShard
    {
        TSpinLock Lock;
        TSummarySnapshot<T> Value;
    };

    std::array<TShard, TTscp::MaxProcessorId> Shards_;
};

DEFINE_REFCOUNTED_TYPE(TPerCpuSummary<double>);
DEFINE_REFCOUNTED_TYPE(TPerCpuSummary<TDuration>);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NProfiling
