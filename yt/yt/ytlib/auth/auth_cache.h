#pragma once

#include "public.h"

namespace NYT::NAuth {

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class TContext>
class TAuthCache
    : public virtual TRefCounted
{
public:
    TAuthCache(
        TAuthCacheConfigPtr config,
        NProfiling::TRegistry profiler = {})
        : Config_(std::move(config))
        , Profiler_(std::move(profiler))
    { }

    TFuture<TValue> Get(const TKey& key, const TContext& context);

private:
    struct TEntry
        : public TRefCounted
    {
        const TKey Key;

        TAdaptiveLock Lock;
        TContext Context;
        TFuture<TValue> Future;
        TPromise<TValue> Promise;

        NConcurrency::TDelayedExecutorCookie EraseCookie;
        NProfiling::TCpuInstant LastAccessTime;

        NProfiling::TCpuInstant LastUpdateTime;
        bool Updating = false;

        bool IsOutdated(TDuration ttl, TDuration errorTtl);
        bool IsExpired(TDuration ttl);

        TEntry(const TKey& key, const TContext& context)
            : Key(key)
            , Context(context)
            , LastAccessTime(NProfiling::GetCpuInstant())
            , LastUpdateTime(NProfiling::GetCpuInstant())
        { }
    };
    typedef TIntrusivePtr<TEntry> TEntryPtr;

    const TAuthCacheConfigPtr Config_;
    const NProfiling::TRegistry Profiler_;

    NConcurrency::TReaderWriterSpinLock SpinLock_;
    THashMap<TKey, TEntryPtr> Cache_;

    virtual TFuture<TValue> DoGet(const TKey& key, const TContext& context) noexcept = 0;
    void TryErase(const TWeakPtr<TEntry>& weakEntry);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NAuth

#define AUTH_CACHE_INL_H_
#include "auth_cache-inl.h"
#undef AUTH_CACHE_INL_H_
