#include <gtest/gtest.h>
#include "shelterops/shell/TrayBadgeState.h"
#include "shelterops/repositories/InventoryRepository.h"

using namespace shelterops::shell;
using namespace shelterops::repositories;

// TrayManager requires Win32 APIs and cannot be tested in Docker.
// This file tests the cross-platform TrayBadgeState logic that drives tray updates.

class TrayBadgeStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        state_ = std::make_unique<TrayBadgeState>();
    }

    std::unique_ptr<TrayBadgeState> state_;
};

TEST_F(TrayBadgeStateTest, InitialCountIsZero) {
    EXPECT_EQ(0, state_->TotalBadgeCount());
    EXPECT_FALSE(state_->HasAlerts());
}

TEST_F(TrayBadgeStateTest, UpdateSetsCountsFromAlerts) {
    std::vector<AlertStateRecord> alerts;
    AlertStateRecord a1;
    a1.alert_type = "low_stock";
    a1.item_id = 1;
    a1.triggered_at = 1000;
    alerts.push_back(a1);
    
    AlertStateRecord a2;
    a2.alert_type = "expiring_soon";
    a2.item_id = 2;
    a2.triggered_at = 1000;
    alerts.push_back(a2);
    
    state_->Update(alerts);
    EXPECT_EQ(2, state_->TotalBadgeCount());
    EXPECT_TRUE(state_->HasAlerts());
}

TEST_F(TrayBadgeStateTest, TotalBadgeCountSumsAllCategories) {
    state_->low_stock_count = 3;
    state_->expiring_count = 2;
    state_->expired_count = 1;
    EXPECT_EQ(6, state_->TotalBadgeCount());
}

TEST_F(TrayBadgeStateTest, HasAlertsReturnsTrueWhenCountPositive) {
    state_->low_stock_count = 1;
    EXPECT_TRUE(state_->HasAlerts());
}
