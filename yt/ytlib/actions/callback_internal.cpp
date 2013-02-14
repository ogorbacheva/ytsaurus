#include "stdafx.h"
#include "callback_internal.h"

namespace NYT {
namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

#ifdef ENABLE_BIND_LOCATION_TRACKING
TBindStateBase::TBindStateBase(const ::NYT::TSourceLocation& location)
    : Location_(location)
{ }
#endif

TBindStateBase::~TBindStateBase()
{ }

bool TCallbackBase::IsNull() const
{
    return BindState.Get() == NULL;
}

void TCallbackBase::Reset()
{
    BindState = NULL;
    UntypedInvoke = NULL;
}

void* TCallbackBase::GetHandle() const
{
    return (void*)((size_t)(void*)BindState.Get() ^ (size_t)(void*)UntypedInvoke);
}

void TCallbackBase::Swap(TCallbackBase& other)
{
    TIntrusivePtr<TBindStateBase> tempBindState = std::move(other.BindState);
    TUntypedInvokeFunction tempUntypedInvoke = std::move(other.UntypedInvoke);

    other.BindState = std::move(BindState);
    other.UntypedInvoke = std::move(UntypedInvoke);

    BindState = std::move(tempBindState);
    UntypedInvoke = std::move(tempUntypedInvoke);
}

bool TCallbackBase::Equals(const TCallbackBase& other) const
{
    return
        BindState.Get() == other.BindState.Get() &&
        UntypedInvoke == other.UntypedInvoke;
}

TCallbackBase::TCallbackBase(const TCallbackBase& other)
    : BindState(other.BindState)
    , UntypedInvoke(other.UntypedInvoke)
{ }

TCallbackBase::TCallbackBase(TCallbackBase&& other)
    : BindState(std::move(other.BindState))
    , UntypedInvoke(std::move(other.UntypedInvoke))
{ }

TCallbackBase::TCallbackBase(TIntrusivePtr<TBindStateBase>&& bindState)
    : BindState(std::move(bindState))
    , UntypedInvoke(NULL)
{
    YASSERT(!BindState || BindState->GetRefCount() == 1);
}

TCallbackBase::~TCallbackBase()
{ }

////////////////////////////////////////////////////////////////////////////////

}  // namespace NYT
}  // namespace NDetail
