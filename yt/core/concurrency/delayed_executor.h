#pragma once

#include "public.h"

#include <yt/core/actions/callback.h>

namespace NYT {
namespace NConcurrency {

////////////////////////////////////////////////////////////////////////////////

//! Manages delayed callback execution.
class TDelayedExecutor
{
public:
    ~TDelayedExecutor();

    //! Constructs a future that gets set when a given #delay elapses.
    static TFuture<void> MakeDelayed(TDuration delay);

    //! Submits #callback for execution after a given #delay.
    static TDelayedExecutorCookie Submit(TClosure callback, TDuration delay);

    //! Submits #callback for execution at a given #deadline.
    static TDelayedExecutorCookie Submit(TClosure callback, TInstant deadline);

    //! Cancels an earlier scheduled execution and clears the cookie.
    static void CancelAndClear(TDelayedExecutorCookie& cookie);

    //! Terminates the scheduler thread.
    /*!
     *  All subsequent #Submit calls are silently ignored.
     */
    static void StaticShutdown();

private:
    class TImpl;

    TDelayedExecutor();

    static TImpl* const GetImpl();

    Y_DECLARE_SINGLETON_FRIEND();
};

extern const TDelayedExecutorCookie NullDelayedExecutorCookie;

////////////////////////////////////////////////////////////////////////////////

} // namespace NConcurrency
} // namespace NYT
