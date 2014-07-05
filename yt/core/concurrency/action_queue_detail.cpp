#include "stdafx.h"
#include "action_queue_detail.h"
#include "fiber.h"
#include "scheduler.h"
#include "thread.h"

#include <core/actions/invoker_util.h>

#include <core/ypath/token.h>

#include <core/logging/log.h>

#include <core/profiling/timing.h>

#include <util/system/sigset.h>

namespace NYT {
namespace NConcurrency {

using namespace NYPath;
using namespace NProfiling;

///////////////////////////////////////////////////////////////////////////////

static NLog::TLogger Logger("ActionQueue");

///////////////////////////////////////////////////////////////////////////////

TInvokerQueue::TInvokerQueue(
    TEventCount* eventCount,
    const NProfiling::TTagIdList& tagIds,
    bool enableLogging,
    bool enableProfiling)
    : EventCount(eventCount)
    , ThreadId(InvalidThreadId)
    , EnableLogging(enableLogging)
    // XXX(babenko): VS2013 Nov CTP does not have a proper ctor :(
    // , Running(true)
    , Profiler("/action_queue")
    , EnqueueCounter("/enqueue_rate", tagIds)
    , DequeueCounter("/dequeue_rate", tagIds)
    , QueueSize(0)
    , QueueSizeCounter("/size", tagIds)
    , WaitTimeCounter("/time/wait", tagIds)
    , ExecTimeCounter("/time/exec", tagIds)
    , TotalTimeCounter("/time/total", tagIds)
{
    Running.store(true, std::memory_order_relaxed);
    Profiler.SetEnabled(enableProfiling);
}

void TInvokerQueue::SetThreadId(TThreadId threadId)
{
    ThreadId = threadId;
}

void TInvokerQueue::Invoke(const TClosure& callback)
{
    if (!Running.load(std::memory_order_relaxed)) {
        LOG_TRACE_IF(
            EnableLogging,
            "Queue had been shut down, incoming action ignored: %p",
            callback.GetHandle());
        return;
    }

    ++QueueSize;
    Profiler.Increment(EnqueueCounter);

    LOG_TRACE_IF(EnableLogging, "Callback enqueued: %p",
        callback.GetHandle());

    TEnqueuedAction action;
    action.Finished = false;
    action.EnqueuedAt = GetCpuInstant();
    action.Callback = std::move(callback);
    Queue.Enqueue(action);

    EventCount->Notify();
}

TThreadId TInvokerQueue::GetThreadId() const
{
    return ThreadId;
}

void TInvokerQueue::Shutdown()
{
    Running.store(false, std::memory_order_relaxed);
}

bool TInvokerQueue::IsRunning() const
{
    return Running;
}

EBeginExecuteResult TInvokerQueue::BeginExecute(TEnqueuedAction* action)
{
    YASSERT(action->Finished);

    if (!Queue.Dequeue(action)) {
        return EBeginExecuteResult::QueueEmpty;
    }

    EventCount->CancelWait();

    Profiler.Increment(DequeueCounter);

    action->StartedAt = GetCpuInstant();
    Profiler.Aggregate(
        WaitTimeCounter,
        CpuDurationToValue(action->StartedAt - action->EnqueuedAt));

    // Move callback to the stack frame to ensure that we hold it as hold as it runs.
    auto callback = std::move(action->Callback);
    try {
        TCurrentInvokerGuard guard(this);
        callback.Run();
    } catch (const TFiberCanceledException&) {
        // Still consider this a success.
        // This caller is responsible for terminating the current fiber.
    }

    return EBeginExecuteResult::Success;
}

void TInvokerQueue::EndExecute(TEnqueuedAction* action)
{
    if (action->Finished)
        return;

    int size = --QueueSize;
    Profiler.Aggregate(QueueSizeCounter, size);

    auto endedAt = GetCpuInstant();
    Profiler.Aggregate(
        ExecTimeCounter,
        CpuDurationToValue(endedAt - action->StartedAt));
    Profiler.Aggregate(
        TotalTimeCounter,
        CpuDurationToValue(endedAt - action->EnqueuedAt));

    action->Finished = true;
}

int TInvokerQueue::GetSize() const
{
    return QueueSize.load();
}

bool TInvokerQueue::IsEmpty() const
{
    return const_cast<TLockFreeQueue<TEnqueuedAction>&>(Queue).IsEmpty();
}

///////////////////////////////////////////////////////////////////////////////

TSchedulerThread::TSchedulerThread(
    TEventCount* eventCount,
    const Stroka& threadName,
    const NProfiling::TTagIdList& tagIds,
    bool enableLogging,
    bool enableProfiling)
    : EventCount(eventCount)
    , ThreadName(threadName)
    , EnableLogging(enableLogging)
    , Profiler("/action_queue", tagIds)
    , Started(NewPromise())
    , ThreadId(InvalidThreadId)
    , Thread(ThreadMain, (void*) this)
    , FibersCreated(0)
    , FibersAlive(0)
{
    Epoch.store(0, std::memory_order_relaxed);
    Profiler.SetEnabled(enableProfiling);
}

TSchedulerThread::~TSchedulerThread()
{
    YCHECK(!IsRunning());
    Thread.Detach();
}

void TSchedulerThread::Start()
{
    Epoch.fetch_or((unsigned int)0x1, std::memory_order_relaxed);

    LOG_DEBUG_IF(EnableLogging, "Starting thread (Name: %s)",
        ~ThreadName);

    Thread.Start();

    OnStart();

    Started.Get();
}

void TSchedulerThread::Shutdown()
{
    if (!IsRunning()) {
        return;
    }

    LOG_DEBUG_IF(EnableLogging, "Stopping thread (Name: %s)",
        ~ThreadName);

    Epoch.fetch_and(~((unsigned int)0x1), std::memory_order_relaxed);

    EventCount->NotifyAll();

    OnShutdown();

    // Prevent deadlock.
    if (GetCurrentThreadId() != ThreadId) {
        Thread.Join();
    }
}

void* TSchedulerThread::ThreadMain(void* opaque)
{
    static_cast<TSchedulerThread*>(opaque)->ThreadMain();
    return nullptr;
}

void TSchedulerThread::ThreadMain()
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    TCurrentSchedulerGuard guard(this);
    ThreadId = GetCurrentThreadId();
    SetCurrentThreadName(~ThreadName);

    // Hold this strongly.
    auto this_ = MakeStrong(this);

    try {
        OnThreadStart();
        Started.Set();

        while (IsRunning()) {
            ThreadMainStep();
        }

        OnThreadShutdown();
        LOG_DEBUG_IF(EnableLogging, "Thread stopped (Name: %s)",
            ~ThreadName);
    } catch (const std::exception& ex) {
        LOG_FATAL(ex, "Unhandled exception in executor thread (Name: %s)",
            ~ThreadName);
    }
}

void TSchedulerThread::ThreadMainStep()
{
    YASSERT(!CurrentFiber);

    if (RunQueue.empty()) {
        // Spawn a new idle fiber to run the loop.
        YASSERT(!IdleFiber);
        IdleFiber = New<TFiber>(BIND(
            &TSchedulerThread::FiberMain,
            MakeStrong(this),
            Epoch.load(std::memory_order_relaxed)));

        RunQueue.push_back(IdleFiber);
    }

    YASSERT(!RunQueue.empty());

    CurrentFiber = std::move(RunQueue.front());
    RunQueue.pop_front();

    YCHECK(CurrentFiber->GetState() == EFiberState::Suspended);

    CurrentFiber->SetState(EFiberState::Running);
    SwitchExecutionContext(
        &SchedulerContext,
        CurrentFiber->GetContext(),
        /* as per FiberTrampoline */ CurrentFiber.Get());

    // Notify context switch subscribers.
    OnContextSwitch();

    switch (CurrentFiber->GetState()) {
        case EFiberState::Sleeping:
            // Advance epoch as this (idle) fiber might be rescheduled elsewhere.
            if (CurrentFiber == IdleFiber) {
                Epoch.fetch_add(0x2, std::memory_order_relaxed);
                IdleFiber.Reset();
            }
            // Reschedule this fiber.
            Reschedule(
                std::move(CurrentFiber),
                std::move(WaitForFuture),
                std::move(SwitchToInvoker));
            break;

        case EFiberState::Suspended:
            // Reschedule this fiber to be executed later.
            RunQueue.emplace_back(std::move(CurrentFiber));
            break;

        case EFiberState::Running:
            // We cannot reach here.
            YUNREACHABLE();
            break;

        case EFiberState::Terminated:
        case EFiberState::Canceled:
            // Advance epoch as this (idle) fiber just died.
            if (CurrentFiber == IdleFiber) {
                Epoch.fetch_add(0x2, std::memory_order_relaxed);
                IdleFiber.Reset();
            }
            // We do not own this fiber any more, so forget about it.
            CurrentFiber.Reset();
            break;

        default:
            YUNREACHABLE();
    }

    // Finish sync part of the execution.
    EndExecute();

    // Check for a clear scheduling state.
    YASSERT(!CurrentFiber);
    YASSERT(!WaitForFuture);
    YASSERT(!SwitchToInvoker);
}

void TSchedulerThread::FiberMain(unsigned int spawnedEpoch)
{
    ++FibersCreated;
    Profiler.Enqueue("/fibers_created", FibersCreated);

    ++FibersAlive;
    Profiler.Enqueue("/fibers_alive", FibersAlive);

    LOG_DEBUG_IF(EnableLogging, "Fiber started (Name: %s, Created: %d, Alive: %d)",
        ~ThreadName,
        FibersCreated,
        FibersAlive);

    while (FiberMainStep(spawnedEpoch));

    --FibersAlive;
    Profiler.Enqueue("/fibers_alive", FibersAlive);

    LOG_DEBUG_IF(EnableLogging, "Fiber finished (Name: %s, Created: %d, Alive: %d)",
        ~ThreadName,
        FibersCreated,
        FibersAlive);
}

bool TSchedulerThread::FiberMainStep(unsigned int spawnedEpoch)
{
    auto cookie = EventCount->PrepareWait();

    if (!IsRunning()) {
        return false;
    }

    // CancelWait must be called within BeginExecute, if needed.
    auto result = BeginExecute();

    auto currentEpoch = Epoch.load(std::memory_order_relaxed);

    if (spawnedEpoch == currentEpoch) {
        // Make the matching call to EndExecute unless it is already done in ThreadMain.
        // NB: It is safe to call EndExecute even if no actual action was dequeued and
        // invoked in BeginExecute.
        EndExecute();
    }

    if (result == EBeginExecuteResult::QueueEmpty) {
        EventCount->Wait(cookie);
        return true;
    }

    if (result == EBeginExecuteResult::Terminated) {
        return false;
    }

    if (spawnedEpoch != currentEpoch) {
        // If the current fiber has seen WaitFor/SwitchTo calls then
        // its ownership has been transfered to the callback. In the latter case
        // we must abandon the current fiber immediately since the queue's thread
        // had spawned (or will soon spawn) a brand new fiber to continue
        // serving the queue.
        return false;
    }

    return true;
}

void TSchedulerThread::Reschedule(TFiberPtr fiber, TFuture<void> future, IInvokerPtr invoker)
{
    SetCurrentInvoker(invoker, fiber.Get());

    auto continuation = BIND(
        &GuardedInvoke,
        Passed(std::move(invoker)),
        Passed(BIND(&NDetail::ResumeFiber, fiber)),
        Passed(BIND(&NDetail::UnwindFiber, fiber)));

    if (future) {
        future.Subscribe(std::move(continuation));
    } else {
        continuation.Run();
    }
}

void TSchedulerThread::OnContextSwitch()
{
    ContextSwitchCallbacks.Fire();
    ContextSwitchCallbacks.Clear();
}

TThreadId TSchedulerThread::GetId() const
{
    return TThreadId(Thread.SystemId());
}

bool TSchedulerThread::IsRunning() const
{
    return (Epoch.load(std::memory_order_relaxed) & 0x1) == 0x1;
}

TFiber* TSchedulerThread::GetCurrentFiber()
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    return CurrentFiber.Get();
}

void TSchedulerThread::Return()
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    YASSERT(CurrentFiber);
    YASSERT(CurrentFiber->CanReturn());

    SwitchExecutionContext(
        CurrentFiber->GetContext(),
        &SchedulerContext,
        nullptr);

    YUNREACHABLE();
}

void TSchedulerThread::Yield()
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    YASSERT(CurrentFiber);
    auto fiber = CurrentFiber.Get();

    if (fiber->IsCanceled()) {
        throw TFiberCanceledException();
    }

    fiber->SetState(EFiberState::Suspended);
    SwitchExecutionContext(
        fiber->GetContext(),
        &SchedulerContext,
        nullptr);

    if (fiber->IsCanceled()) {
        throw TFiberCanceledException();
    }
}

void TSchedulerThread::SubscribeContextSwitched(TClosure callback)
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    ContextSwitchCallbacks.Subscribe(std::move(callback));
}

void TSchedulerThread::UnsubscribeContextSwitched(TClosure callback)
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    ContextSwitchCallbacks.Unsubscribe(std::move(callback));
}

void TSchedulerThread::YieldTo(TFiberPtr&& other)
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    YASSERT(CurrentFiber);

    if (CurrentFiber->IsCanceled()) {
        throw TFiberCanceledException();
    }

    // Memoize raw pointers.
    auto caller = CurrentFiber.Get();
    auto target = other.Get();

    RunQueue.emplace_front(std::move(CurrentFiber));
    CurrentFiber = std::move(other);

    caller->SetState(EFiberState::Suspended);
    target->SetState(EFiberState::Running);

    SwitchExecutionContext(
        caller->GetContext(),
        target->GetContext(),
        /* as per FiberTrampoline */ target);

    // Cannot access |this| from this point as the fiber might be resumed
    // in other scheduler.

    if (caller->IsCanceled()) {
        throw TFiberCanceledException();
    }
}

void TSchedulerThread::SwitchTo(IInvokerPtr invoker)
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    YASSERT(CurrentFiber);
    auto fiber = CurrentFiber.Get();

    if (fiber->IsCanceled()) {
        throw TFiberCanceledException();
    }

    // Update scheduling state.
    YASSERT(!SwitchToInvoker);
    SwitchToInvoker = std::move(invoker);

    fiber->SetState(EFiberState::Sleeping);
    SwitchExecutionContext(
        fiber->GetContext(),
        &SchedulerContext,
        nullptr);

    // Cannot access |this| from this point as the fiber might be resumed
    // in other scheduler.

    if (fiber->IsCanceled()) {
        throw TFiberCanceledException();
    }
}

void TSchedulerThread::WaitFor(TFuture<void> future, IInvokerPtr invoker)
{
    VERIFY_THREAD_AFFINITY(HomeThread);

    YASSERT(CurrentFiber);
    auto fiber = CurrentFiber.Get();

    if (fiber->IsCanceled()) {
        throw TFiberCanceledException();
    }

    // Update scheduling state.
    YASSERT(!WaitForFuture);
    WaitForFuture = std::move(future);
    YASSERT(!SwitchToInvoker);
    SwitchToInvoker = std::move(invoker);

    fiber->SetState(EFiberState::Sleeping);
    SwitchExecutionContext(
        fiber->GetContext(),
        &SchedulerContext,
        nullptr);

    // Cannot access |this| from this point as the fiber might be resumed
    // in other scheduler.

    if (fiber->IsCanceled()) {
        throw TFiberCanceledException();
    }
}

void TSchedulerThread::OnStart()
{ }

void TSchedulerThread::OnShutdown()
{ }

void TSchedulerThread::OnThreadStart()
{
#ifdef _unix_
    // Set empty sigmask for all threads.
    sigset_t sigset;
    SigEmptySet(&sigset);
    SigProcMask(SIG_SETMASK, &sigset, nullptr);
#endif
}

void TSchedulerThread::OnThreadShutdown()
{ }

///////////////////////////////////////////////////////////////////////////////

TSingleQueueSchedulerThread::TSingleQueueSchedulerThread(
    TInvokerQueuePtr queue,
    TEventCount* eventCount,
    const Stroka& threadName,
    const NProfiling::TTagIdList& tagIds,
    bool enableLogging,
    bool enableProfiling)
    : TSchedulerThread(
        eventCount,
        threadName,
        tagIds,
        enableLogging,
        enableProfiling)
    , Queue(queue)
{ }

TSingleQueueSchedulerThread::~TSingleQueueSchedulerThread()
{ }

IInvokerPtr TSingleQueueSchedulerThread::GetInvoker()
{
    return Queue;
}

EBeginExecuteResult TSingleQueueSchedulerThread::BeginExecute()
{
    return Queue->BeginExecute(&CurrentAction);
}

void TSingleQueueSchedulerThread::EndExecute()
{
    Queue->EndExecute(&CurrentAction);
}

///////////////////////////////////////////////////////////////////////////////

TEVSchedulerThread::TInvoker::TInvoker(TEVSchedulerThread* owner)
    : Owner(owner)
{ }

void TEVSchedulerThread::TInvoker::Invoke(const TClosure& callback)
{
    if (!Owner->IsRunning())
        return;

    Owner->Queue.Enqueue(callback);
    Owner->CallbackWatcher.send();
}

TThreadId TEVSchedulerThread::TInvoker::GetThreadId() const
{
    return Owner->ThreadId;
}

///////////////////////////////////////////////////////////////////////////////

TEVSchedulerThread::TEVSchedulerThread(
    const Stroka& threadName,
    bool enableLogging)
    : TSchedulerThread(
        &EventCount,
        threadName,
        NProfiling::EmptyTagIds,
        enableLogging,
        false)
    , CallbackWatcher(EventLoop)
    , Invoker(New<TInvoker>(this))
{
    CallbackWatcher.set<TEVSchedulerThread, &TEVSchedulerThread::OnCallback>(this);
    CallbackWatcher.start();
}

IInvokerPtr TEVSchedulerThread::GetInvoker()
{
    return Invoker;
}

void TEVSchedulerThread::OnShutdown()
{
    CallbackWatcher.send();
}

EBeginExecuteResult TEVSchedulerThread::BeginExecute()
{
    {
        auto result = BeginExecuteCallbacks();
        if (result != EBeginExecuteResult::QueueEmpty) {
            return result;
        }
    }

    EventLoop.run(0);

    {
        auto result = BeginExecuteCallbacks();
        if (result != EBeginExecuteResult::QueueEmpty) {
            return result;
        }
    }

    // NB: Never return QueueEmpty to prevent waiting on EventCount.
    return EBeginExecuteResult::Success;
}

EBeginExecuteResult TEVSchedulerThread::BeginExecuteCallbacks()
{
    if (!IsRunning()) {
        return EBeginExecuteResult::Terminated;
    }

    TClosure callback;
    if (!Queue.Dequeue(&callback)) {
        return EBeginExecuteResult::QueueEmpty;
    }

    try {
        TCurrentInvokerGuard guard(Invoker);
        callback.Run();
    } catch (const TFiberCanceledException&) {
        // Still consider this a success.
        // This caller is responsible for terminating the current fiber.
    }

    return EBeginExecuteResult::Success;
}

void TEVSchedulerThread::EndExecute()
{ }

void TEVSchedulerThread::OnCallback(ev::async&, int)
{
    EventLoop.break_loop();
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NConcurrency
} // namespace NYT
