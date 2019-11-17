#include "fair_share_invoker_pool.h"

#include "scheduler.h"

#include <yt/core/actions/invoker_detail.h>

#include <yt/core/concurrency/rw_spinlock.h>

#include <yt/core/misc/optional.h>
#include <yt/core/misc/ring_queue.h>
#include <yt/core/misc/weak_ptr.h>

#include <yt/core/profiling/timing.h>

#include <utility>

namespace NYT::NConcurrency {

using namespace NProfiling;

////////////////////////////////////////////////////////////////////////////////

class TFairShareCallbackQueue
    : public IFairShareCallbackQueue
{
public:
    explicit TFairShareCallbackQueue(int bucketCount)
        : Buckets_(bucketCount)
        , ExcessTimes_(bucketCount, 0)
    { }

    virtual void Enqueue(TClosure callback, int bucketIndex) override
    {
        auto guard = Guard(Lock_);

        YT_VERIFY(IsValidBucketIndex(bucketIndex));
        Buckets_[bucketIndex].push(std::move(callback));
    }

    virtual bool TryDequeue(TClosure* resultCallback, int* resultBucketIndex) override
    {
        YT_VERIFY(resultCallback != nullptr);
        YT_VERIFY(resultBucketIndex != nullptr);

        auto guard = Guard(Lock_);

        auto optionalBucketIndex = GetStarvingBucketIndex();
        if (!optionalBucketIndex) {
            return false;
        }
        auto bucketIndex = *optionalBucketIndex;

        TruncateExcessTimes(ExcessTimes_[bucketIndex]);

        *resultCallback = std::move(Buckets_[bucketIndex].front());
        Buckets_[bucketIndex].pop();

        *resultBucketIndex = bucketIndex;

        return true;
    }

    virtual void AccountCpuTime(int bucketIndex, TCpuDuration cpuTime) override
    {
        auto guard = Guard(Lock_);

        ExcessTimes_[bucketIndex] += cpuTime;
    }

private:
    using TBuckets = std::vector<TRingQueue<TClosure>>;

    TSpinLock Lock_;

    TBuckets Buckets_;
    std::vector<TCpuDuration> ExcessTimes_;

    std::optional<int> GetStarvingBucketIndex() const
    {
        auto minExcessTime = std::numeric_limits<TCpuDuration>::max();
        std::optional<int> minBucketIndex;
        for (int index = 0; index < Buckets_.size(); ++index) {
            if (Buckets_[index].empty()) {
                continue;
            }
            if (!minBucketIndex || ExcessTimes_[index] < minExcessTime) {
                minExcessTime = ExcessTimes_[index];
                minBucketIndex = index;
            }
        }
        return minBucketIndex;
    }

    void TruncateExcessTimes(TCpuDuration delta)
    {
        for (int index = 0; index < Buckets_.size(); ++index) {
            if (ExcessTimes_[index] >= delta) {
                ExcessTimes_[index] -= delta;
            } else {
                ExcessTimes_[index] = 0;
            }
        }
    }

    bool IsValidBucketIndex(int index) const
    {
        return 0 <= index && index < Buckets_.size();
    }
};

////////////////////////////////////////////////////////////////////////////////

IFairShareCallbackQueuePtr CreateFairShareCallbackQueue(int bucketCount)
{
    YT_VERIFY(0 < bucketCount && bucketCount < 100);
    return New<TFairShareCallbackQueue>(bucketCount);
}

////////////////////////////////////////////////////////////////////////////////

class TFairShareInvokerPool
    : public IInvokerPool
{
public:
    TFairShareInvokerPool(
        IInvokerPtr underlyingInvoker,
        int invokerCount,
        TFairShareCallbackQueueFactory callbackQueueFactory)
        : UnderlyingInvoker_(std::move(underlyingInvoker))
        , Queue_(callbackQueueFactory(invokerCount))
    {
        Invokers_.reserve(invokerCount);
        for (int index = 0; index < invokerCount; ++index) {
            Invokers_.push_back(New<TInvoker>(UnderlyingInvoker_, index, MakeWeak(this)));
        }
        TotalActionCounts_.resize(invokerCount);
        TotalWaitRecords_.resize(invokerCount);
    }

    virtual int GetSize() const override
    {
        return Invokers_.size();
    }

    void Enqueue(TClosure callback, int index)
    {
        Queue_->Enqueue(std::move(callback), index);
        auto now = GetCpuInstant();
        UnderlyingInvoker_->Invoke(BIND(
            &TFairShareInvokerPool::Run,
            MakeStrong(this),
            now));
    }

protected:
    virtual const IInvokerPtr& DoGetInvoker(int index) const override
    {
        YT_VERIFY(IsValidInvokerIndex(index));
        return Invokers_[index];
    }

private:
    const IInvokerPtr UnderlyingInvoker_;

    std::vector<IInvokerPtr> Invokers_;

    struct TWaitRecord
    {
        TCpuInstant RecordTime;
        TCpuDuration Duration;
    };

    const int MaxWaitRecordsPerBucket = 3;
    const TCpuDuration MaxWaitRecordsStorageDuration = DurationToCpuDuration(TDuration::Minutes(2));

    NConcurrency::TReaderWriterSpinLock AverageWaitTimeLock_;
    std::vector<i64> TotalActionCounts_;
    std::vector<std::deque<TWaitRecord>> TotalWaitRecords_;

    IFairShareCallbackQueuePtr Queue_;

    class TCpuTimeAccounter
    {
    public:
        TCpuTimeAccounter(int index, IFairShareCallbackQueue* queue)
            : Index_(index)
            , Queue_(queue)
            , ContextSwitchGuard_(
                /* out */ [this] { Account(); },
                /* in  */ [] { })
        { }

        void Account()
        {
            if (Accounted_) {
                return;
            }
            Accounted_ = true;
            Queue_->AccountCpuTime(Index_, DurationToCpuDuration(Timer_.GetElapsedTime()));
            Timer_.Stop();
        }

        ~TCpuTimeAccounter()
        {
            Account();
        }

    private:
        const int Index_;
        bool Accounted_ = false;
        IFairShareCallbackQueue* Queue_;
        TWallTimer Timer_;
        TContextSwitchGuard ContextSwitchGuard_;
    };

    class TInvoker
        : public TInvokerWrapper
    {
    public:
        TInvoker(IInvokerPtr underlyingInvoker_, int index, TWeakPtr<TFairShareInvokerPool> parent)
            : TInvokerWrapper(std::move(underlyingInvoker_))
            , Index_(index)
            , Parent_(std::move(parent))
        { }

        virtual void Invoke(TClosure callback) override
        {
            if (auto strongParent = Parent_.Lock()) {
                strongParent->Enqueue(std::move(callback), Index_);
            }
        }

        virtual TDuration GetAverageWaitTime() const override
        {
            if (auto strongParent = Parent_.Lock()) {
                TReaderGuard guard(strongParent->AverageWaitTimeLock_);

                auto now = GetCpuInstant();
                auto totalWaitTime = TCpuDuration();
                int count = 0;
                for (auto record : strongParent->TotalWaitRecords_[Index_]) {
                    if (record.RecordTime + strongParent->MaxWaitRecordsStorageDuration >= now) {
                        ++count;
                        totalWaitTime += record.Duration;
                    }
                }
                return count == 0
                    ? TDuration::Zero()
                    : CpuDurationToDuration(totalWaitTime / count);
            }

            return TDuration::Zero();
        }

    private:
        const int Index_;
        const TWeakPtr<TFairShareInvokerPool> Parent_;
    };

    bool IsValidInvokerIndex(int index) const
    {
        return 0 <= index && index < Invokers_.size();
    }

    void Run(TCpuDuration enqueuedAt)
    {
        TClosure callback;
        int bucketIndex = -1;
        YT_VERIFY(Queue_->TryDequeue(&callback, &bucketIndex));
        YT_VERIFY(IsValidInvokerIndex(bucketIndex));

        TCurrentInvokerGuard currentInvokerGuard(Invokers_[bucketIndex]);

        {
            TWriterGuard guard(AverageWaitTimeLock_);

            auto now = GetCpuInstant();
            ++TotalActionCounts_[bucketIndex];

            auto& records = TotalWaitRecords_[bucketIndex];
            records.push_back(TWaitRecord{
                .RecordTime = now,
                .Duration = now - enqueuedAt
            });
            if (records.size() > MaxWaitRecordsPerBucket) {
                records.pop_front();
            }
        }

        {
            TCpuTimeAccounter cpuTimeAccounter(bucketIndex, Queue_.Get());
            callback.Run();
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

IInvokerPoolPtr CreateFairShareInvokerPool(
    IInvokerPtr underlyingInvoker,
    int invokerCount,
    TFairShareCallbackQueueFactory callbackQueueFactory)
{
    YT_VERIFY(0 < invokerCount && invokerCount < 100);
    return New<TFairShareInvokerPool>(
        std::move(underlyingInvoker),
        invokerCount,
        std::move(callbackQueueFactory));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
