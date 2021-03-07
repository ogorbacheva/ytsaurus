#pragma once

#include "public.h"

#include <yt/yt/core/actions/callback.h>

#include <yt/yt/core/misc/shutdownable.h>
#include <yt/yt/core/misc/range.h>

namespace NYT::NConcurrency {

////////////////////////////////////////////////////////////////////////////////

struct IFairShareActionQueue
    : public TRefCounted
    , public IShutdownable
{
    virtual const IInvokerPtr& GetInvoker(int index) const = 0;
};

DEFINE_REFCOUNTED_TYPE(IFairShareActionQueue)

////////////////////////////////////////////////////////////////////////////////

IFairShareActionQueuePtr CreateFairShareActionQueue(
    const TString& threadName,
    const std::vector<TString>& queueNames,
    const THashMap<TString, std::vector<TString>>& queueToBucket = {});

////////////////////////////////////////////////////////////////////////////////

template <typename EQueue>
struct IEnumIndexedFairShareActionQueue
    : public TRefCounted
    , public IShutdownable
{
    virtual const IInvokerPtr& GetInvoker(EQueue queue) const = 0;
};

////////////////////////////////////////////////////////////////////////////////

template <typename EQueue, typename EBucket = EQueue>
IEnumIndexedFairShareActionQueuePtr<EQueue> CreateEnumIndexedFairShareActionQueue(
    const TString& threadName,
    const THashMap<EBucket, std::vector<EQueue>>& queueToBucket = {});

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NConcurrency

#define FAIR_SHARE_ACTION_QUEUE_INL_H_
#include "fair_share_action_queue-inl.h"
#undef FAIR_SHARE_ACTION_QUEUE_INL_H_
