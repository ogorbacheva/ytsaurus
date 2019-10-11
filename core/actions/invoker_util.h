#pragma once

#include "public.h"

#include <yt/core/concurrency/public.h>
#include <yt/core/concurrency/scheduler.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

//! Returns the synchronous invoker, i.e. the invoker whose |Invoke|
//! method invokes the closure immediately.
IInvokerPtr GetSyncInvoker();

//! Returns the null invoker, i.e. the invoker whose |Invoke|
//! method does nothing.
IInvokerPtr GetNullInvoker();

//! Returns a special per-process invoker that handles all asynchronous finalization
//! activities (fiber unwinding, abandoned promise cancelation etc).
/*!
 *  This call may return a null invoker (cf. #GetNullInvoker) if the finalizer thread has been shut down.
 *  This is the caller's responsibility to handle such a case gracefully.
 */
IInvokerPtr GetFinalizerInvoker();

// TODO(babenko): remove this when Shutdown Club is finished
void ShutdownFinalizerThread();

//! Tries to invoke #onSuccess via #invoker.
//! If the invoker discards the callback without executing it then
//! #onCancel is run.
void GuardedInvoke(
    const IInvokerPtr& invoker,
    TClosure onSuccess,
    TClosure onCancel);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
