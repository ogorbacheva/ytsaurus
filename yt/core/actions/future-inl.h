#pragma once
#ifndef FUTURE_INL_H_
#error "Direct inclusion of this file is not allowed, include future.h"
// For the sake of sane code completion.
#include "future.h"
#endif
#undef FUTURE_INL_H_

#include "bind.h"
#include "invoker_util.h"

#include <yt/core/concurrency/delayed_executor.h>
#include <yt/core/concurrency/event_count.h>
#include <yt/core/concurrency/thread_affinity.h>

#include <yt/core/misc/small_vector.h>

#include <atomic>
#include <type_traits>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////
// Forward declarations

namespace NConcurrency {

// scheduler.h
TCallback<void(const TError&)> GetCurrentFiberCanceler();

} // namespace NConcurrency

namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

inline TError MakeAbandonedError()
{
    return TError(NYT::EErrorCode::Canceled, "Promise abandoned");
}

inline TError MakeCanceledError(const TError& error)
{
    return TError(NYT::EErrorCode::Canceled, "Operation canceled")
        << error;
}

////////////////////////////////////////////////////////////////////////////////

class TCancelableStateBase
    : public TRefCountedBase
{
public:
    TCancelableStateBase(bool wellKnown, int cancelableRefCount)
        : WellKnown_(wellKnown)
        , CancelableRefCount_(cancelableRefCount)
    { }

    virtual ~TCancelableStateBase() noexcept
    {
#ifdef YT_ENABLE_REF_COUNTED_TRACKING
        FinalizeTracking();
#endif
    }

    virtual bool Cancel(const TError& error) noexcept = 0;

    void RefCancelable()
    {
        if (WellKnown_) {
            return;
        }
        auto oldCount = CancelableRefCount_++;
        YT_ASSERT(oldCount > 0);
    }

    void UnrefCancelable()
    {
        if (WellKnown_) {
            return;
        }
        auto oldCount = CancelableRefCount_--;
        YT_ASSERT(oldCount > 0);
        if (oldCount == 1) {
            OnLastCancelableRefLost();
        }
    }

protected:
    const bool WellKnown_;

    //! Number of cancelables plus one if FutureRefCount_ > 0.
    std::atomic<int> CancelableRefCount_;

private:
    void OnLastCancelableRefLost()
    {
        delete this;
    }
};

Y_FORCE_INLINE void Ref(TCancelableStateBase* state)
{
    state->RefCancelable();
}

Y_FORCE_INLINE void Unref(TCancelableStateBase* state)
{
    state->UnrefCancelable();
}

////////////////////////////////////////////////////////////////////////////////

class TFutureStateBase
    : public TCancelableStateBase
{
public:
    using TVoidResultHandler = TClosure;
    using TVoidResultHandlers = SmallVector<TVoidResultHandler, 8>;

    using TCancelHandler = TCallback<void(const TError&)>;
    using TCancelHandlers = SmallVector<TCancelHandler, 8>;

    void RefFuture()
    {
        if (WellKnown_) {
            return;
        }
        auto oldCount = FutureRefCount_++;
        YT_ASSERT(oldCount > 0);
    }

    bool TryRefFuture()
    {
        if (WellKnown_) {
            return true;
        }
        auto oldCount = FutureRefCount_.load();
        while (true) {
            if (oldCount == 0) {
                return false;
            }
            auto newCount = oldCount + 1;
            if (FutureRefCount_.compare_exchange_weak(oldCount, newCount)) {
                return true;
            }
        }
    }

    void UnrefFuture()
    {
        if (WellKnown_) {
            return;
        }
        auto oldCount = FutureRefCount_--;
        YT_ASSERT(oldCount > 0);
        if (oldCount == 1) {
            OnLastFutureRefLost();
        }
    }

    void RefPromise()
    {
        YT_ASSERT(!WellKnown_);
        auto oldCount = PromiseRefCount_++;
        YT_ASSERT(oldCount > 0 && FutureRefCount_ > 0);
    }

    void UnrefPromise()
    {
        YT_ASSERT(!WellKnown_);
        auto oldCount = PromiseRefCount_--;
        YT_ASSERT(oldCount > 0);
        if (oldCount == 1) {
            OnLastPromiseRefLost();
        }
    }

    void Subscribe(TVoidResultHandler handler);

    virtual bool Cancel(const TError& error) noexcept override;

    void OnCanceled(TCancelHandler handler);

    bool IsSet() const
    {
        return Set_ || AbandonedUnset_;
    }

    bool IsCanceled() const
    {
        return Canceled_;
    }

    bool TimedWait(TDuration timeout) const;
    bool TimedWait(TInstant deadline) const;

protected:
    //! Number of promises.
    std::atomic<int> PromiseRefCount_;
    //! Number of futures plus one if PromiseRefCount_ > 0.
    std::atomic<int> FutureRefCount_;

    //! Protects the following section of members.
    mutable TSpinLock SpinLock_;
    std::atomic<bool> Canceled_ = {false};
    TError CancelationError_;
    std::atomic<bool> Set_;
    std::atomic<bool> AbandonedUnset_ = {false};

    bool HasHandlers_ = false;
    TVoidResultHandlers VoidResultHandlers_;
    TCancelHandlers CancelHandlers_;

    mutable std::unique_ptr<NConcurrency::TEvent> ReadyEvent_;

    TFutureStateBase(int promiseRefCount, int futureRefCount, int cancelableRefCount)
        : TCancelableStateBase(false, cancelableRefCount)
        , PromiseRefCount_(promiseRefCount)
        , FutureRefCount_(futureRefCount)
        , Set_(false)
    { }

    TFutureStateBase(bool wellKnown, int promiseRefCount, int futureRefCount, int cancelableRefCount)
        : TCancelableStateBase(wellKnown, cancelableRefCount)
        , PromiseRefCount_(promiseRefCount)
        , FutureRefCount_(futureRefCount)
        , Set_(true)
    { }


    template <class F, class... As>
    static auto RunNoExcept(F&& functor, As&&... args) noexcept -> decltype(functor(std::forward<As>(args)...))
    {
        return functor(std::forward<As>(args)...);
    }


    virtual void DoInstallAbandonedError() = 0;
    virtual void DoTrySetAbandonedError() = 0;
    virtual bool DoTrySetCanceledError(const TError& error) = 0;

    void InstallAbandonedError();
    void InstallAbandonedError() const;

    virtual void ResetValue() = 0;

private:
    void OnLastFutureRefLost();
    void OnLastPromiseRefLost();
};

Y_FORCE_INLINE void Ref(TFutureStateBase* state)
{
    state->RefFuture();
}

Y_FORCE_INLINE void Unref(TFutureStateBase* state)
{
    state->UnrefFuture();
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
class TFutureState
    : public TFutureStateBase
{
public:
    using TResultHandler = TCallback<void(const TErrorOr<T>&)>;
    using TResultHandlers = SmallVector<TResultHandler, 8>;

    using TUniqueResultHandler = TCallback<void(TErrorOr<T>&&)>;

private:
    std::optional<TErrorOr<T>> Value_;
#ifndef NDEBUG
    std::atomic_flag ValueMovedOut_ = ATOMIC_FLAG_INIT;
#endif

    TResultHandlers ResultHandlers_;
    TUniqueResultHandler UniqueResultHandler_;


    template <class U, bool MustSet>
    bool DoSet(U&& value) noexcept
    {
        // Calling subscribers may release the last reference to this.
        TIntrusivePtr<TFutureStateBase> this_(this);

        NConcurrency::TEvent* readyEvent = nullptr;
        bool canceled;
        {
            TGuard<TSpinLock> guard(SpinLock_);
            YT_ASSERT(!AbandonedUnset_);
            if (MustSet && !Canceled_) {
                YT_VERIFY(!Set_);
            } else if (Set_) {
                return false;
            }
            // TODO(sandello): What about exceptions here?
            Value_.emplace(std::forward<U>(value));
            Set_ = true;
            canceled = Canceled_;
            readyEvent = ReadyEvent_.get();
        }

        if (readyEvent) {
            readyEvent->NotifyAll();
        }


        for (const auto& handler : VoidResultHandlers_) {
            RunNoExcept(handler);
        }
        VoidResultHandlers_.clear();

        for (const auto& handler : ResultHandlers_) {
            RunNoExcept(handler, *Value_);
        }
        ResultHandlers_.clear();

        if (UniqueResultHandler_) {
            RunNoExcept(UniqueResultHandler_, MoveValueOut());
            UniqueResultHandler_ = {};
        }

        if (!canceled) {
            CancelHandlers_.clear();
        }

        return true;
    }

    TErrorOr<T> MoveValueOut()
    {
#ifndef NDEBUG
        YT_ASSERT(!ValueMovedOut_.test_and_set());
#endif
        auto result = std::move(*Value_);
        Value_.reset();
        return result;
    }

    virtual void DoInstallAbandonedError() override
    {
        Value_ = MakeAbandonedError();
        Set_ = true;
    }

    virtual void DoTrySetAbandonedError() override
    {
        TrySet(MakeAbandonedError());
    }

    virtual bool DoTrySetCanceledError(const TError& error) override
    {
        return TrySet(MakeCanceledError(error));
    }

    virtual void ResetValue() override
    {
        Value_.reset();
    }

protected:
    TFutureState(int promiseRefCount, int futureRefCount, int cancelableRefCount)
        : TFutureStateBase(promiseRefCount, futureRefCount, cancelableRefCount)
    { }

    template <class U>
    TFutureState(bool wellKnown, int promiseRefCount, int futureRefCount, int cancelableRefCount, U&& value)
        : TFutureStateBase(wellKnown, promiseRefCount, futureRefCount, cancelableRefCount)
        , Value_(std::forward<U>(value))
    { }

public:
    const TErrorOr<T>& Get() const
    {
        // Fast path.
        if (Set_) {
            return *Value_;
        }

        // Slow path.
        {
            auto guard = Guard(SpinLock_);
            InstallAbandonedError();
            if (Set_) {
                return *Value_;
            }
            if (!ReadyEvent_) {
                ReadyEvent_.reset(new NConcurrency::TEvent());
            }
        }

        ReadyEvent_->Wait();

        return *Value_;
    }

    TErrorOr<T> GetUnique()
    {
        // Fast path.
        if (Set_) {
            return MoveValueOut();
        }

        // Slow path.
        {
            auto guard = Guard(SpinLock_);
            InstallAbandonedError();
            if (Set_) {
                return MoveValueOut();
            }
            if (!ReadyEvent_) {
                ReadyEvent_.reset(new NConcurrency::TEvent());
            }
        }

        ReadyEvent_->Wait();

        return MoveValueOut();
    }

    std::optional<TErrorOr<T>> TryGet() const
    {
        // Fast path.
        if (Set_) {
            return Value_;
        } else if (!AbandonedUnset_) {
            return std::nullopt;
        }

        // Slow path.
        {
            auto guard = Guard(SpinLock_);
            InstallAbandonedError();
            if (!Set_) {
                return std::nullopt;
            }
            return Value_;
        }
    }

    std::optional<TErrorOr<T>> TryGetUnique()
    {
        // Fast path.
        if (Set_) {
            return MoveValueOut();
        } else if (!AbandonedUnset_) {
            return std::nullopt;
        }

        // Slow path.
        {
            auto guard = Guard(SpinLock_);
            InstallAbandonedError();
            if (!Set_) {
                return std::nullopt;
            }
            return MoveValueOut();
        }
    }

    template <class U>
    void Set(U&& value)
    {
        DoSet<U, true>(std::forward<U>(value));
    }

    template <class U>
    bool TrySet(U&& value)
    {
        // Fast path.
        if (Set_) {
            return false;
        }

        // Slow path.
        return DoSet<U, false>(std::forward<U>(value));
    }

    void Subscribe(TResultHandler handler)
    {
        // Fast path.
        if (Set_) {
            RunNoExcept(handler, *Value_);
            return;
        }

        // Slow path.
        {
            auto guard = Guard(SpinLock_);
            InstallAbandonedError();
            if (Set_) {
                guard.Release();
                RunNoExcept(handler, *Value_);
            } else {
                ResultHandlers_.push_back(std::move(handler));
                HasHandlers_ = true;
            }
        }
    }

    void SubscribeUnique(TUniqueResultHandler handler)
    {
        // Fast path.
        if (Set_) {
            RunNoExcept(handler, MoveValueOut());
            return;
        }

        // Slow path.
        {
            auto guard = Guard(SpinLock_);
            InstallAbandonedError();
            if (Set_) {
                guard.Release();
                RunNoExcept(handler, MoveValueOut());
            } else {
                YT_ASSERT(!UniqueResultHandler_);
                YT_ASSERT(ResultHandlers_.empty());
                UniqueResultHandler_ = std::move(handler);
                HasHandlers_ = true;
            }
        }
    }
};

template <class T>
Y_FORCE_INLINE void Ref(TFutureState<T>* state)
{
    state->RefFuture();
}

template <class T>
Y_FORCE_INLINE void Unref(TFutureState<T>* state)
{
    state->UnrefFuture();
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
class TPromiseState
    : public TFutureState<T>
{
public:
    TPromiseState(int promiseRefCount, int futureRefCount, int cancelableRefCount)
        : TFutureState<T>(promiseRefCount, futureRefCount, cancelableRefCount)
    { }

    template <class U>
    TPromiseState(bool wellKnown, int promiseRefCount, int futureRefCount, int cancelableRefCount, U&& value)
        : TFutureState<T>(wellKnown, promiseRefCount, futureRefCount, cancelableRefCount, std::forward<U>(value))
    { }
};

template <class T>
Y_FORCE_INLINE void Ref(TPromiseState<T>* state)
{
    state->RefPromise();
}

template <class T>
Y_FORCE_INLINE void Unref(TPromiseState<T>* state)
{
    state->UnrefPromise();
}

////////////////////////////////////////////////////////////////////////////////

template <class T, class S>
struct TPromiseSetter;

template <class T, class F>
void InterceptExceptions(const TPromise<T>& promise, const F& func)
{
    try {
        func();
    } catch (const TErrorException& ex) {
        promise.Set(ex.Error());
    } catch (const std::exception& ex) {
        promise.Set(TError(ex));
    }
}

template <class R, class T, class... TArgs>
struct TPromiseSetter<T, R(TArgs...)>
{
    template <class... TCallArgs>
    static void Do(const TPromise<T>& promise, const TCallback<T(TArgs...)>& callback, TCallArgs&&... args)
    {
        InterceptExceptions(
            promise,
            [&] {
                promise.Set(callback.Run(std::forward<TCallArgs>(args)...));
            });
    }
};

template <class R, class T, class... TArgs>
struct TPromiseSetter<T, TErrorOr<R>(TArgs...)>
{
    template <class... TCallArgs>
    static void Do(const TPromise<T>& promise, const TCallback<TErrorOr<T>(TArgs...)>& callback, TCallArgs&&... args)
    {
        InterceptExceptions(
            promise,
            [&] {
                promise.Set(callback.Run(std::forward<TCallArgs>(args)...));
            });
    }
};

template <class... TArgs>
struct TPromiseSetter<void, void(TArgs...)>
{
    template <class... TCallArgs>
    static void Do(const TPromise<void>& promise, const TCallback<void(TArgs...)>& callback, TCallArgs&&... args)
    {
        InterceptExceptions(
            promise,
            [&] {
                callback.Run(std::forward<TCallArgs>(args)...);
                promise.Set();
            });
    }
};

template <class T, class... TArgs>
struct TPromiseSetter<T, TFuture<T>(TArgs...)>
{
    template <class... TCallArgs>
    static void Do(const TPromise<T>& promise, const TCallback<TFuture<T>(TArgs...)>& callback, TCallArgs&&... args)
    {
        InterceptExceptions(
            promise,
            [&] {
                promise.SetFrom(callback.Run(std::forward<TCallArgs>(args)...));
            });
    }
};

template <class T, class... TArgs>
struct TPromiseSetter<T, TErrorOr<TFuture<T>>(TArgs...)>
{
    template <class... TCallArgs>
    static void Do(const TPromise<T>& promise, const TCallback<TFuture<T>(TArgs...)>& callback, TCallArgs&&... args)
    {
        InterceptExceptions(
            promise,
            [&] {
                auto result = callback.Run(std::forward<TCallArgs>(args)...);
                if (result.IsOK()) {
                    promise.SetFrom(std::move(result));
                } else {
                    promise.Set(TError(result));
                }
            });
    }
};

template <class R, class T>
void ApplyHelperHandler(const TPromise<T>& promise, const TCallback<R()>& callback, const TError& value)
{
    if (value.IsOK()) {
        TPromiseSetter<T, R()>::Do(promise, callback);
    } else {
        promise.Set(TError(value));
    }
}

template <class R, class T, class U>
void ApplyHelperHandler(const TPromise<T>& promise, const TCallback<R(const U&)>& callback, const TErrorOr<U>& value)
{
    if (value.IsOK()) {
        TPromiseSetter<T, R(const U&)>::Do(promise, callback, value.Value());
    } else {
        promise.Set(TError(value));
    }
}

template <class R, class T, class U>
void ApplyHelperHandler(const TPromise<T>& promise, const TCallback<R(const TErrorOr<U>&)>& callback, const TErrorOr<U>& value)
{
    TPromiseSetter<T, R(const TErrorOr<U>&)>::Do(promise, callback, value);
}

template <class R, class T, class S>
TFuture<R> ApplyHelper(TFutureBase<T> this_, TCallback<S> callback)
{
    YT_ASSERT(this_);

    auto promise = NewPromise<R>();

    this_.Subscribe(BIND([=, callback = std::move(callback)] (const TErrorOr<T>& value) {
        ApplyHelperHandler(promise, callback, value);
    }));

    promise.OnCanceled(BIND([cancelable = this_.AsCancelable()] (const TError& error) {
        cancelable.Cancel(error);
    }));

    return promise;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

template <class T>
TPromise<T> NewPromise()
{
    return TPromise<T>(New<NYT::NDetail::TPromiseState<T>>(1, 1, 1));
}

template <class T>
TPromise<T> MakePromise(TErrorOr<T> value)
{
    return TPromise<T>(New<NYT::NDetail::TPromiseState<T>>(false, 1, 1, 1, std::move(value)));
}

template <class T>
TPromise<T> MakePromise(T value)
{
    return TPromise<T>(New<NYT::NDetail::TPromiseState<T>>(false, 1, 1, 1, std::move(value)));
}

template <class T>
TFuture<T> MakeFuture(TErrorOr<T> value)
{
    return TFuture<T>(New<NYT::NDetail::TPromiseState<T>>(false, 0, 1, 1, std::move(value)));
}

template <class T>
TFuture<T> MakeFuture(T value)
{
    return TFuture<T>(New<NYT::NDetail::TPromiseState<T>>(false, 0, 1, 1, std::move(value)));
}

template <class T>
TFuture<T> MakeWellKnownFuture(TErrorOr<T> value)
{
    return TFuture<T>(New<NYT::NDetail::TPromiseState<T>>(true, -1, -1, -1, std::move(value)));
}

////////////////////////////////////////////////////////////////////////////////

inline bool operator==(const TCancelable& lhs, const TCancelable& rhs)
{
    return lhs.Impl_ == rhs.Impl_;
}

inline bool operator!=(const TCancelable& lhs, const TCancelable& rhs)
{
    return !(lhs == rhs);
}

inline void swap(TCancelable& lhs, TCancelable& rhs)
{
    using std::swap;
    swap(lhs.Impl_, rhs.Impl_);
}

inline bool operator==(const TAwaitable& lhs, const TAwaitable& rhs)
{
    return lhs.Impl_ == rhs.Impl_;
}

inline bool operator!=(const TAwaitable& lhs, const TAwaitable& rhs)
{
    return !(lhs == rhs);
}

inline void swap(TAwaitable& lhs, TAwaitable& rhs)
{
    using std::swap;
    swap(lhs.Impl_, rhs.Impl_);
}

template <class T>
bool operator==(const TFuture<T>& lhs, const TFuture<T>& rhs)
{
    return lhs.Impl_ == rhs.Impl_;
}

template <class T>
bool operator!=(const TFuture<T>& lhs, const TFuture<T>& rhs)
{
    return !(lhs == rhs);
}

template <class T>
void swap(TFuture<T>& lhs, TFuture<T>& rhs)
{
    using std::swap;
    swap(lhs.Impl_, rhs.Impl_);
}

template <class T>
bool operator==(const TPromise<T>& lhs, const TPromise<T>& rhs)
{
    return lhs.Impl_ == rhs.Impl_;
}

template <class T>
bool operator!=(const TPromise<T>& lhs, const TPromise<T>& rhs)
{
    return *(lhs == rhs);
}

template <class T>
void swap(TPromise<T>& lhs, TPromise<T>& rhs)
{
    using std::swap;
    swap(lhs.Impl_, rhs.Impl_);
}

////////////////////////////////////////////////////////////////////////////////

inline TCancelable::operator bool() const
{
    return Impl_.operator bool();
}

inline void TCancelable::Reset()
{
    Impl_.Reset();
}

inline bool TCancelable::Cancel(const TError& error) const
{
    YT_ASSERT(Impl_);
    return Impl_->Cancel(error);
}

inline TCancelable::TCancelable(TIntrusivePtr<NYT::NDetail::TCancelableStateBase> impl)
    : Impl_(std::move(impl))
{ }

////////////////////////////////////////////////////////////////////////////////

inline TAwaitable::operator bool() const
{
    return Impl_.operator bool();
}

inline void TAwaitable::Reset()
{
    Impl_.Reset();
}

inline void TAwaitable::Subscribe(TClosure handler) const
{
    YT_ASSERT(Impl_);
    return Impl_->Subscribe(std::move(handler));
}

inline bool TAwaitable::Cancel(const TError& error) const
{
    YT_ASSERT(Impl_);
    return Impl_->Cancel(error);
}

inline TAwaitable::TAwaitable(TIntrusivePtr<NYT::NDetail::TFutureStateBase> impl)
    : Impl_(std::move(impl))
{ }

////////////////////////////////////////////////////////////////////////////////

template <class T>
TFutureBase<T>::operator bool() const
{
    return Impl_.operator bool();
}

template <class T>
void TFutureBase<T>::Reset()
{
    Impl_.Reset();
}

template <class T>
bool TFutureBase<T>::IsSet() const
{
    YT_ASSERT(Impl_);
    return Impl_->IsSet();
}

template <class T>
const TErrorOr<T>& TFutureBase<T>::Get() const
{
    YT_ASSERT(Impl_);
    return Impl_->Get();
}

template <class T>
TErrorOr<T> TFutureBase<T>::GetUnique() const
{
    YT_ASSERT(Impl_);
    return Impl_->GetUnique();
}

template <class T>
bool TFutureBase<T>::TimedWait(TDuration timeout) const
{
    YT_ASSERT(Impl_);
    return Impl_->TimedWait(timeout);
}

template <class T>
bool TFutureBase<T>::TimedWait(TInstant deadline) const
{
    YT_ASSERT(Impl_);
    return Impl_->TimedWait(deadline);
}

template <class T>
std::optional<TErrorOr<T>> TFutureBase<T>::TryGet() const
{
    YT_ASSERT(Impl_);
    return Impl_->TryGet();
}

template <class T>
std::optional<TErrorOr<T>> TFutureBase<T>::TryGetUnique() const
{
    YT_ASSERT(Impl_);
    return Impl_->TryGetUnique();
}

template <class T>
void TFutureBase<T>::Subscribe(TCallback<void(const TErrorOr<T>&)> handler) const
{
    YT_ASSERT(Impl_);
    return Impl_->Subscribe(std::move(handler));
}

template <class T>
void TFutureBase<T>::SubscribeUnique(TCallback<void(TErrorOr<T>&&)> handler) const
{
    YT_ASSERT(Impl_);
    return Impl_->SubscribeUnique(std::move(handler));
}

template <class T>
bool TFutureBase<T>::Cancel(const TError& error) const
{
    YT_ASSERT(Impl_);
    return Impl_->Cancel(error);
}

template <class T>
TFuture<T> TFutureBase<T>::ToUncancelable() const
{
    if (!Impl_) {
        return TFuture<T>();
    }

    auto promise = NewPromise<T>();

    this->Subscribe(BIND([=] (const TErrorOr<T>& value) {
        promise.Set(value);
    }));

    return promise;
}

template <class T>
TFuture<T> TFutureBase<T>::ToImmediatelyCancelable() const
{
    if (!Impl_) {
        return TFuture<T>();
    }

    auto promise = NewPromise<T>();

    this->Subscribe(BIND([=] (const TErrorOr<T>& value) {
        promise.TrySet(value);
    }));

    promise.OnCanceled(BIND([=, cancelable = AsCancelable()] (const TError& error) {
        cancelable.Cancel(error);
        promise.TrySet(NDetail::MakeCanceledError(error));
    }));

    return promise;
}

template <class T>
TFuture<T> TFutureBase<T>::WithTimeout(TDuration timeout) const
{
    YT_ASSERT(Impl_);

    if (IsSet()) {
        return TFuture<T>(Impl_);
    }

    auto promise = NewPromise<T>();

    auto cookie = NConcurrency::TDelayedExecutor::Submit(
        BIND([=, cancelable = AsCancelable()] (bool aborted) {
            TError error;
            if (aborted) {
                error = TError(NYT::EErrorCode::Canceled, "Operation aborted");
            } else {
                error = TError(NYT::EErrorCode::Timeout, "Operation timed out")
                    << TErrorAttribute("timeout", timeout);
            }
            promise.TrySet(error);
            cancelable.Cancel(error);
        }),
        timeout);

    Subscribe(BIND([=] (const TErrorOr<T>& value) mutable {
        NConcurrency::TDelayedExecutor::CancelAndClear(cookie);
        promise.TrySet(value);
    }));

    promise.OnCanceled(BIND([=, cancelable = AsCancelable()] (const TError& error) mutable {
        NConcurrency::TDelayedExecutor::CancelAndClear(cookie);
        cancelable.Cancel(error);
    }));

    return promise;
}

template <class T>
TFuture<T> TFutureBase<T>::WithTimeout(std::optional<TDuration> timeout) const
{
    return timeout ? WithTimeout(*timeout) : TFuture<T>(Impl_);
}

template <class T>
template <class R>
TFuture<R> TFutureBase<T>::Apply(TCallback<R(const TErrorOr<T>&)> callback) const
{
    return NYT::NDetail::ApplyHelper<R>(*this, std::move(callback));
}

template <class T>
template <class R>
TFuture<R> TFutureBase<T>::Apply(TCallback<TErrorOr<R>(const TErrorOr<T>&)> callback) const
{
    return NYT::NDetail::ApplyHelper<R>(*this, std::move(callback));
}

template <class T>
template <class R>
TFuture<R> TFutureBase<T>::Apply(TCallback<TFuture<R>(const TErrorOr<T>&)> callback) const
{
    return NYT::NDetail::ApplyHelper<R>(*this, std::move(callback));
}

template <class T>
template <class R>
TFuture<R> TFutureBase<T>::Apply(TCallback<TErrorOr<TFuture<R>>(const TErrorOr<T>&)> callback) const
{
    return NYT::NDetail::ApplyHelper<R>(*this, std::move(callback));
}

template <class T>
template <class U>
TFuture<U> TFutureBase<T>::As() const
{
    if (!Impl_) {
        return TFuture<U>();
    }

    auto promise = NewPromise<U>();

    Subscribe(BIND([=] (const TErrorOr<T>& value) {
        promise.Set(TErrorOr<U>(value));
    }));

    promise.OnCanceled(BIND([cancelable = AsCancelable()] (const TError& error) {
        cancelable.Cancel(error);
    }));

    return promise;
}

template <class T>
TCancelable TFutureBase<T>::AsCancelable() const
{
    return TCancelable(Impl_);
}

template <class T>
TAwaitable TFutureBase<T>::AsAwaitable() const
{
    return TAwaitable(Impl_);
}

template <class T>
TFutureBase<T>::TFutureBase(TIntrusivePtr<NYT::NDetail::TFutureState<T>> impl)
    : Impl_(std::move(impl))
{ }

////////////////////////////////////////////////////////////////////////////////

template <class T>
TFuture<T>::TFuture(std::nullopt_t)
{ }

template <class T>
template <class R>
TFuture<R> TFuture<T>::Apply(TCallback<R(const T&)> callback) const
{
    return NYT::NDetail::ApplyHelper<R>(*this, callback);
}

template <class T>
template <class R>
TFuture<R> TFuture<T>::Apply(TCallback<R(T)> callback) const
{
    return this->Apply(TCallback<R(const T&)>(callback));
}

template <class T>
template <class R>
TFuture<R> TFuture<T>::Apply(TCallback<TFuture<R>(const T&)> callback) const
{
    return NYT::NDetail::ApplyHelper<R>(*this, callback);
}

template <class T>
template <class R>
TFuture<R> TFuture<T>::Apply(TCallback<TFuture<R>(T)> callback) const
{
    return this->Apply(TCallback<TFuture<R>(const T&)>(callback));
}

template <class T>
TFuture<T>::TFuture(TIntrusivePtr<NYT::NDetail::TFutureState<T>> impl)
    : TFutureBase<T>(std::move(impl))
{ }

////////////////////////////////////////////////////////////////////////////////

inline TFuture<void>::TFuture(std::nullopt_t)
{ }

template <class R>
TFuture<R> TFuture<void>::Apply(TCallback<R()> callback) const
{
    return NYT::NDetail::ApplyHelper<R>(*this, callback);
}

template <class R>
TFuture<R> TFuture<void>::Apply(TCallback<TFuture<R>()> callback) const
{
    return NYT::NDetail::ApplyHelper<R>(*this, callback);
}

inline TFuture<void>::TFuture(TIntrusivePtr<NYT::NDetail::TFutureState<void>> impl)
    : TFutureBase<void>(std::move(impl))
{ }

////////////////////////////////////////////////////////////////////////////////

template <class T>
TPromiseBase<T>::operator bool() const
{
    return Impl_.operator bool();
}

template <class T>
void TPromiseBase<T>::Reset()
{
    Impl_.Reset();
}

template <class T>
bool TPromiseBase<T>::IsSet() const
{
    YT_ASSERT(Impl_);
    return Impl_->IsSet();
}

template <class T>
void TPromiseBase<T>::Set(const TErrorOr<T>& value) const
{
    YT_ASSERT(Impl_);
    Impl_->Set(value);
}

template <class T>
void TPromiseBase<T>::Set(TErrorOr<T>&& value) const
{
    YT_ASSERT(Impl_);
    Impl_->Set(std::move(value));
}

template <class T>
template <class U>
void TPromiseBase<T>::SetFrom(const TFuture<U>& another) const
{
    YT_ASSERT(Impl_);

    auto this_ = *this;

    another.Subscribe(BIND([this_] (const TErrorOr<U>& value)   {
        this_.Set(value);
    }));

    OnCanceled(BIND([anotherCancelable = another.AsCancelable()] (const TError& error) {
        anotherCancelable.Cancel(error);
    }));
}

template <class T>
bool TPromiseBase<T>::TrySet(const TErrorOr<T>& value) const
{
    YT_ASSERT(Impl_);
    return Impl_->TrySet(value);
}

template <class T>
bool TPromiseBase<T>::TrySet(TErrorOr<T>&& value) const
{
    YT_ASSERT(Impl_);
    return Impl_->TrySet(std::move(value));
}

template <class T>
template <class U>
inline void TPromiseBase<T>::TrySetFrom(TFuture<U> another) const
{
    YT_ASSERT(Impl_);

    auto this_ = *this;

    another.Subscribe(BIND([this_] (const TErrorOr<U>& value) {
        this_.TrySet(value);
    }));

    OnCanceled(BIND([anotherCancelable = another.AsCancelable()] (const TError& error) {
        anotherCancelable.Cancel(error);
    }));
}

template <class T>
const TErrorOr<T>& TPromiseBase<T>::Get() const
{
    YT_ASSERT(Impl_);
    return Impl_->Get();
}

template <class T>
std::optional<TErrorOr<T>> TPromiseBase<T>::TryGet() const
{
    YT_ASSERT(Impl_);
    return Impl_->TryGet();
}

template <class T>
bool TPromiseBase<T>::IsCanceled() const
{
    return Impl_->IsCanceled();
}

template <class T>
void TPromiseBase<T>::OnCanceled(TCallback<void(const TError&)> handler) const
{
    YT_ASSERT(Impl_);
    Impl_->OnCanceled(std::move(handler));
}

template <class T>
TFuture<T> TPromiseBase<T>::ToFuture() const
{
    return TFuture<T>(Impl_);
}

template <class T>
TPromiseBase<T>::operator TFuture<T>() const
{
    return TFuture<T>(Impl_);
}

template <class T>
TPromiseBase<T>::TPromiseBase(TIntrusivePtr<NYT::NDetail::TPromiseState<T>> impl)
    : Impl_(std::move(impl))
{ }

////////////////////////////////////////////////////////////////////////////////

template <class T>
TPromise<T>::TPromise(std::nullopt_t)
{ }

template <class T>
void TPromise<T>::Set(const T& value) const
{
    YT_ASSERT(this->Impl_);
    this->Impl_->Set(value);
}

template <class T>
void TPromise<T>::Set(T&& value) const
{
    YT_ASSERT(this->Impl_);
    this->Impl_->Set(std::move(value));
}

template <class T>
void TPromise<T>::Set(const TError& error) const
{
    Set(TErrorOr<T>(error));
}

template <class T>
void TPromise<T>::Set(TError&& error) const
{
    Set(TErrorOr<T>(std::move(error)));
}

template <class T>
bool TPromise<T>::TrySet(const T& value) const
{
    YT_ASSERT(this->Impl_);
    return this->Impl_->TrySet(value);
}

template <class T>
bool TPromise<T>::TrySet(T&& value) const
{
    YT_ASSERT(this->Impl_);
    return this->Impl_->TrySet(std::move(value));
}

template <class T>
bool TPromise<T>::TrySet(const TError& error) const
{
    return TrySet(TErrorOr<T>(error));
}

template <class T>
bool TPromise<T>::TrySet(TError&& error) const
{
    return TrySet(TErrorOr<T>(std::move(error)));
}

template <class T>
TPromise<T>::TPromise(TIntrusivePtr<NYT::NDetail::TPromiseState<T>> impl)
    : TPromiseBase<T>(std::move(impl))
{ }

////////////////////////////////////////////////////////////////////////////////

inline TPromise<void>::TPromise(std::nullopt_t)
{ }

inline void TPromise<void>::Set() const
{
    YT_ASSERT(this->Impl_);
    this->Impl_->Set(TError());
}

inline bool TPromise<void>::TrySet() const
{
    YT_ASSERT(this->Impl_);
    return this->Impl_->TrySet(TError());
}

inline TPromise<void>::TPromise(TIntrusivePtr<NYT::NDetail::TPromiseState<void>> impl)
    : TPromiseBase<void>(std::move(impl))
{ }

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

template <class TSignature>
struct TAsyncViaHelper;

template <class R, class... TArgs>
struct TAsyncViaHelper<R(TArgs...)>
{
    using TUnderlying = typename TFutureTraits<R>::TUnderlying;
    using TSourceCallback = TCallback<R(TArgs...)>;
    using TTargetCallback = TCallback<TFuture<TUnderlying>(TArgs...)>;

    static void Inner(
        const TSourceCallback& this_,
        const TPromise<TUnderlying>& promise,
        TArgs... args)
    {
        if (promise.IsCanceled()) {
            promise.Set(TError(
                NYT::EErrorCode::Canceled,
                "Computation was canceled before it was started"));
            return;
        }

        auto canceler = NConcurrency::GetCurrentFiberCanceler();
        if (canceler) {
            promise.OnCanceled(std::move(canceler));
        }

        NYT::NDetail::TPromiseSetter<TUnderlying, R(TArgs...)>::Do(promise, this_, args...);
    }

    static TFuture<TUnderlying> Outer(
        const TSourceCallback& this_,
        const IInvokerPtr& invoker,
        TArgs... args)
    {
        auto promise = NewPromise<TUnderlying>();
        invoker->Invoke(BIND(&Inner, this_, promise, args...));
        return promise;
    }

    static TFuture<TUnderlying> OuterGuarded(
        const TSourceCallback& this_,
        const IInvokerPtr& invoker,
        TError cancellationError,
        TArgs... args)
    {
        auto promise = NewPromise<TUnderlying>();
        GuardedInvoke(
            invoker,
            BIND(&Inner, this_, promise, args...),
            BIND([promise, cancellationError = std::move(cancellationError)] {
                promise.Set(std::move(cancellationError));
            }));
        return promise;
    }

    static TTargetCallback Do(
        TSourceCallback this_,
        IInvokerPtr invoker)
    {
        return BIND(&Outer, std::move(this_), std::move(invoker));
    }

    static TTargetCallback DoGuarded(
        TSourceCallback this_,
        IInvokerPtr invoker,
        TError cancellationError)
    {
        return BIND(&OuterGuarded, std::move(this_), std::move(invoker), std::move(cancellationError));
    }
};

} // namespace NDetail

template <class R, class... TArgs>
TCallback<typename TFutureTraits<R>::TWrapped(TArgs...)>
TCallback<R(TArgs...)>::AsyncVia(IInvokerPtr invoker) const
{
    return NYT::NDetail::TAsyncViaHelper<R(TArgs...)>::Do(*this, std::move(invoker));
}

template <class R, class... TArgs>
TCallback<typename TFutureTraits<R>::TWrapped(TArgs...)>
TCallback<R(TArgs...)>::AsyncViaGuarded(IInvokerPtr invoker, TError cancellationError) const
{
    return NYT::NDetail::TAsyncViaHelper<R(TArgs...)>::DoGuarded(*this, std::move(invoker), std::move(cancellationError));
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
TFutureHolder<T>::TFutureHolder(std::nullopt_t)
{ }

template <class T>
TFutureHolder<T>::TFutureHolder(TFuture<T> future)
    : Future_(std::move(future))
{ }

template <class T>
TFutureHolder<T>::~TFutureHolder()
{
    if (Future_) {
        Future_.Cancel(TError("Future holder destroyed"));
    }
}

template <class T>
TFutureHolder<T>::operator bool() const
{
    return static_cast<bool>(Future_);
}

template <class T>
TFuture<T>& TFutureHolder<T>::Get()
{
    return Future_;
}

template <class T>
const TFuture<T>& TFutureHolder<T>::Get() const
{
    return Future_;
}

template <class T>
const TFuture<T>& TFutureHolder<T>::operator*() const // noexcept
{
    return Future_;
}

template <class T>
TFuture<T>& TFutureHolder<T>::operator*() // noexcept
{
    return Future_;
}

template <class T>
const TFuture<T>* TFutureHolder<T>::operator->() const // noexcept
{
    return &Future_;
}

template <class T>
TFuture<T>* TFutureHolder<T>::operator->() // noexcept
{
    return &Future_;
}

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

template <class T>
class TFutureCombinerResultHolder
{
public:
    using TResult = std::vector<T>;

    explicit TFutureCombinerResultHolder(int size)
        : Result_(size)
    { }

    bool SetResult(int index, const TErrorOr<T>& errorOrValue)
    {
        if (errorOrValue.IsOK()) {
            Result_[index] = errorOrValue.Value();
            return true;
        } else {
            return false;
        }
    }

    void SetPromise(const TPromise<TResult>& promise)
    {
        promise.TrySet(std::move(Result_));
    }

private:
    TResult Result_;
};

template <class T>
class TFutureCombinerResultHolder<TErrorOr<T>>
{
public:
    using TResult = std::vector<TErrorOr<T>>;

    explicit TFutureCombinerResultHolder(int size)
        : Result_(size)
    { }

    bool SetResult(int index, const TErrorOr<T>& errorOrValue)
    {
        Result_[index] = errorOrValue;
        return true;
    }

    void SetPromise(const TPromise<TResult>& promise)
    {
        promise.TrySet(std::move(Result_));
    }

private:
    TResult Result_;
};

template <>
class TFutureCombinerResultHolder<void>
{
public:
    using TResult = void;

    explicit TFutureCombinerResultHolder(int /*size*/)
    { }

    bool SetResult(int /*index*/, const TError& error)
    {
        return error.IsOK();
    }

    void SetPromise(const TPromise<TResult>& promise)
    {
        promise.TrySet();
    }
};

template <class T, class R>
class TAnyOfFutureCombiner
    : public TRefCounted
{
public:
    explicit TAnyOfFutureCombiner(
        std::vector<TFuture<T>> futures,
        bool skipErrors,
        TFutureCombinerOptions options)
        : Futures_(std::move(futures))
        , SkipErrors_(skipErrors)
        , Options_(options)
    { }

    TFuture<R> Run()
    {
        if (this->Futures_.empty()) {
            return MakeFuture<T>(TError(
                NYT::EErrorCode::FutureCombinerFailure,
                "Any-of combiner failure: empty input"));
        }

        for (const auto& future : Futures_) {
            future.Subscribe(BIND(&TAnyOfFutureCombiner::OnFutureSet, MakeStrong(this)));
        }

        if (Options_.PropagateCancelationToInput) {
            Promise_.OnCanceled(BIND(&TAnyOfFutureCombiner::OnCanceled, MakeWeak(this)));
        }

        return Promise_;
    }

private:
    const std::vector<TFuture<T>> Futures_;
    const bool SkipErrors_;
    const TFutureCombinerOptions Options_;
    const TPromise<R> Promise_ = NewPromise<T>();

    std::atomic_flag FuturesCanceled_ = ATOMIC_FLAG_INIT;

    TSpinLock ErrorsLock_;
    std::vector<TError> Errors_;

    void CancelFutures(const TError& error)
    {
        for (const auto& future : Futures_) {
            future.Cancel(error);
        }
    }

    void OnFutureSet(const TErrorOr<T>& result)
    {
        if (SkipErrors_ && !result.IsOK()) {
            RegisterError(result);
            return;
        }

        Promise_.TrySet(result);

        if (Options_.CancelInputOnShortcut && Futures_.size() > 1 && !FuturesCanceled_.test_and_set()) {
            CancelFutures(TError(
                NYT::EErrorCode::FutureCombinerShortcut,
                "Any-of combiner shortcut: some response received"));
        }
    }

    void OnCanceled(const TError& error)
    {
        if (!FuturesCanceled_.test_and_set()) {
            CancelFutures(error);
        }
    }

    void RegisterError(const TError& error)
    {
        auto guard = Guard(ErrorsLock_);

        Errors_.push_back(error);

        if (Errors_.size() < Futures_.size()) {
            return;
        }

        auto combinerError = TError(
            NYT::EErrorCode::FutureCombinerFailure,
            "Any-of combiner failure: all responses have failed")
            << Errors_;

        guard.Release();

        Promise_.TrySet(combinerError);
    }
};

template <class T, class TResultHolder>
class TFutureCombinerBase
    : public TRefCounted
{
public:
    TFutureCombinerBase(std::vector<TFuture<T>> futures, TFutureCombinerOptions options)
        : Futures_(std::move(futures))
        , Options_(options)
        , ResultHolder_(Futures_.size())
    { }

    TFutureCombinerBase(std::vector<TFuture<T>> futures, int n, TFutureCombinerOptions options)
        : Futures_(std::move(futures))
        , Options_(options)
        , ResultHolder_(n)
    { }

protected:
    const std::vector<TFuture<T>> Futures_;
    const TFutureCombinerOptions Options_;
    const TPromise<typename TResultHolder::TResult> Promise_ = NewPromise<typename TResultHolder::TResult>();

    std::atomic_flag FuturesCanceled_ = ATOMIC_FLAG_INIT;

    TResultHolder ResultHolder_;

    TFuture<typename TResultHolder::TResult> DoRun()
    {
        for (int index = 0; index < static_cast<int>(Futures_.size()); ++index) {
            Futures_[index].Subscribe(BIND(&TFutureCombinerBase::OnFutureSet, MakeStrong(this), index));
        }

        if (Options_.PropagateCancelationToInput) {
            Promise_.OnCanceled(BIND(&TFutureCombinerBase::OnCanceled, MakeWeak(this)));
        }

        return Promise_;
    }

    void CancelFutures(const TError& error)
    {
        for (const auto& future : Futures_) {
            future.Cancel(error);
        }
    }

    virtual void OnFutureSet(int index, const TErrorOr<T>& result) = 0;

    void OnCanceled(const TError& error)
    {
        if (!FuturesCanceled_.test_and_set()) {
            CancelFutures(error);
        }
    }
};

template <class T, class TResultHolder>
class TAllOfFutureCombiner
    : public TFutureCombinerBase<T, TResultHolder>
{
public:
    TAllOfFutureCombiner(
        std::vector<TFuture<T>> futures,
        TFutureCombinerOptions options)
        : TFutureCombinerBase<T, TResultHolder>(std::move(futures), options)
    { }

    TFuture<typename TResultHolder::TResult> Run()
    {
        if (this->Futures_.empty()) {
            return MakeFuture<typename TResultHolder::TResult>({});
        }

        return this->DoRun();
    }

private:
    std::atomic<int> ResponseCount_ = 0;

    virtual void OnFutureSet(int index, const TErrorOr<T>& result) override
    {
        if (!this->ResultHolder_.SetResult(index, result)) {
            TError error(result);
            this->Promise_.TrySet(error);

            if (this->Options_.CancelInputOnShortcut && this->Futures_.size() > 1 && !this->FuturesCanceled_.test_and_set()) {
                this->CancelFutures(TError(
                    NYT::EErrorCode::FutureCombinerShortcut,
                    "All-of combiner shortcut: some response failed")
                    << error);
            }

            return;
        }

        if (++ResponseCount_ == static_cast<int>(this->Futures_.size())) {
            this->ResultHolder_.SetPromise(this->Promise_);
        }
    }
};

template <class T, class TResultHolder>
class TAnyNOfFutureCombiner
    : public TFutureCombinerBase<T, TResultHolder>
{
public:
    TAnyNOfFutureCombiner(
        std::vector<TFuture<T>> futures,
        int n,
        bool skipErrors,
        TFutureCombinerOptions options)
        : TFutureCombinerBase<T, TResultHolder>(std::move(futures), n, options)
        , N_(n)
        , SkipErrors_(skipErrors)
    {
        YT_VERIFY(N_ >= 0);
    }

    TFuture<typename TResultHolder::TResult> Run()
    {
        if (N_ == 0) {
            if (this->Options_.CancelInputOnShortcut && !this->Futures_.empty()) {
                this->CancelFutures(TError(
                    NYT::EErrorCode::FutureCombinerShortcut,
                    "Any-N-of combiner shortcut: no responses needed"));
            }

            return MakeFuture<typename TResultHolder::TResult>({});
        }

        if (static_cast<int>(this->Futures_.size()) < N_) {
            if (this->Options_.CancelInputOnShortcut) {
                this->CancelFutures(TError(
                    NYT::EErrorCode::FutureCombinerShortcut,
                    "Any-N-of combiner shortcut: too few inputs given"));
            }

            return MakeFuture<typename TResultHolder::TResult>(TError(
                NYT::EErrorCode::FutureCombinerFailure,
                "Any-N-of combiner failure: %v responses needed, %v inputs given",
                N_,
                this->Futures_.size()));
        }

        return this->DoRun();
    }

private:
    const int N_;
    const bool SkipErrors_;

    std::atomic<int> ResponseCount_ = 0;

    TSpinLock ErrorsLock_;
    std::vector<TError> Errors_;

    virtual void OnFutureSet(int /*index*/, const TErrorOr<T>& result) override
    {
        if (SkipErrors_ && !result.IsOK()) {
            RegisterError(result);
            return;
        }

        int responseIndex = ResponseCount_++;
        if (responseIndex >= N_) {
            return;
        }

        if (!this->ResultHolder_.SetResult(responseIndex, result)) {
            TError error(result);
            this->Promise_.TrySet(error);

            if (this->Options_.CancelInputOnShortcut && this->Futures_.size() > 1) {
                this->CancelFutures(TError(
                    NYT::EErrorCode::FutureCombinerShortcut,
                    "Any-N-of combiner shortcut: some input failed"));
            }
            return;
        }

        if (responseIndex == N_ - 1) {
            this->ResultHolder_.SetPromise(this->Promise_);

            if (this->Options_.CancelInputOnShortcut && responseIndex < static_cast<int>(this->Futures_.size()) - 1) {
                this->CancelFutures(TError(
                    NYT::EErrorCode::FutureCombinerShortcut,
                    "Any-N-of combiner shortcut: enough responses received"));
            }
        }
    }

    void RegisterError(const TError& error)
    {
        auto guard = Guard(ErrorsLock_);

        Errors_.push_back(error);

        auto totalCount = static_cast<int>(this->Futures_.size());
        auto failedCount = static_cast<int>(Errors_.size());
        if (totalCount - failedCount >= N_) {
            return;
        }

        auto combinerError = TError(
            NYT::EErrorCode::FutureCombinerFailure,
            "Any-N-of combiner failure: %v responses needed, %v failed, %v inputs given",
            N_,
            failedCount,
            totalCount)
            << Errors_;

        guard.Release();

        this->Promise_.TrySet(combinerError);

        if (this->Options_.CancelInputOnShortcut) {
            this->CancelFutures(TError(
                NYT::EErrorCode::FutureCombinerShortcut,
                "Any-N-of combiner shortcut: one of responses failed")
                << error);
        }
    }
};

} // namespace NDetail

template <class T>
TFuture<T> AnyOf(
    std::vector<TFuture<T>> futures,
    TSkipErrorPolicy /*errorPolicy*/,
    TFutureCombinerOptions options)
{
    if (futures.size() == 1) {
        return std::move(futures[0]);
    }
    return New<NDetail::TAnyOfFutureCombiner<T, T>>(std::move(futures), true, options)
        ->Run();
}

template <class T>
TFuture<TErrorOr<T>> AnyOf(
    std::vector<TFuture<T>> futures,
    TRetainErrorPolicy /*errorPolicy*/,
    TFutureCombinerOptions options)
{
    return New<NDetail::TAnyOfFutureCombiner<T, TErrorOr<T>>>(std::move(futures), false, options)
        ->Run();
}

template <class T>
TFuture<typename TFutureCombinerTraits<T>::TCombinedVector> AllOf(
    std::vector<TFuture<T>> futures,
    TPropagateErrorPolicy /*errorPolicy*/,
    TFutureCombinerOptions options)
{
    auto size = futures.size();
    if constexpr(std::is_same_v<T, void>) {
        if (size == 0) {
            return VoidFuture;
        }
        if (size == 1) {
            return std::move(futures[0]);
        }
    }
    using TResultHolder = NDetail::TFutureCombinerResultHolder<T>;
    return New<NDetail::TAllOfFutureCombiner<T, TResultHolder>>(std::move(futures), options)
        ->Run();
}

template <class T>
TFuture<std::vector<TErrorOr<T>>> AllOf(
    std::vector<TFuture<T>> futures,
    TRetainErrorPolicy /*errorPolicy*/,
    TFutureCombinerOptions options)
{
    using TResultHolder = NDetail::TFutureCombinerResultHolder<TErrorOr<T>>;
    return New<NDetail::TAllOfFutureCombiner<T, TResultHolder>>(std::move(futures), options)
        ->Run();
}

template <class T>
TFuture<typename TFutureCombinerTraits<T>::TCombinedVector> AnyNOf(
    std::vector<TFuture<T>> futures,
    int n,
    TSkipErrorPolicy /*errorPolicy*/,
    TFutureCombinerOptions options)
{
    auto size = futures.size();
    if constexpr(std::is_same_v<T, void>) {
        if (size == 1 && n == 1) {
            return std::move(futures[0]);
        }
    }
    using TResultHolder = NDetail::TFutureCombinerResultHolder<T>;
    return New<NDetail::TAnyNOfFutureCombiner<T, TResultHolder>>(std::move(futures), n, true, options)
        ->Run();
}

template <class T>
TFuture<std::vector<TErrorOr<T>>> AnyNOf(
    std::vector<TFuture<T>> futures,
    int n,
    TRetainErrorPolicy /*errorPolicy*/,
    TFutureCombinerOptions options)
{
    using TResultHolder = NDetail::TFutureCombinerResultHolder<TErrorOr<T>>;
    return New<NDetail::TAnyNOfFutureCombiner<T, TResultHolder>>(std::move(futures), n, false, options)
        ->Run();
}

////////////////////////////////////////////////////////////////////////////////
// COMPAT(babenko)

template <class T>
TFuture<typename TFutureCombinerTraits<T>::TCombinedVector> Combine(std::vector<TFuture<T>> futures)
{
    return AllOf(std::move(futures));
}

template <class T>
TFuture<typename TFutureCombinerTraits<T>::TCombinedVector> CombineQuorum(std::vector<TFuture<T>> futures, int quorum)
{
    return AnyNOf(std::move(futures), quorum);
}

template <class T>
TFuture<std::vector<TErrorOr<T>>> CombineAll(std::vector<TFuture<T>> futures)
{
    return AllOf(std::move(futures), TRetainErrorPolicy{});
}

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

template <class T>
class TBoundedConcurrencyRunner
    : public TIntrinsicRefCounted
{
public:
    TBoundedConcurrencyRunner(
        std::vector<TCallback<TFuture<T>()>> callbacks,
        int concurrencyLimit)
        : Callbacks_(std::move(callbacks))
        , ConcurrencyLimit_(concurrencyLimit)
        , Results_(Callbacks_.size())
    { }

    TFuture<std::vector<TErrorOr<T>>> Run()
    {
        if (Callbacks_.empty()) {
            return MakeFuture(std::vector<TErrorOr<T>>());
        }
        int startImmediatelyCount = std::min(ConcurrencyLimit_, static_cast<int>(Callbacks_.size()));
        CurrentIndex_ = startImmediatelyCount;
        for (int index = 0; index < startImmediatelyCount; ++index) {
            RunCallback(index);
        }
        return Promise_;
    }

private:
    const std::vector<TCallback<TFuture<T>()>> Callbacks_;
    const int ConcurrencyLimit_;
    const TPromise<std::vector<TErrorOr<T>>> Promise_ = NewPromise<std::vector<TErrorOr<T>>>();

    std::vector<TErrorOr<T>> Results_;
    std::atomic<int> CurrentIndex_;
    std::atomic<int> FinishedCount_ = 0;


    void RunCallback(int index)
    {
        Callbacks_[index].Run().Subscribe(
            BIND(&TBoundedConcurrencyRunner::OnResult, MakeStrong(this), index));
    }

    void OnResult(int index, const TErrorOr<T>& result)
    {
        Results_[index] = result;

        int newIndex = CurrentIndex_++;
        if (newIndex < Callbacks_.size()) {
            RunCallback(newIndex);
        }

        if (++FinishedCount_ == Callbacks_.size()) {
            Promise_.Set(Results_);
        }
    }
};

} // namespace NDetail

template <class T>
TFuture<std::vector<TErrorOr<T>>> RunWithBoundedConcurrency(
    std::vector<TCallback<TFuture<T>()>> callbacks,
    int concurrencyLimit)
{
    YT_VERIFY(concurrencyLimit >= 0);
    return New<NDetail::TBoundedConcurrencyRunner<T>>(std::move(callbacks), concurrencyLimit)
        ->Run();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

//! A hasher for TAwaitable.
template <>
struct THash<NYT::TAwaitable>
{
    inline size_t operator () (const NYT::TAwaitable& awaitable) const
    {
        return THash<NYT::TIntrusivePtr<NYT::NDetail::TFutureStateBase>>()(awaitable.Impl_);
    }
};

//! A hasher for TFuture.
template <class T>
struct THash<NYT::TFuture<T>>
{
    size_t operator () (const NYT::TFuture<T>& future) const
    {
        return THash<NYT::TIntrusivePtr<NYT::NDetail::TFutureState<T>>>()(future.Impl_);
    }
};

//! A hasher for TPromise.
template <class T>
struct THash<NYT::TPromise<T>>
{
    size_t operator () (const NYT::TPromise<T>& promise) const
    {
        return THash<NYT::TIntrusivePtr<NYT::NDetail::TPromiseState<T>>>()(promise.Impl_);
    }
};
