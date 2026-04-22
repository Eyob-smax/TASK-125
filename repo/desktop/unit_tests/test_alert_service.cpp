#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AlertService.h"
#include "shelterops/services/AuditService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using shelterops::domain::AlertThreshold;
using shelterops::domain::AlertType;
using shelterops::domain::UserRole;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "event_type TEXT NOT NULL, description TEXT NOT NULL, actor_id INTEGER, "
            "occurred_at INTEGER NOT NULL, entity_type TEXT, entity_id INTEGER)");
    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("INSERT INTO inventory_categories VALUES(1,'Food','bag',7,14,1)");
    g->Exec("CREATE TABLE inventory_items(item_id INTEGER PRIMARY KEY, "
            "category_id INTEGER NOT NULL, name TEXT NOT NULL, description TEXT, "
            "storage_location TEXT, quantity INTEGER NOT NULL DEFAULT 0, "
            "unit_cost_cents INTEGER NOT NULL DEFAULT 0, expiration_date INTEGER, "
            "serial_number TEXT UNIQUE, barcode TEXT, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, anonymized_at INTEGER)");
    g->Exec("CREATE TABLE item_usage_history(usage_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, period_date INTEGER NOT NULL, "
            "quantity_used INTEGER NOT NULL DEFAULT 0, UNIQUE(item_id,period_date))");
    g->Exec("CREATE TABLE inbound_records(record_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, quantity INTEGER NOT NULL, "
            "received_at INTEGER NOT NULL, received_by INTEGER NOT NULL, "
            "vendor TEXT, unit_cost_cents INTEGER NOT NULL DEFAULT 0, lot_number TEXT, notes TEXT)");
    g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER, "
            "check_in_at INTEGER, check_out_at INTEGER, status TEXT, "
            "nightly_price_cents INTEGER DEFAULT 0, total_price_cents INTEGER DEFAULT 0, "
            "created_at INTEGER, created_by INTEGER)");
    g->Exec("CREATE TABLE outbound_records(record_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, quantity INTEGER NOT NULL, "
            "issued_at INTEGER NOT NULL, issued_by INTEGER NOT NULL, "
            "recipient TEXT, reason TEXT NOT NULL, booking_id INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE alert_states(alert_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, alert_type TEXT NOT NULL, "
            "triggered_at INTEGER NOT NULL, acknowledged_at INTEGER, acknowledged_by INTEGER, "
            "UNIQUE(item_id,alert_type,triggered_at))");
}

class AlertSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_         = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        inv_repo_   = std::make_unique<InventoryRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_  = std::make_unique<AuditService>(*audit_repo_);
        svc_        = std::make_unique<AlertService>(*inv_repo_, *audit_svc_);
    }

    int64_t InsertItem(const std::string& name, int qty,
                       int64_t expiry = 0, const std::string& barcode = "") {
        NewItemParams p;
        p.category_id = 1; p.name = name;
        p.serial_number = ""; p.barcode = barcode;
        p.unit_cost_cents = 0;
        if (expiry) p.expiration_date = expiry;
        auto item_id = inv_repo_->InsertItem(p, 1000);
        if (qty > 0) {
            inv_repo_->InsertInbound(item_id, qty, "", "", 0, 1, 1000, "");
        }
        return item_id;
    }

    std::unique_ptr<Database>              db_;
    std::unique_ptr<InventoryRepository>   inv_repo_;
    std::unique_ptr<AuditRepository>       audit_repo_;
    std::unique_ptr<AuditService>          audit_svc_;
    std::unique_ptr<AlertService>          svc_;
};

TEST_F(AlertSvcTest, ScanTriggersLowStockAlert) {
    InsertItem("LowItem", 2); // qty=2, threshold tests low-stock if avg usage suggests need

    AlertThreshold thresholds;
    thresholds.low_stock_qty     = 5;  // trigger if qty < 5
    thresholds.expiring_soon_days = 30;

    int64_t now = 1000000;
    auto report = svc_->Scan(now, thresholds);
    EXPECT_GE(report.new_alerts.size(), 1u);
}

TEST_F(AlertSvcTest, ScanTriggersExpiringSoonAlert) {
    int64_t now = 1000000;
    int64_t soon_expiry = now + (10 * 86400); // expires in 10 days
    InsertItem("ExpItem", 50, soon_expiry);

    AlertThreshold thresholds;
    thresholds.low_stock_qty      = 1;
    thresholds.expiring_soon_days = 30; // 30-day window, item expires in 10

    auto report = svc_->Scan(now, thresholds);
    bool found_expiring = false;
    for (const auto& a : report.new_alerts) {
        if (a.type == AlertType::ExpiringSoon) found_expiring = true;
    }
    EXPECT_TRUE(found_expiring);
}

TEST_F(AlertSvcTest, AcknowledgeAlertClearsFromActive) {
    InsertItem("LowItem2", 1);

    AlertThreshold thresholds;
    thresholds.low_stock_qty     = 10;
    thresholds.expiring_soon_days = 30;

    int64_t now = 1000000;
    svc_->Scan(now, thresholds);
    auto active = svc_->ListActive();
    ASSERT_GE(active.size(), 1u);

    UserContext ctx;
    ctx.user_id = 1; ctx.role = UserRole::Administrator;
    auto err = svc_->AcknowledgeAlert(active[0].alert_id, ctx, now + 1);
    EXPECT_FALSE(err.has_value());

    auto after = svc_->ListActive();
    for (const auto& a : after) {
        EXPECT_NE(active[0].alert_id, a.alert_id);
    }
}

TEST_F(AlertSvcTest, AuditorCannotAcknowledgeAlert) {
    InsertItem("LowItem3", 1);
    AlertThreshold thresholds;
    thresholds.low_stock_qty = 10;
    thresholds.expiring_soon_days = 30;
    const int64_t now = 1000000;
    svc_->Scan(now, thresholds);
    auto active = svc_->ListActive();
    ASSERT_FALSE(active.empty());

    UserContext auditor_ctx;
    auditor_ctx.user_id = 2;
    auditor_ctx.role = UserRole::Auditor;
    auto err = svc_->AcknowledgeAlert(active[0].alert_id, auditor_ctx, now + 1);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(shelterops::common::ErrorCode::Forbidden, err->code);
}
