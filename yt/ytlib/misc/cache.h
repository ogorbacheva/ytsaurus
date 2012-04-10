#pragma once

#include "common.h"

#include <ytlib/misc/error.h>
#include <ytlib/actions/future.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class THash>
class TCacheBase;

template <class TKey, class TValue, class THash = ::hash<TKey> >
class TCacheValueBase
    : public virtual TRefCounted
{
public:
    typedef TIntrusivePtr<TValue> TPtr;

    virtual ~TCacheValueBase();

    TKey GetKey() const;

protected:
    TCacheValueBase(const TKey& key);

private:
    typedef TCacheBase<TKey, TValue, THash> TCache;
    friend class TCacheBase<TKey, TValue, THash>;

    TIntrusivePtr<TCache> Cache;
    TKey Key;

};

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class THash = hash<TKey> >
class TCacheBase
    : public virtual TRefCounted
{
public:
    typedef TIntrusivePtr<TValue> TValuePtr;
    typedef TIntrusivePtr< TCacheBase<TKey, TValue, THash> > TPtr;
    typedef TValueOrError<TValuePtr> TValuePtrOrError;
    typedef TFuture<TValuePtrOrError> TAsyncValuePtrOrError;

    void Clear();
    i32 GetSize() const;
    TValuePtr Find(const TKey& key);
    yvector<TValuePtr> GetAll();

protected:
    class TInsertCookie
    {
    public:
        TInsertCookie(const TKey& key);
        ~TInsertCookie();

        TAsyncValuePtrOrError GetAsyncResult() const;
        TKey GetKey() const;
        bool IsActive() const;
        void Cancel(const TError& error);
        void EndInsert(TValuePtr value);

    private:
        friend class TCacheBase;

        TKey Key;
        TPtr Cache;
        TAsyncValuePtrOrError AsyncResult;
        bool Active;

    };

    TCacheBase();

    TAsyncValuePtrOrError Lookup(const TKey& key);
    bool BeginInsert(TInsertCookie* cookie);
    void Touch(const TKey& key);
    bool Remove(const TKey& key);

    // Called under SpinLock.
    virtual bool NeedTrim() const = 0;
    virtual void OnAdded(TValue* value);
    virtual void OnRemoved(TValue* value);

private:
    friend class TCacheValueBase<TKey, TValue, THash>;

    struct TItem
        : TIntrusiveListItem<TItem>
    {
        TItem()
            : AsyncResult()
        { }

        explicit TItem(const TValuePtr& value)
            : AsyncResult()
        {
            AsyncResult.Set(TValuePtrOrError(value));
        }

        explicit TItem(TValuePtr&& value)
            : AsyncResult()
        {
            AsyncResult.Set(TValuePtrOrError(MoveRV(value)));
        }

        TPromise<TValuePtrOrError> AsyncResult;

    };

    TSpinLock SpinLock;

    typedef yhash_map<TKey, TValue*, THash> TValueMap;
    typedef yhash_map<TKey, TItem*, THash> TItemMap;
    typedef TIntrusiveListWithAutoDelete<TItem, TDelete> TItemList;

    TValueMap ValueMap;
    TItemMap ItemMap;
    TItemList LruList;
    i32 Size;

    void EndInsert(TValuePtr value, TInsertCookie* cookie);
    void CancelInsert(const TKey& key, const TError& error);
    void Touch(TItem* item); // thread-unsafe
    void Unregister(const TKey& key);
    void TrimIfNeeded(); // thread-unsafe

};

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class THash = ::hash<TKey> >
class TSizeLimitedCache
    : public TCacheBase<TKey, TValue, THash>
{
protected:
    TSizeLimitedCache(i32 maxSize);

    virtual bool NeedTrim() const;

private:
    i32 MaxSize;

};

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue, class THash = ::hash<TKey> >
class TWeightLimitedCache
    : public TCacheBase<TKey, TValue, THash>
{
protected:
    TWeightLimitedCache(i64 maxWeight);

    virtual i64 GetWeight(TValue* value) const = 0;
    virtual void OnAdded(TValue* value);
    virtual void OnRemoved(TValue* value);
    virtual bool NeedTrim() const;

private:
    i64 TotalWeight;
    i64 MaxWeight;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define CACHE_INL_H_
#include "cache-inl.h"
#undef CACHE_INL_H_
