#pragma once
#include "object.h"

#ifndef OBJECT_INL_H_
#error "Direct inclusion of this file is not allowed, include object.h"
#endif

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

inline TObjectBase::TObjectBase(const TObjectId& id)
    : Id_(id)
{
    // This is reset to false in TCypressNodeBase ctor for non-trunk nodes.
    Flags_.Trunk = true;
}

inline TObjectBase::~TObjectBase()
{
    // To make debugging easier.
    Flags_.Disposed = true;
}

inline TObjectDynamicData* TObjectBase::GetDynamicData() const
{
    return GetTypedDynamicData<TObjectDynamicData>();
}

inline void TObjectBase::SetDestroyed()
{
    Y_ASSERT(RefCounter_ == 0);
    Flags_.Destroyed = true;
}

inline void TObjectBase::SetForeign()
{
    Flags_.Foreign = true;
}

inline const TObjectId& TObjectBase::GetId() const
{
    return Id_;
}

inline int TObjectBase::RefObject()
{
    Y_ASSERT(RefCounter_ >= 0);
    return ++RefCounter_;
}

inline int TObjectBase::UnrefObject(int count)
{
    Y_ASSERT(RefCounter_ >= count);
    return RefCounter_ -= count;
}

inline int TObjectBase::WeakRefObject(TEpoch epoch)
{
    YCHECK(IsAlive());
    Y_ASSERT(WeakRefCounter_ >= 0);

    if (epoch != WeakLockEpoch_) {
        WeakRefCounter_ = 0;
        WeakLockEpoch_ = epoch;
    }
    return ++WeakRefCounter_;
}

inline int TObjectBase::WeakUnrefObject(TEpoch epoch)
{
    Y_ASSERT(WeakRefCounter_ > 0);
    Y_ASSERT(WeakLockEpoch_ == epoch);
    return --WeakRefCounter_;
}

inline int TObjectBase::ImportRefObject()
{
    return ++ImportRefCounter_;
}

inline int TObjectBase::ImportUnrefObject()
{
    Y_ASSERT(ImportRefCounter_ > 0);
    return --ImportRefCounter_;
}

inline int TObjectBase::GetObjectRefCounter() const
{
    return RefCounter_;
}

inline int TObjectBase::GetObjectWeakRefCounter(TEpoch epoch) const
{
    return WeakLockEpoch_== epoch ? WeakRefCounter_ : 0;
}

inline int TObjectBase::GetImportRefCounter() const
{
    return ImportRefCounter_;
}

inline bool TObjectBase::IsAlive() const
{
    return RefCounter_ > 0;
}

inline bool TObjectBase::IsDestroyed() const
{
    return Flags_.Destroyed;
}

inline bool TObjectBase::IsLocked() const
{
    return WeakRefCounter_ > 0;
}

inline bool TObjectBase::IsTrunk() const
{
    return Flags_.Trunk;
}

inline bool TObjectBase::IsForeign() const
{
    return Flags_.Foreign;
}

template <class TDerived>
TDerived* TObjectBase::As()
{
    return static_cast<TDerived*>(this);
}

template <class TDerived>
const TDerived* TObjectBase::As() const
{
    return static_cast<const TDerived*>(this);
}

////////////////////////////////////////////////////////////////////////////////

inline TNonversionedObjectBase::TNonversionedObjectBase(const TObjectId& id)
    : TObjectBase(id)
{ }

////////////////////////////////////////////////////////////////////////////////

inline bool TObjectRefComparer::Compare(const TObjectBase* lhs, const TObjectBase* rhs)
{
    return lhs->GetId() < rhs->GetId();
}

inline TObjectId GetObjectId(const TObjectBase* object)
{
    return object ? object->GetId() : NullObjectId;
}

inline bool IsObjectAlive(const TObjectBase* object)
{
    return object && object->IsAlive();
}

template <class T>
std::vector<TObjectId> ToObjectIds(const T& objects, size_t sizeLimit)
{
    std::vector<TObjectId> result;
    result.reserve(std::min(objects.size(), sizeLimit));
    for (auto* object : objects) {
        if (result.size() == sizeLimit)
            break;
        result.push_back(object->GetId());
    }
    return result;
}

template <class TValue>
std::vector<TValue*> GetValuesSortedByKey(const NHydra::TReadOnlyEntityMap<TValue>& entities)
{
    std::vector<TValue*> values;
    values.reserve(entities.size());

    for (const auto& pair : entities) {
        auto* object = pair.second;
        if (IsObjectAlive(object)) {
            values.push_back(object);
        }
    }
    std::sort(values.begin(), values.end(), TObjectRefComparer::Compare);
    return values;
}

template <class TValue>
std::vector<TValue*> GetValuesSortedByKey(const THashSet<TValue*>& entities)
{
    std::vector<TValue*> values;
    values.reserve(entities.size());

    for (auto* object : entities) {
        if (IsObjectAlive(object)) {
            values.push_back(object);
        }
    }
    std::sort(values.begin(), values.end(), TObjectRefComparer::Compare);
    return values;
}

template <class TObject, class TValue>
std::vector<std::pair<TObject*, TValue>> GetPairsSortedByKey(const THashMap<TObject*, TValue>& entities)
{
    std::vector<std::pair<TObject*, TValue>> pairs;
    pairs.reserve(entities.size());

    for (auto& pair : entities) {
        if (IsObjectAlive(pair.first)) {
            pairs.push_back(pair);
        }
    }
    std::sort(pairs.begin(), pairs.end(), [] (const auto& lhs, const auto& rhs) {
        return TObjectRefComparer::Compare(lhs.first, rhs.first);
    });
    return pairs;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT
