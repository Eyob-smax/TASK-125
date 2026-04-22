#include <gtest/gtest.h>
#include "shelterops/infrastructure/RateLimiter.h"
#include <thread>

using namespace shelterops::infrastructure;

TEST(RateLimiter, AllowsUpToLimit) {
    RateLimiter limiter(10); // 10 rpm
    for (int i = 0; i < 10; ++i) {
        auto r = limiter.TryAcquire("tok");
        EXPECT_TRUE(r.allowed) << "Expected allowed on request " << i;
    }
}

TEST(RateLimiter, BlocksOnExceedingLimit) {
    RateLimiter limiter(5); // 5 rpm
    for (int i = 0; i < 5; ++i) limiter.TryAcquire("tok");
    auto r = limiter.TryAcquire("tok");
    EXPECT_FALSE(r.allowed);
    EXPECT_GT(r.retry_after_seconds, 0);
}

TEST(RateLimiter, DifferentKeysAreIndependent) {
    RateLimiter limiter(2);
    limiter.TryAcquire("a");
    limiter.TryAcquire("a");
    // "a" is exhausted; "b" should still be allowed
    EXPECT_FALSE(limiter.TryAcquire("a").allowed);
    EXPECT_TRUE(limiter.TryAcquire("b").allowed);
}

TEST(RateLimiter, EvictClearsState) {
    RateLimiter limiter(1);
    limiter.TryAcquire("tok"); // consume the token
    EXPECT_FALSE(limiter.TryAcquire("tok").allowed);
    limiter.Evict("tok");
    // After evict, a new bucket is created with full capacity.
    EXPECT_TRUE(limiter.TryAcquire("tok").allowed);
}

TEST(RateLimiter, ResetClearsAll) {
    RateLimiter limiter(1);
    limiter.TryAcquire("a");
    limiter.TryAcquire("b");
    limiter.Reset();
    EXPECT_TRUE(limiter.TryAcquire("a").allowed);
    EXPECT_TRUE(limiter.TryAcquire("b").allowed);
}
