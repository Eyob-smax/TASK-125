#include <gtest/gtest.h>
#include "shelterops/domain/RetentionPolicy.h"

using namespace shelterops::domain;

static constexpr int64_t kYear = 365LL * 24 * 3600;

static RetentionRule MakeRule(
    const std::string& entity_type,
    int years,
    RetentionActionKind action = RetentionActionKind::Anonymize)
{
    return {entity_type, years, action};
}

// ---------------------------------------------------------------------------
// NeedsRetention
// ---------------------------------------------------------------------------
TEST(NeedsRetention, RecordExceedingPeriodNeeds) {
    int64_t now       = 10 * kYear;
    int64_t created   = now - 8 * kYear;   // 8 years ago
    auto rule = MakeRule("users", 7);
    EXPECT_TRUE(NeedsRetention(rule, created, now));
}

TEST(NeedsRetention, RecentRecordDoesNotNeed) {
    int64_t now     = 10 * kYear;
    int64_t created = now - 5 * kYear;    // 5 years ago < 7 year threshold
    auto rule = MakeRule("users", 7);
    EXPECT_FALSE(NeedsRetention(rule, created, now));
}

TEST(NeedsRetention, ExactlyAtBoundaryNeeds) {
    int64_t now     = 10 * kYear;
    int64_t created = now - 7 * kYear;    // exactly 7 years ago
    auto rule = MakeRule("users", 7);
    EXPECT_TRUE(NeedsRetention(rule, created, now));
}

TEST(NeedsRetention, ZeroYearsRetentionNeverNeeds) {
    RetentionRule r{"users", 0, RetentionActionKind::Delete};
    EXPECT_FALSE(NeedsRetention(r, 0, 99999999LL));
}

// ---------------------------------------------------------------------------
// EvaluateRetention
// ---------------------------------------------------------------------------
TEST(EvaluateRetention, AlreadyAnonymizedSkipped) {
    int64_t now = 10 * kYear;
    RetentionCandidate c{1, "users", now - 8 * kYear, /*already_anonymized=*/true};
    auto rules = {MakeRule("users", 7)};
    auto decisions = EvaluateRetention({c}, rules, now);
    EXPECT_TRUE(decisions.empty());
}

TEST(EvaluateRetention, OverdueRecordProducesDecision) {
    int64_t now = 10 * kYear;
    RetentionCandidate c{42, "users", now - 8 * kYear, false};
    auto decisions = EvaluateRetention(
        {c},
        {MakeRule("users", 7, RetentionActionKind::Anonymize)},
        now);
    ASSERT_EQ(decisions.size(), 1u);
    EXPECT_EQ(decisions[0].entity_id, 42);
    EXPECT_EQ(decisions[0].action, RetentionActionKind::Anonymize);
    EXPECT_FALSE(decisions[0].reason.empty());
}

TEST(EvaluateRetention, DeleteActionPropagated) {
    int64_t now = 10 * kYear;
    RetentionCandidate c{7, "bookings", now - 9 * kYear, false};
    auto decisions = EvaluateRetention(
        {c},
        {MakeRule("bookings", 7, RetentionActionKind::Delete)},
        now);
    ASSERT_EQ(decisions.size(), 1u);
    EXPECT_EQ(decisions[0].action, RetentionActionKind::Delete);
}

TEST(EvaluateRetention, NoRuleForEntityTypeSkipped) {
    int64_t now = 10 * kYear;
    RetentionCandidate c{1, "animals", now - 9 * kYear, false};
    auto decisions = EvaluateRetention(
        {c},
        {MakeRule("users", 7)},  // no rule for "animals"
        now);
    EXPECT_TRUE(decisions.empty());
}

TEST(EvaluateRetention, RecentRecordNoDecision) {
    int64_t now = 10 * kYear;
    RetentionCandidate c{1, "users", now - 3 * kYear, false};
    auto decisions = EvaluateRetention(
        {c},
        {MakeRule("users", 7)},
        now);
    EXPECT_TRUE(decisions.empty());
}

TEST(EvaluateRetention, MultipleEntityTypes) {
    int64_t now = 10 * kYear;
    std::vector<RetentionCandidate> candidates = {
        {1, "users",    now - 8 * kYear, false},
        {2, "bookings", now - 9 * kYear, false},
        {3, "animals",  now - 2 * kYear, false},  // not overdue
    };
    std::vector<RetentionRule> rules = {
        MakeRule("users",    7),
        MakeRule("bookings", 7),
        MakeRule("animals",  7),
    };
    auto decisions = EvaluateRetention(candidates, rules, now);
    EXPECT_EQ(decisions.size(), 2u);  // users + bookings; animals not overdue
}
