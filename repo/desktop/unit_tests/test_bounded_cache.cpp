#include <gtest/gtest.h>
#include "shelterops/infrastructure/BoundedCache.h"
#include <thread>
#include <chrono>

using namespace shelterops::infrastructure;

TEST(BoundedCache, BasicPutGet) {
    BoundedCache<std::string, int> cache(10);
    cache.Put("a", 1);
    auto v = cache.Get("a");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 1);
}

TEST(BoundedCache, MissingReturnsNullopt) {
    BoundedCache<std::string, int> cache(10);
    EXPECT_FALSE(cache.Get("x").has_value());
}

TEST(BoundedCache, LruEvictionAtCapacity) {
    BoundedCache<int, int> cache(3);
    cache.Put(1, 10);
    cache.Put(2, 20);
    cache.Put(3, 30);
    cache.Put(4, 40); // should evict 1 (LRU)
    EXPECT_FALSE(cache.Get(1).has_value());
    EXPECT_TRUE(cache.Get(2).has_value());
    EXPECT_TRUE(cache.Get(3).has_value());
    EXPECT_TRUE(cache.Get(4).has_value());
}

TEST(BoundedCache, AccessMakesEntryRecent) {
    BoundedCache<int, int> cache(3);
    cache.Put(1, 10);
    cache.Put(2, 20);
    cache.Put(3, 30);
    cache.Get(1); // 1 is now most-recently-used
    cache.Put(4, 40); // should evict 2 (now LRU)
    EXPECT_TRUE(cache.Get(1).has_value());
    EXPECT_FALSE(cache.Get(2).has_value());
}

TEST(BoundedCache, TtlExpiry) {
    BoundedCache<int, int> cache(10, std::chrono::seconds{0}); // no expiry
    cache.Put(1, 99);
    EXPECT_TRUE(cache.Get(1).has_value());
}

TEST(BoundedCache, InvalidateRemoves) {
    BoundedCache<std::string, int> cache(10);
    cache.Put("x", 5);
    cache.Invalidate("x");
    EXPECT_FALSE(cache.Get("x").has_value());
}

TEST(BoundedCache, ClearEmptiesAll) {
    BoundedCache<int, int> cache(10);
    cache.Put(1, 1); cache.Put(2, 2);
    cache.Clear();
    EXPECT_EQ(cache.Size(), 0u);
}
