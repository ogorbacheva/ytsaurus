#include "invoker_queue.h"
#include "fair_share_invoker_queue.h"

namespace NYT {
namespace NConcurrency {

using namespace NProfiling;
using namespace NYPath;
using namespace NYTree;

///////////////////////////////////////////////////////////////////////////////

TFairShareInvokerQueue::TFairShareInvokerQueue(
    std::shared_ptr<TEventCount> callbackEventCount,
    const std::vector<NProfiling::TTagIdList> bucketsTagIds,
    bool enableLogging,
    bool enableProfiling)
    : Buckets_(bucketsTagIds.size())
{
    for (size_t index = 0; index < bucketsTagIds.size(); ++index) {
        Buckets_[index].Queue = New<TInvokerQueue>(
            callbackEventCount,
            bucketsTagIds[index],
            enableLogging,
            enableProfiling);
        Buckets_[index].ExcessTime = 0;
    }
}

TFairShareInvokerQueue::~TFairShareInvokerQueue()
{ }

void TFairShareInvokerQueue::SetThreadId(TThreadId threadId)
{
    for (auto& bucket : Buckets_) {
        bucket.Queue->SetThreadId(threadId);
    }
}

IInvokerPtr TFairShareInvokerQueue::GetInvoker(int index)
{
    Y_ASSERT(0 <= index && index < static_cast<int>(Buckets_.size()));
    return Buckets_[index].Queue;
}

void TFairShareInvokerQueue::Shutdown()
{
    for (auto& bucket : Buckets_) {
        bucket.Queue->Shutdown();
    }
}

void TFairShareInvokerQueue::Drain()
{
    for (auto& bucket : Buckets_) {
        bucket.Queue->Drain();
    }
}

bool TFairShareInvokerQueue::IsRunning() const
{
    for (const auto& bucket : Buckets_) {
        if (!bucket.Queue->IsRunning()) {
            return false;
        }
    }
    return true;
}

EBeginExecuteResult TFairShareInvokerQueue::BeginExecute(TEnqueuedAction* action)
{
    YCHECK(!CurrentBucket_);

    // Check if any callback is ready at all.
    CurrentBucket_ = GetStarvingBucket();
    if (!CurrentBucket_) {
        return EBeginExecuteResult::QueueEmpty;
    }

    // Reduce excesses (with truncation).
    auto delta = CurrentBucket_->ExcessTime;
    for (auto& bucket : Buckets_) {
        bucket.ExcessTime = std::max<NProfiling::TCpuDuration>(bucket.ExcessTime - delta, 0);
    }

    // Pump the starving queue.
    StartInstant_ = GetCpuInstant();
    return CurrentBucket_->Queue->BeginExecute(action);
}

void TFairShareInvokerQueue::EndExecute(TEnqueuedAction* action)
{
    if (!CurrentBucket_) {
        return;
    }

    CurrentBucket_->Queue->EndExecute(action);
    CurrentBucket_->ExcessTime += (GetCpuInstant() - StartInstant_);
    CurrentBucket_ = nullptr;
}

TFairShareInvokerQueue::TBucket* TFairShareInvokerQueue::GetStarvingBucket()
{
    // Compute min excess over non-empty queues.
    i64 minExcess = std::numeric_limits<i64>::max();
    TBucket* minBucket = nullptr;
    for (auto& bucket : Buckets_) {
        const auto& queue = bucket.Queue;
        Y_ASSERT(queue);
        if (!queue->IsEmpty()) {
            if (bucket.ExcessTime < minExcess) {
                minExcess = bucket.ExcessTime;
                minBucket = &bucket;
            }
        }
    }
    return minBucket;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NConcurrency
} // namespace NYT

