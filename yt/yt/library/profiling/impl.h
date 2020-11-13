#pragma once

#include "public.h"
#include "sensor.h"

#include <yt/core/misc/weak_ptr.h>
#include <yt/core/misc/ref_counted.h>

namespace NYT::NProfiling {

////////////////////////////////////////////////////////////////////////////////

struct IRegistryImpl
    : public TRefCounted
{
public:
    virtual ICounterImplPtr RegisterCounter(
        const TString& name,
        const TTagSet& tags,
        TSensorOptions options) = 0;

    virtual ITimeCounterImplPtr RegisterTimeCounter(
        const TString& name,
        const TTagSet& tags,
        TSensorOptions options) = 0;

    virtual IGaugeImplPtr RegisterGauge(
        const TString& name,
        const TTagSet& tags,
        TSensorOptions options) = 0;

    virtual ISummaryImplPtr RegisterSummary(
        const TString& name,
        const TTagSet& tags,
        TSensorOptions options) = 0;

    virtual ITimerImplPtr RegisterTimerSummary(
        const TString& name,
        const TTagSet& tags,
        TSensorOptions options) = 0;

    virtual void RegisterFuncCounter(
        const TString& name,
        const TTagSet& tags,
        TSensorOptions options,
        const TIntrusivePtr<TRefCounted>& owner,
        std::function<i64()> reader) = 0;

    virtual void RegisterFuncGauge(
        const TString& name,
        const TTagSet& tags,
        TSensorOptions options,
        const TIntrusivePtr<TRefCounted>& owner,
        std::function<double()> reader) = 0;

    virtual void RegisterProducer(
        const TString& prefix,
        const TTagSet& tags,
        TSensorOptions options,
        const ISensorProducerPtr& owner) = 0;
};

DEFINE_REFCOUNTED_TYPE(IRegistryImpl)

IRegistryImplPtr GetGlobalRegistry();

////////////////////////////////////////////////////////////////////////////////

struct ICounterImpl
    : public TRefCounted
{
    virtual void Increment(i64 delta) = 0;
};

DEFINE_REFCOUNTED_TYPE(ICounterImpl)

////////////////////////////////////////////////////////////////////////////////

struct ITimeCounterImpl
    : public TRefCounted
{
    virtual void Add(TDuration delta) = 0;
};

DEFINE_REFCOUNTED_TYPE(ITimeCounterImpl)

////////////////////////////////////////////////////////////////////////////////

struct IGaugeImpl
    : public TRefCounted
{
    virtual void Update(double value) = 0;
};

DEFINE_REFCOUNTED_TYPE(IGaugeImpl)

////////////////////////////////////////////////////////////////////////////////

struct ISummaryImpl
    : public TRefCounted
{
    virtual void Record(double value) = 0;
};

DEFINE_REFCOUNTED_TYPE(ISummaryImpl)

////////////////////////////////////////////////////////////////////////////////

struct ITimerImpl
    : public TRefCounted
{
    virtual void Record(TDuration value) = 0;
};

DEFINE_REFCOUNTED_TYPE(ITimerImpl)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NProfiling
