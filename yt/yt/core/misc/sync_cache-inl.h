#pragma once
#ifndef SYNC_CACHE_INL_H_
#error "Direct inclusion of this file is not allowed, include sync_cache.h"
// For the sake of sane code completion.
#include "sync_cache.h"
#endif

#include "config.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class THash>
const TKey& TSyncCacheValueBase<TKey, TValue, THash>::GetKey() const
{
    return Key_;
}

template <class TKey, class TValue, class THash>
TSyncCacheValueBase<TKey, TValue, THash>::TSyncCacheValueBase(const TKey& key)
    : Key_(key)
{ }

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class THash>
TSyncSlruCacheBase<TKey, TValue, THash>::TItem::TItem(TValuePtr value)
    : Value(std::move(value))
{ }

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class THash>
TSyncSlruCacheBase<TKey, TValue, THash>::TSyncSlruCacheBase(
    TSlruCacheConfigPtr config,
    const NProfiling::TProfiler& profiler)
    : Config_(std::move(config))
    , Profiler(profiler)
    , HitWeightCounter_("/hit")
    , MissedWeightCounter_("/missed")
    , DroppedWeightCounter_("/dropped")
    , YoungerWeightCounter_("/younger")
    , OlderWeightCounter_("/older")
{
    Shards_.reset(new TShard[Config_->ShardCount]);
    for (int index = 0; index < Config_->ShardCount; ++index) {
        Shards_[index].TouchBuffer.resize(Config_->TouchBufferCapacity);
    }
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::Clear()
{
    for (size_t i = 0; i < Config_->ShardCount; ++i) {
        auto& shard = Shards_[i];
        NConcurrency::TWriterGuard guard(shard.SpinLock);

        shard.TouchBufferPosition = 0;

        shard.ItemMap.clear();

        TIntrusiveListWithAutoDelete<TItem, TDelete> youngerLruList;
        shard.YoungerLruList.Swap(youngerLruList);

        TIntrusiveListWithAutoDelete<TItem, TDelete> olderLruList;
        shard.OlderLruList.Swap(olderLruList);

        guard.Release();

        int totalItemCount = 0;
        i64 totalYoungerWeight = 0;
        i64 totalOlderWeight = 0;
        for (const auto& item : youngerLruList) {
            totalYoungerWeight += GetWeight(item.Value);
            ++totalItemCount;
        }
        for (const auto& item : olderLruList) {
            totalOlderWeight += GetWeight(item.Value);
            ++totalItemCount;
        }

        shard.YoungerWeightCounter -= totalYoungerWeight;
        shard.OlderWeightCounter -= totalOlderWeight;
        Profiler.Increment(YoungerWeightCounter_, -totalYoungerWeight);
        Profiler.Increment(OlderWeightCounter_, -totalOlderWeight);
        Size_ -= totalItemCount;

        // NB: Lists must die outside the critical section.
    }
}

template <class TKey, class TValue, class THash>
typename TSyncSlruCacheBase<TKey, TValue, THash>::TValuePtr
TSyncSlruCacheBase<TKey, TValue, THash>::Find(const TKey& key)
{
    auto* shard = GetShardByKey(key);

    NConcurrency::TReaderGuard readerGuard(shard->SpinLock);

    auto itemIt = shard->ItemMap.find(key);
    if (itemIt == shard->ItemMap.end()) {
        return nullptr;
    }

    auto* item = itemIt->second;
    bool needToDrain = Touch(shard, item);
    auto value = item->Value;

    auto weight = GetWeight(item->Value);
    Profiler.Increment(HitWeightCounter_, weight);

    readerGuard.Release();

    if (needToDrain) {
        NConcurrency::TWriterGuard writerGuard(shard->SpinLock);
        DrainTouchBuffer(shard);
    }

    return value;
}

template <class TKey, class TValue, class THash>
std::vector<typename TSyncSlruCacheBase<TKey, TValue, THash>::TValuePtr>
TSyncSlruCacheBase<TKey, TValue, THash>::GetAll()
{
    std::vector<TValuePtr> result;
    result.reseve(GetSize());
    for (const auto& shard : Shards_) {
        NConcurrency::TReaderGuard guard(shard->SpinLock);
        for (const auto& pair : shard->ItemMap) {
            result.push_back(pair.second->Value);
        }
    }
    return result;
}

template <class TKey, class TValue, class THash>
bool TSyncSlruCacheBase<TKey, TValue, THash>::TryInsert(const TValuePtr& value, TValuePtr* existingValue)
{
    const auto& key = value->GetKey();
    auto weight = GetWeight(value);
    auto* shard = GetShardByKey(key);

    NConcurrency::TWriterGuard guard(shard->SpinLock);

    DrainTouchBuffer(shard);

    auto itemIt = shard->ItemMap.find(key);
    if (itemIt != shard->ItemMap.end()) {
        Profiler.Increment(DroppedWeightCounter_, weight);
        if (existingValue) {
            *existingValue = itemIt->second->Value;
        }
        return false;
    }

    auto* item = new TItem(value);
    YT_VERIFY(shard->ItemMap.insert(std::make_pair(key, item)).second);
    ++Size_;

    Profiler.Increment(MissedWeightCounter_, weight);

    PushToYounger(shard, item);

    // NB: Releases the lock.
    Trim(shard, guard);

    OnAdded(value);

    return true;
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::Trim(TShard* shard, NConcurrency::TWriterGuard& guard)
{
    // Move from older to younger.
    while (!shard->OlderLruList.Empty() &&
           Config_->ShardCount * shard->OlderWeightCounter > Config_->Capacity * (1 - Config_->YoungerSizeFraction))
    {
        auto* item = &*(--shard->OlderLruList.End());
        MoveToYounger(shard, item);
    }

    // Evict from younger.
    std::vector<TValuePtr> evictedValues;
    while (!shard->YoungerLruList.Empty() &&
           Config_->ShardCount * (shard->YoungerWeightCounter + shard->OlderWeightCounter) > Config_->Capacity)
    {
        auto* item = &*(--shard->YoungerLruList.End());
        auto value = item->Value;

        Pop(shard, item);

        YT_VERIFY(shard->ItemMap.erase(value->GetKey()) == 1);
        --Size_;

        evictedValues.emplace_back(std::move(item->Value));

        delete item;
    }

    guard.Release();

    for (const auto& value : evictedValues) {
        OnRemoved(value);
    }
}

template <class TKey, class TValue, class THash>
bool TSyncSlruCacheBase<TKey, TValue, THash>::TryRemove(const TKey& key)
{
    auto* shard = GetShardByKey(key);

    NConcurrency::TWriterGuard guard(shard->SpinLock);

    DrainTouchBuffer(shard);

    auto it = shard->ItemMap.find(key);
    if (it == shard->ItemMap.end()) {
        return false;
    }

    auto* item = it->second;
    auto value = item->Value;

    shard->ItemMap.erase(it);
    --Size_;

    Pop(shard, item);

    delete item;

    guard.Release();

    OnRemoved(value);

    return true;
}

template <class TKey, class TValue, class THash>
bool TSyncSlruCacheBase<TKey, TValue, THash>::TryRemove(const TValuePtr& value)
{
    const auto& key = value->GetKey();
    auto* shard = GetShardByKey(key);

    NConcurrency::TWriterGuard guard(shard->SpinLock);

    DrainTouchBuffer(shard);

    auto itemIt = shard->ItemMap.find(key);
    if (itemIt == shard->ItemMap.end()) {
        return false;
    }

    auto* item = itemIt->second;
    if (item->Value != value) {
        return false;
    }

    shard->ItemMap.erase(itemIt);
    --Size_;

    Pop(shard, item);

    delete item;

    guard.Release();

    OnRemoved(value);

    return true;
}

template <class TKey, class TValue, class THash>
auto TSyncSlruCacheBase<TKey, TValue, THash>::GetShardByKey(const TKey& key) const -> TShard*
{
    return &Shards_[THash()(key) % Config_->ShardCount];
}

template <class TKey, class TValue, class THash>
bool TSyncSlruCacheBase<TKey, TValue, THash>::Touch(TShard* shard, TItem* item)
{
    int capacity = shard->TouchBuffer.size();
    int index = shard->TouchBufferPosition++;
    if (index >= capacity) {
        // Drop touch request due to buffer overflow.
        // NB: We still return false since the other thread is already responsible for
        // draining the buffer.
        return false;
    }

    shard->TouchBuffer[index] = item;
    return index == capacity - 1;
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::DrainTouchBuffer(TShard* shard)
{
    int count = std::min(
        shard->TouchBufferPosition.load(),
        static_cast<int>(shard->TouchBuffer.size()));
    for (int index = 0; index < count; ++index) {
        MoveToOlder(shard, shard->TouchBuffer[index]);
    }
    shard->TouchBufferPosition = 0;
}

template <class TKey, class TValue, class THash>
i64 TSyncSlruCacheBase<TKey, TValue, THash>::GetWeight(const TValuePtr& /*value*/) const
{
    return 1;
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::OnAdded(const TValuePtr& /*value*/)
{ }

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::OnRemoved(const TValuePtr& /*value*/)
{ }

template <class TKey, class TValue, class THash>
int TSyncSlruCacheBase<TKey, TValue, THash>::GetSize() const
{
    return Size_.load(std::memory_order_relaxed);
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::PushToYounger(TShard* shard, TItem* item)
{
    YT_ASSERT(item->Empty());
    shard->YoungerLruList.PushFront(item);
    auto weight = GetWeight(item->Value);
    shard->YoungerWeightCounter += weight;
    Profiler.Increment(YoungerWeightCounter_, +weight);
    item->Younger = true;
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::MoveToYounger(TShard* shard, TItem* item)
{
    YT_ASSERT(!item->Empty());
    item->Unlink();
    shard->YoungerLruList.PushFront(item);
    if (!item->Younger) {
        auto weight = GetWeight(item->Value);
        shard->YoungerWeightCounter += weight;
        shard->OlderWeightCounter -= weight;
        Profiler.Increment(OlderWeightCounter_, -weight);
        Profiler.Increment(YoungerWeightCounter_, +weight);
        item->Younger = true;
    }
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::MoveToOlder(TShard* shard, TItem* item)
{
    YT_ASSERT(!item->Empty());
    item->Unlink();
    shard->OlderLruList.PushFront(item);
    if (item->Younger) {
        auto weight = GetWeight(item->Value);
        shard->YoungerWeightCounter -= weight;
        shard->OlderWeightCounter += weight;
        Profiler.Increment(YoungerWeightCounter_, -weight);
        Profiler.Increment(OlderWeightCounter_, +weight);
        item->Younger = false;
    }
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::Pop(TShard* shard, TItem* item)
{
    if (item->Empty()) {
        return;
    }
    auto weight = GetWeight(item->Value);
    if (item->Younger) {
        shard->YoungerWeightCounter -= weight;
        Profiler.Increment(YoungerWeightCounter_, -weight);
    } else {
        shard->OlderWeightCounter -= weight;
        Profiler.Increment(OlderWeightCounter_, -weight);
    }
    item->Unlink();
}

template <class TKey, class TValue, class THash>
void TSyncSlruCacheBase<TKey, TValue, THash>::OnProfiling()
{
    Profiler.Increment(HitWeightCounter_, 0);
    Profiler.Increment(MissedWeightCounter_, 0);
    Profiler.Increment(DroppedWeightCounter_, 0);
    Profiler.Increment(YoungerWeightCounter_, 0);
    Profiler.Increment(OlderWeightCounter_, 0);
}

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class THash>
TSimpleLruCache<TKey, TValue, THash>::TSimpleLruCache(size_t maxWeight)
    : MaxWeight_(maxWeight)
{ }

template <class TKey, class TValue, class THash>
size_t TSimpleLruCache<TKey, TValue, THash>::GetSize() const
{
    return ItemMap_.size();
}

template <class TKey, class TValue, class THash>
const TValue& TSimpleLruCache<TKey, TValue, THash>::Get(const TKey& key)
{
    auto it = ItemMap_.find(key);
    YT_VERIFY(it != ItemMap_.end());
    UpdateLruList(it);
    return it->second.Value;
}

template <class TKey, class TValue, class THash>
TValue* TSimpleLruCache<TKey, TValue, THash>::Find(const TKey& key)
{
    auto it = ItemMap_.find(key);
    if (it == ItemMap_.end()) {
        return nullptr;
    }
    UpdateLruList(it);
    return &(it->second.Value);
}

template <class TKey, class TValue, class THash>
TValue* TSimpleLruCache<TKey, TValue, THash>::Insert(const TKey& key, TValue value, size_t weight)
{
    {
        auto mapIt = ItemMap_.find(key);
        if (mapIt != ItemMap_.end()) {
            LruList_.erase(mapIt->second.LruListIterator);
            CurrentWeight_ -= mapIt->second.Weight;
            ItemMap_.erase(mapIt);
        }
    }

    auto item = TItem(std::move(value), weight);
    while (GetSize() > 0 && CurrentWeight_ + weight > MaxWeight_) {
        Pop();
    }
    auto insertResult = ItemMap_.emplace(key, std::move(item));
    YT_VERIFY(insertResult.second);
    auto mapIt = insertResult.first;
    LruList_.push_front(mapIt);
    mapIt->second.LruListIterator = LruList_.begin();
    CurrentWeight_ += weight;

    return &mapIt->second.Value;
}

template <class TKey, class TValue, class THash>
void TSimpleLruCache<TKey, TValue, THash>::Clear()
{
    ItemMap_.clear();
    CurrentWeight_ = 0;
}

template <class TKey, class TValue, class THash>
void TSimpleLruCache<TKey, TValue, THash>::Pop()
{
    auto mapIt = LruList_.back();
    CurrentWeight_ -= mapIt->second.Weight;
    ItemMap_.erase(mapIt);
    LruList_.pop_back();
}

template <class TKey, class TValue, class THash>
void TSimpleLruCache<TKey, TValue, THash>::UpdateLruList(typename TItemMap::iterator mapIt)
{
    LruList_.erase(mapIt->second.LruListIterator);
    LruList_.push_front(mapIt);
    mapIt->second.LruListIterator = LruList_.begin();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
