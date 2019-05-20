#pragma once

#include <yt/core/concurrency/rw_spinlock.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

//! A synchronization object to load and store nontrivial object.
//! It looks like atomics but for objects.
template <class T>
class TAtomicObject
{
public:
    TAtomicObject() = default;

    template <class U>
    TAtomicObject(U&& u);

    template <class U>
    void Store(U&& u);

    //! Atomically replaces the old value with the new one and returns the old value.
    template <class U>
    T Exchange(U&& u);

    T Load() const;

private:
    T Object_;
    NConcurrency::TReaderWriterSpinLock Spinlock_;
};

////////////////////////////////////////////////////////////////////////////////

template <class TOriginal, class TSerialized>
void ToProto(TSerialized* serialized, const TAtomicObject<TOriginal>& original);

template <class TOriginal, class TSerialized>
void FromProto(TAtomicObject<TOriginal>* original, const TSerialized& serialized);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define ATOMIC_OBJECT_INL_H_
#include "atomic_object-inl.h"
#undef ATOMIC_OBJECT_INL_H_
