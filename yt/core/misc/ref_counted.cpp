#include "stdafx.h"
#include "common.h"
#include "ref_counted.h"
#include "ref_counted_tracker.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

TRefCountedBase::TRefCountedBase()
#ifdef ENABLE_REF_COUNTED_TRACKING
    : TypeCookie(nullptr)
    , InstanceSize(0)
#endif
{ }

TRefCountedBase::~TRefCountedBase()
{
#ifdef ENABLE_REF_COUNTED_TRACKING
    FinalizeTracking();
#endif
}

#ifdef ENABLE_REF_COUNTED_TRACKING

void TRefCountedBase::InitializeTracking(void* typeCookie, size_t instanceSize)
{
    YASSERT(!TypeCookie);
    TypeCookie = typeCookie;

    YASSERT(InstanceSize == 0);
    YASSERT(instanceSize > 0);
    InstanceSize = instanceSize;

    TRefCountedTracker::Get()->Allocate(typeCookie, instanceSize);
}

void TRefCountedBase::FinalizeTracking()
{
    YASSERT(TypeCookie);
    YASSERT(InstanceSize > 0);
    TRefCountedTracker::Get()->Free(TypeCookie, InstanceSize);
}

#endif

////////////////////////////////////////////////////////////////////////////////

TExtrinsicRefCounted::TExtrinsicRefCounted()
    : RefCounter(new NDetail::TRefCounter(this))
{ }

TExtrinsicRefCounted::~TExtrinsicRefCounted()
{
    // There are two common mistakes that may lead to triggering the checks below:
    // - Improper allocation/deallocation of ref-counted objects, e.g.
    //   creating an instance with raw "new" and deleting it afterwards with raw "delete"
    //   (possibly inside auto_ptr, unique_ptr, shared_ptr or similar helpers),
    //   or declaring an instance with static or automatic durations.
    // - Throwing an exception from ctor.
    YASSERT(RefCounter->GetRefCount() == 0);
}

////////////////////////////////////////////////////////////////////////////////

TIntrinsicRefCounted::TIntrinsicRefCounted()
    : RefCounter(1)
{ }

TIntrinsicRefCounted::~TIntrinsicRefCounted()
{
    // For failed assertions, see the comments in TExtrinsicRefCounted::~TExtrinsicRefCounted.
    YASSERT(NDetail::AtomicallyFetch(&RefCounter) == 0);
}

////////////////////////////////////////////////////////////////////////////////

void NDetail::TRefCounter::Dispose()
{
    delete that;
}

void NDetail::TRefCounter::Destroy()
{
    delete this;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
