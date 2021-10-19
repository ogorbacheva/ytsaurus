#include "thread_pool_detail.h"

#include <yt/yt/core/actions/invoker_util.h>

#include <algorithm>

namespace NYT::NConcurrency {

////////////////////////////////////////////////////////////////////////////////

TThreadPoolBase::TThreadPoolBase(TString threadNamePrefix)
    : ThreadNamePrefix_(std::move(threadNamePrefix))
    , ShutdownCookie_(RegisterShutdownCallback(
        Format("ThreadPool(%v)", ThreadNamePrefix_),
        BIND(&TThreadPoolBase::Shutdown, MakeWeak(this)),
        /*priority*/ 100))
    , FinalizerInvoker_(GetFinalizerInvoker())
{ }

void TThreadPoolBase::Configure(int threadCount)
{
    DoConfigure(std::clamp(threadCount, 1, MaxThreadCount));
}

void TThreadPoolBase::Shutdown()
{
    if (!ShutdownFlag_.exchange(true)) {
        StartFlag_ = true;
        DoShutdown();
    }
}

void TThreadPoolBase::EnsureStarted()
{
    if (!StartFlag_.exchange(true)) {
        DoStart();
    }
}

TString TThreadPoolBase::MakeThreadName(int index)
{
    return Format("%v:%v", ThreadNamePrefix_, index);
}

void TThreadPoolBase::DoStart()
{
    decltype(Threads_) threads;
    {
        auto guard = Guard(SpinLock_);
        threads = Threads_;
    }

    for (const auto& thread : threads) {
        thread->Start();
    }
}

void TThreadPoolBase::DoShutdown()
{
    FinalizerInvoker_->Invoke(MakeFinalizerCallback());
    FinalizerInvoker_.Reset();
}

TClosure TThreadPoolBase::MakeFinalizerCallback()
{
    decltype(Threads_) threads;
    {
        auto guard = Guard(SpinLock_);
        std::swap(threads, Threads_);
    }

    return BIND([threads = std::move(threads)] () {
        for (const auto& thread : threads) {
            thread->Stop();
        }
    });
}

int TThreadPoolBase::GetThreadCount()
{
    auto guard = Guard(SpinLock_);
    return std::ssize(Threads_);
}

void TThreadPoolBase::DoConfigure(int threadCount)
{
    decltype(Threads_) threadsToStart;
    decltype(Threads_) threadsToStop;
    {
        auto guard = Guard(SpinLock_);

        while (std::ssize(Threads_) < threadCount) {
            auto thread = SpawnThread(std::ssize(Threads_));
            threadsToStart.push_back(thread);
            Threads_.push_back(thread);
        }

        while (std::ssize(Threads_) > threadCount) {
            threadsToStop.push_back(Threads_.back());
            Threads_.pop_back();
        }
    }

    for (const auto& thread : threadsToStop) {
        thread->Stop();
    }

    StartFlag_.store(false);
    EnsureStarted();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency
