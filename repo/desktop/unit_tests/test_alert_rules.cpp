#include <gtest/gtest.h>
#include "shelterops/domain/AlertRules.h"
#include "shelterops/domain/Types.h"

using namespace shelterops::domain;

static AlertThreshold DefaultThresholds() {
    return AlertThreshold{.low_stock_days = 7, .expiration_days = 14};
}

static AlertCandidate MakeCandidate(
    int64_t item_id,
    int quantity, double daily_usage,
    int64_t expiry = 0,
    bool alerted_low  = false,
    bool alerted_exp  = false,
    bool alerted_expired = false)
{
    AlertCandidate c;
    c.item_id              = item_id;
    c.current_quantity     = quantity;
    c.average_daily_usage  = daily_usage;
    c.expiration_unix      = expiry;
    c.already_alerted_low_stock = alerted_low;
    c.already_alerted_expiring  = alerted_exp;
    c.already_alerted_expired   = alerted_expired;
    return c;
}

TEST(AlertRules, LowStockTriggersFired) {
    int64_t now = 1000000;
    // 3 units / 2 per day = 1.5 days < 7 threshold → fires
    auto c = MakeCandidate(1, 3, 2.0);
    auto triggers = EvaluateAlerts({c}, DefaultThresholds(), now);
    ASSERT_EQ(triggers.size(), 1u);
    EXPECT_EQ(triggers[0].type, AlertType::LowStock);
    EXPECT_EQ(triggers[0].item_id, 1);
}

TEST(AlertRules, SufficientStockNoTrigger) {
    int64_t now = 1000000;
    // 100 units / 2 per day = 50 days > 7 threshold → no alert
    auto c = MakeCandidate(1, 100, 2.0);
    EXPECT_TRUE(EvaluateAlerts({c}, DefaultThresholds(), now).empty());
}

TEST(AlertRules, AlreadyAlertedLowStockNotRepeated) {
    int64_t now = 1000000;
    auto c = MakeCandidate(1, 3, 2.0, 0, /*alerted_low=*/true);
    EXPECT_TRUE(EvaluateAlerts({c}, DefaultThresholds(), now).empty());
}

TEST(AlertRules, ExpiringSoonTriggered) {
    int64_t now    = 1000000;
    int64_t expiry = now + 10 * 86400;  // 10 days away; threshold = 14
    auto c = MakeCandidate(2, 100, 0.1, expiry);
    auto triggers = EvaluateAlerts({c}, DefaultThresholds(), now);
    bool found = false;
    for (auto& t : triggers)
        if (t.type == AlertType::ExpiringSoon && t.item_id == 2) found = true;
    EXPECT_TRUE(found);
}

TEST(AlertRules, NotExpiringSoonBeyondThreshold) {
    int64_t now    = 1000000;
    int64_t expiry = now + 30 * 86400;  // 30 days away; threshold = 14
    auto c = MakeCandidate(2, 100, 0.1, expiry);
    auto triggers = EvaluateAlerts({c}, DefaultThresholds(), now);
    for (auto& t : triggers)
        EXPECT_NE(t.type, AlertType::ExpiringSoon);
}

TEST(AlertRules, ExpiredItemTriggersExpiredAlert) {
    int64_t now    = 1000000;
    int64_t expiry = now - 86400;  // expired yesterday
    auto c = MakeCandidate(3, 5, 0.5, expiry);
    auto triggers = EvaluateAlerts({c}, DefaultThresholds(), now);
    bool found = false;
    for (auto& t : triggers)
        if (t.type == AlertType::Expired && t.item_id == 3) found = true;
    EXPECT_TRUE(found);
}

TEST(AlertRules, AlreadyAlertedExpiredNotRepeated) {
    int64_t now    = 1000000;
    int64_t expiry = now - 86400;
    auto c = MakeCandidate(3, 5, 0.5, expiry,
        /*alerted_low=*/false, /*alerted_exp=*/false, /*alerted_expired=*/true);
    auto triggers = EvaluateAlerts({c}, DefaultThresholds(), now);
    for (auto& t : triggers)
        EXPECT_NE(t.type, AlertType::Expired);
}

TEST(AlertRules, ZeroUsageNoLowStockAlert) {
    int64_t now = 1000000;
    auto c = MakeCandidate(4, 0, 0.0);  // no usage rate → infinite days of stock
    auto triggers = EvaluateAlerts({c}, DefaultThresholds(), now);
    for (auto& t : triggers)
        EXPECT_NE(t.type, AlertType::LowStock);
}

TEST(AlertRules, MultipleAlertsPerCandidate) {
    int64_t now    = 1000000;
    int64_t expiry = now + 5 * 86400;  // expiring soon
    // Also low stock: 2 units / 1.0 per day = 2 days < 7
    auto c = MakeCandidate(5, 2, 1.0, expiry);
    auto triggers = EvaluateAlerts({c}, DefaultThresholds(), now);
    bool has_low   = false, has_exp = false;
    for (auto& t : triggers) {
        if (t.type == AlertType::LowStock)    has_low = true;
        if (t.type == AlertType::ExpiringSoon) has_exp = true;
    }
    EXPECT_TRUE(has_low);
    EXPECT_TRUE(has_exp);
}
