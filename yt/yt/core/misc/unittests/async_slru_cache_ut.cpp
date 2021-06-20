#include <yt/yt/core/test_framework/framework.h>

#include <yt/yt/core/misc/async_slru_cache.h>

namespace NYT {
namespace {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TSimpleCachedValue)

class TSimpleCachedValue
    : public TAsyncCacheValueBase<int, TSimpleCachedValue>
{
public:
    explicit TSimpleCachedValue(int key, int value, int weight = 1)
        : TAsyncCacheValueBase(key)
        , Value(value)
        , Weight(weight)
    { }

    int Value;
    int Weight;
};

DEFINE_REFCOUNTED_TYPE(TSimpleCachedValue)

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TSimpleSlruCache)

class TSimpleSlruCache
    : public TAsyncSlruCacheBase<int, TSimpleCachedValue>
{
public:
    explicit TSimpleSlruCache(TSlruCacheConfigPtr config)
        : TAsyncSlruCacheBase(std::move(config))
    { }

protected:
    virtual i64 GetWeight(const TSimpleCachedValuePtr& value) const override
    {
        return value->Weight;
    }
};

DEFINE_REFCOUNTED_TYPE(TSimpleSlruCache)

////////////////////////////////////////////////////////////////////////////////

std::vector<int> GetAllKeys(const TSimpleSlruCachePtr& cache)
{
    std::vector<int> result;

    for (const auto& cachedValue : cache->GetAll()) {
        result.emplace_back(cachedValue->GetKey());
    }
    std::sort(result.begin(), result.end());

    return result;
}

std::vector<int> GetKeysFromRanges(std::vector<std::pair<int, int>> ranges)
{
    std::vector<int> result;

    for (const auto& [from, to] : ranges) {
        for (int i = from; i < to; ++i) {
            result.push_back(i);
        }
    }
    std::sort(result.begin(), result.end());

    return result;
}

////////////////////////////////////////////////////////////////////////////////

TSlruCacheConfigPtr CreateCacheConfig(i64 cacheSize)
{
    auto config = New<TSlruCacheConfig>(cacheSize);
    config->ShardCount = 1;

    return config;
}

////////////////////////////////////////////////////////////////////////////////

TEST(TAsyncSlruCacheTest, Simple)
{
    const int cacheSize = 10;
    auto config = CreateCacheConfig(cacheSize);
    auto cache = New<TSimpleSlruCache>(config);

    for (int i = 0; i < 2 * cacheSize; ++i) {
        auto cookie = cache->BeginInsert(i);
        EXPECT_TRUE(cookie.IsActive());
        cookie.EndInsert(New<TSimpleCachedValue>(i, i));
    }

    // Cache size is small, so on the second pass every element should be missing too.
    for (int i = 0; i < 2 * cacheSize; ++i) {
        auto cookie = cache->BeginInsert(i);
        EXPECT_TRUE(cookie.IsActive());
        cookie.EndInsert(New<TSimpleCachedValue>(i, i * 2));
    }

    // Only last cacheSize items.
    EXPECT_EQ(GetAllKeys(cache), GetKeysFromRanges({{cacheSize, 2 * cacheSize}}));

    // Check that Find() works as expected.
    for (int i = 0; i < cacheSize; ++i) {
        auto cachedValue = cache->Find(i);
        EXPECT_EQ(cachedValue, nullptr);
    }
    for (int i = cacheSize; i < 2 * cacheSize; ++i) {
        auto cachedValue = cache->Find(i);
        ASSERT_NE(cachedValue, nullptr);
        EXPECT_EQ(cachedValue->GetKey(), i);
        EXPECT_EQ(cachedValue->Value, i * 2);
    }
}

TEST(TAsyncSlruCacheTest, Youngest)
{
    const int cacheSize = 10;
    const int oldestSize = 5;
    auto config = CreateCacheConfig(cacheSize);
    config->YoungerSizeFraction = 0.5;
    auto cache = New<TSimpleSlruCache>(config);

    for (int i = 0; i < oldestSize; ++i) {
        auto cookie = cache->BeginInsert(i);
        EXPECT_TRUE(cookie.IsActive());
        cookie.EndInsert(New<TSimpleCachedValue>(i, i));
        // Move to oldest.
        cache->Find(i);
    }

    for (int i = cacheSize; i < 2 * cacheSize; ++i) {
        auto cookie = cache->BeginInsert(i);
        EXPECT_TRUE(cookie.IsActive());
        cookie.EndInsert(New<TSimpleCachedValue>(i, i));
    }

    EXPECT_EQ(GetAllKeys(cache), GetKeysFromRanges({{0, oldestSize}, {cacheSize + oldestSize, 2 * cacheSize}}));
}

TEST(TAsyncSlruCacheTest, Resurrection)
{
    const int cacheSize = 10;
    auto config = CreateCacheConfig(cacheSize);
    auto cache = New<TSimpleSlruCache>(config);

    std::vector<TSimpleCachedValuePtr> values;

    for (int i = 0; i < 2 * cacheSize; ++i) {
        auto value = New<TSimpleCachedValue>(i, i);
        auto cookie = cache->BeginInsert(i);
        EXPECT_TRUE(cookie.IsActive());
        cookie.EndInsert(value);
        values.push_back(value);
    }

    EXPECT_EQ(cache->GetSize(), cacheSize);
    // GetAll() returns values which are in cache or can be resurrected.
    EXPECT_EQ(GetAllKeys(cache), GetKeysFromRanges({{0, 2 * cacheSize}}));

    for (int i = 0; i < 2 * cacheSize; ++i) {
        // It's expired because our cache is too smal.
        EXPECT_EQ(cache->Find(i), nullptr);
        // But lookup can find and restore it (and make some other values expired)
        // because the value is alive in 'values' vector.
        EXPECT_EQ(cache->Lookup(i).Get().ValueOrThrow(), values[i]);
    }
}

TEST(TAsyncSlruCacheTest, LookupBetweenBeginAndEndInsert)
{
    const int cacheSize = 10;
    auto config = CreateCacheConfig(cacheSize);
    auto cache = New<TSimpleSlruCache>(config);

    auto cookie = cache->BeginInsert(1);
    EXPECT_TRUE(cookie.IsActive());

    EXPECT_FALSE(cache->Find(1).operator bool ());

    auto future = cache->Lookup(1);
    EXPECT_TRUE(future.operator bool());
    EXPECT_FALSE(future.IsSet());

    auto value = New<TSimpleCachedValue>(1, 10);
    cookie.EndInsert(value);

    EXPECT_TRUE(future.IsSet());
    EXPECT_TRUE(future.Get().IsOK());
    EXPECT_EQ(value, future.Get().Value());
}

TEST(TAsyncSlruCacheTest, UpdateWeight)
{
    const int cacheSize = 10;
    auto config = CreateCacheConfig(cacheSize);
    auto cache = New<TSimpleSlruCache>(config);

    for (int i = 0; i < cacheSize; ++i) {
        auto cookie = cache->BeginInsert(i);
        EXPECT_TRUE(cookie.IsActive());
        cookie.EndInsert(New<TSimpleCachedValue>(i, i));
    }

    // All values fit in cache.
    for (int i = 0; i < cacheSize; ++i) {
        auto value = cache->Find(i);
        EXPECT_NE(value, nullptr);
        EXPECT_EQ(value->GetKey(), i);
        EXPECT_EQ(value->Value, i);
    }

    {
        // When we search '0' again, it goes to the end of the queue to be deleted.
        auto value = cache->Find(0);
        value->Weight = cacheSize;
        cache->UpdateWeight(value);
        // It should not be deleted.
        EXPECT_EQ(cache->Find(0), value);
    }

    for (int i = 1; i < cacheSize; ++i) {
        EXPECT_EQ(cache->Find(i), nullptr);
    }

    {
        auto value = New<TSimpleCachedValue>(1, 1);
        auto cookie = cache->BeginInsert(1);
        EXPECT_TRUE(cookie.IsActive());
        cookie.EndInsert(value);

        // After first insert we can not find value '1' because '0' was in 'oldest' segment.
        EXPECT_EQ(cache->Find(1), nullptr);
        // But now '0' should be moved to 'youngest' after Trim() call.
        // Second insert should delete '0' and insert '1' because it's newer.
        cookie = cache->BeginInsert(1);
        // Cookie is not active becase we still hold value and it can be resurrected.
        EXPECT_FALSE(cookie.IsActive());

        // '0' is deleted, because it is too big.
        EXPECT_EQ(cache->Find(0), nullptr);
        EXPECT_EQ(cache->Find(1), value);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT
