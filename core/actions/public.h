#pragma once

#include <yt/core/misc/common.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class TSignature>
class TCallback;

typedef TCallback<void()> TClosure;

template <class TSignature>
class TCallbackList;

template <class T>
class TFuture;

template <>
class TFuture<void>;

template <class T>
class TPromise;

template <>
class TPromise<void>;

template <class T>
class TFutureHolder;

DECLARE_REFCOUNTED_STRUCT(IInvoker)
DECLARE_REFCOUNTED_STRUCT(IPrioritizedInvoker)
DECLARE_REFCOUNTED_STRUCT(ISuspendableInvoker)

DECLARE_REFCOUNTED_CLASS(TCancelableContext)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
