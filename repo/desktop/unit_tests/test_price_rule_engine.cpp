#include <gtest/gtest.h>
#include "shelterops/domain/PriceRuleEngine.h"

using namespace shelterops::domain;

static PriceRule MakeRule(int64_t id, PriceAdjustmentType type, double amount,
                           bool active = true) {
    PriceRule r;
    r.rule_id = id;
    r.adjustment_type = type;
    r.amount = amount;
    r.is_active = active;
    r.valid_from = 0;
    r.valid_to   = 9999999999LL;
    return r;
}

TEST(PriceRuleEngine, FixedDiscount) {
    auto r = MakeRule(1, PriceAdjustmentType::FixedDiscountCents, 500.0);
    auto result = ApplyPriceRules(2000, {r}, {}, 1000);
    EXPECT_EQ(1500, result.final_cents);
    ASSERT_EQ(1u, result.applied_rule_ids.size());
    EXPECT_EQ(1, result.applied_rule_ids[0]);
}

TEST(PriceRuleEngine, PercentDiscount) {
    auto r = MakeRule(1, PriceAdjustmentType::PercentDiscount, 10.0);
    auto result = ApplyPriceRules(1000, {r}, {}, 1000);
    EXPECT_EQ(900, result.final_cents);
}

TEST(PriceRuleEngine, FixedSurcharge) {
    auto r = MakeRule(1, PriceAdjustmentType::FixedSurchargeCents, 300.0);
    auto result = ApplyPriceRules(1000, {r}, {}, 1000);
    EXPECT_EQ(1300, result.final_cents);
}

TEST(PriceRuleEngine, PercentSurcharge) {
    auto r = MakeRule(1, PriceAdjustmentType::PercentSurcharge, 20.0);
    auto result = ApplyPriceRules(1000, {r}, {}, 1000);
    EXPECT_EQ(1200, result.final_cents);
}

TEST(PriceRuleEngine, DiscountsBeforeSurcharges) {
    // Discount 50% first, then +500 surcharge.
    // On 1000: after discount=500, after surcharge=1000.
    // If applied in reverse: 1000+500=1500, 1500*0.5=750. Should be 1000.
    auto d = MakeRule(1, PriceAdjustmentType::PercentDiscount, 50.0);
    auto s = MakeRule(2, PriceAdjustmentType::FixedSurchargeCents, 500.0);
    auto result = ApplyPriceRules(1000, {d, s}, {}, 1000);
    EXPECT_EQ(1000, result.final_cents);
}

TEST(PriceRuleEngine, FlooredAtZero) {
    auto r = MakeRule(1, PriceAdjustmentType::FixedDiscountCents, 99999.0);
    auto result = ApplyPriceRules(500, {r}, {}, 1000);
    EXPECT_EQ(0, result.final_cents);
}

TEST(PriceRuleEngine, InactiveRuleSkipped) {
    auto r = MakeRule(1, PriceAdjustmentType::FixedDiscountCents, 500.0, false);
    auto result = ApplyPriceRules(1000, {r}, {}, 1000);
    EXPECT_EQ(1000, result.final_cents);
    EXPECT_TRUE(result.applied_rule_ids.empty());
}

TEST(PriceRuleEngine, OutsideValidityWindowSkipped) {
    PriceRule r = MakeRule(1, PriceAdjustmentType::FixedDiscountCents, 200.0);
    r.valid_from = 9000000000LL; // far future
    r.valid_to   = 9999999999LL;
    auto result = ApplyPriceRules(1000, {r}, {}, 1000);
    EXPECT_EQ(1000, result.final_cents);
}

TEST(PriceRuleEngine, MultipleRulesCombine) {
    auto d1 = MakeRule(1, PriceAdjustmentType::FixedDiscountCents, 100.0);
    auto d2 = MakeRule(2, PriceAdjustmentType::FixedDiscountCents, 200.0);
    auto s1 = MakeRule(3, PriceAdjustmentType::FixedSurchargeCents, 50.0);
    // Base 1000, disc -100 = 900, disc -200 = 700, surcharge +50 = 750
    auto result = ApplyPriceRules(1000, {d1, d2, s1}, {}, 1000);
    EXPECT_EQ(750, result.final_cents);
    EXPECT_EQ(3u, result.applied_rule_ids.size());
}
