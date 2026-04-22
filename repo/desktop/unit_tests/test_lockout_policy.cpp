#include <gtest/gtest.h>
#include "shelterops/services/LockoutPolicy.h"

using namespace shelterops::services;

TEST(LockoutPolicy, UnderThresholdNoLock) {
    auto d = LockoutPolicy::Evaluate(0, 0, 1000);
    EXPECT_FALSE(d.locked);
    EXPECT_EQ(d.lock_until, 0);
    EXPECT_GT(d.remaining_attempts, 0);
}

TEST(LockoutPolicy, AtThresholdLocks) {
    // 4 prior failures + 1 more = 5 = kThreshold
    auto d = LockoutPolicy::Evaluate(
        LockoutPolicy::kThreshold - 1, 0, 1000);
    EXPECT_TRUE(d.locked);
    EXPECT_EQ(d.lock_until, 1000 + LockoutPolicy::kFirstLockSec);
}

TEST(LockoutPolicy, EscalatesWhenAlreadyLocked) {
    // Account is currently locked; another threshold breach → 1h lock
    int64_t current_lock = 1000 + LockoutPolicy::kFirstLockSec;
    auto d = LockoutPolicy::Evaluate(
        LockoutPolicy::kThreshold * 2 - 1, current_lock, 1050);
    EXPECT_TRUE(d.locked);
    EXPECT_EQ(d.lock_until, 1050 + LockoutPolicy::kEscalateSec);
}

TEST(LockoutPolicy, ExpiredLockNotEscalated) {
    // Lock has already expired → treats as fresh (no escalation)
    int64_t expired_lock = 500;
    int64_t now = 2000; // now > expired_lock
    auto d = LockoutPolicy::Evaluate(
        LockoutPolicy::kThreshold - 1, expired_lock, now);
    EXPECT_TRUE(d.locked);
    EXPECT_EQ(d.lock_until, now + LockoutPolicy::kFirstLockSec);
}

TEST(LockoutPolicy, IsCurrentlyLockedTrue) {
    EXPECT_TRUE(LockoutPolicy::IsCurrentlyLocked(2000, 1000));
}

TEST(LockoutPolicy, IsCurrentlyLockedFalseExpired) {
    EXPECT_FALSE(LockoutPolicy::IsCurrentlyLocked(500, 1000));
}

TEST(LockoutPolicy, IsCurrentlyLockedFalseZero) {
    EXPECT_FALSE(LockoutPolicy::IsCurrentlyLocked(0, 1000));
}

TEST(LockoutPolicy, RemainingAttemptsCorrect) {
    auto d = LockoutPolicy::Evaluate(1, 0, 1000); // 2 failures so far; 3 remaining before 5
    EXPECT_EQ(d.remaining_attempts, LockoutPolicy::kThreshold - 2);
}
