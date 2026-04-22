#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/AlertService.h"
#include "shelterops/shell/TrayBadgeState.h"
#include "shelterops/ui/controllers/AlertsPanelController.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::ui::controllers;
using namespace shelterops::shell;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("INSERT INTO inventory_categories VALUES(1,'Medical','unit',7,30,1)");
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
    g->Exec("CREATE TABLE outbound_records(record_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, quantity INTEGER NOT NULL, "
            "issued_at INTEGER NOT NULL, issued_by INTEGER NOT NULL, "
            "recipient TEXT, reason TEXT NOT NULL, booking_id INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE alert_states(alert_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, alert_type TEXT NOT NULL, "
            "triggered_at INTEGER NOT NULL, acknowledged_at INTEGER, acknowledged_by INTEGER, "
            "UNIQUE(item_id,alert_type,triggered_at))");
}

class AlertsPanelCtrlTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_         = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        inv_repo_   = std::make_unique<InventoryRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_  = std::make_unique<AuditService>(*audit_repo_);
        alert_svc_  = std::make_unique<AlertService>(*inv_repo_, *audit_svc_);
        tray_       = std::make_unique<TrayBadgeState>();
        ctrl_       = std::make_unique<AlertsPanelController>(*alert_svc_, *tray_);

        mgr_ctx_.user_id = 1;
        mgr_ctx_.role    = UserRole::OperationsManager;
        auditor_ctx_.user_id = 2;
        auditor_ctx_.role    = UserRole::Auditor;
    }

    void SeedItem(int64_t item_id, int qty, int64_t expiry = 0) {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO inventory_items VALUES(" +
                std::to_string(item_id) + ",1,'Item" +
                std::to_string(item_id) + "',NULL,NULL," +
                std::to_string(qty) + ",0," +
                (expiry > 0 ? std::to_string(expiry) : "NULL") +
                ",NULL,NULL,1,1000,1000,NULL)");
    }

    std::unique_ptr<Database>              db_;
    std::unique_ptr<InventoryRepository>   inv_repo_;
    std::unique_ptr<AuditRepository>       audit_repo_;
    std::unique_ptr<AuditService>          audit_svc_;
    std::unique_ptr<AlertService>          alert_svc_;
    std::unique_ptr<TrayBadgeState>        tray_;
    std::unique_ptr<AlertsPanelController> ctrl_;
    UserContext                            mgr_ctx_;
    UserContext                            auditor_ctx_;
};

TEST_F(AlertsPanelCtrlTest, InitialStateIsIdle) {
    EXPECT_EQ(AlertsPanelState::Idle, ctrl_->State());
    EXPECT_TRUE(ctrl_->Alerts().empty());
    EXPECT_EQ(0, ctrl_->TotalUnacknowledged());
}

TEST_F(AlertsPanelCtrlTest, RefreshWithNoItemsStaysLoaded) {
    AlertThreshold th;
    ctrl_->Refresh(th, 10000);
    EXPECT_EQ(AlertsPanelState::Loaded, ctrl_->State());
    EXPECT_TRUE(ctrl_->Alerts().empty());
    EXPECT_EQ(0, ctrl_->TotalUnacknowledged());
}

TEST_F(AlertsPanelCtrlTest, LowStockItemTriggersAlert) {
    // Seed item with quantity=1 (below threshold at 7-day coverage)
    SeedItem(1, 1);
    // Insert daily usage so threshold check sees it
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO item_usage_history VALUES(1,1,86400,5)");
    }

    AlertThreshold th;
    ctrl_->Refresh(th, 200000);
    EXPECT_EQ(AlertsPanelState::Loaded, ctrl_->State());
    EXPECT_GE(ctrl_->Alerts().size(), 1u);
    EXPECT_GT(ctrl_->TotalUnacknowledged(), 0);
}

TEST_F(AlertsPanelCtrlTest, ExpiringItemTriggersAlert) {
    // Expiration date = now + 5 days (within 14-day alert window)
    int64_t now = 100000;
    int64_t expiry = now + 5 * 86400;
    SeedItem(2, 10, expiry);

    AlertThreshold th;
    ctrl_->Refresh(th, now);
    EXPECT_GE(ctrl_->Alerts().size(), 1u);
}

TEST_F(AlertsPanelCtrlTest, ManagerCanAcknowledgeAlert) {
    SeedItem(1, 1);
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO item_usage_history VALUES(1,1,86400,5)");
    }
    AlertThreshold th;
    ctrl_->Refresh(th, 200000);
    ASSERT_FALSE(ctrl_->Alerts().empty());
    int64_t alert_id = ctrl_->Alerts()[0].alert_id;

    bool ok = ctrl_->AcknowledgeAlert(alert_id, mgr_ctx_, 201000);
    EXPECT_TRUE(ok);
    EXPECT_EQ(0, ctrl_->TotalUnacknowledged());
}

TEST_F(AlertsPanelCtrlTest, AuditorCannotAcknowledgeAlert) {
    // Manually insert an alert_state
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO inventory_items VALUES(1,1,'X',NULL,NULL,0,0,NULL,NULL,NULL,1,1000,1000,NULL)");
        g->Exec("INSERT INTO alert_states VALUES(1,1,'low_stock',1000,NULL,NULL)");
    }
    ctrl_->Refresh(AlertThreshold{}, 2000);
    ASSERT_FALSE(ctrl_->Alerts().empty());
    int64_t aid = ctrl_->Alerts()[0].alert_id;

    bool ok = ctrl_->AcknowledgeAlert(aid, auditor_ctx_, 3000);
    EXPECT_FALSE(ok) << "Auditor must not acknowledge alerts";
}

TEST_F(AlertsPanelCtrlTest, TrayBadgeUpdatedAfterRefresh) {
    SeedItem(1, 1);
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO item_usage_history VALUES(1,1,86400,5)");
    }
    AlertThreshold th;
    ctrl_->Refresh(th, 200000);
    EXPECT_EQ(ctrl_->TotalUnacknowledged(), ctrl_->BadgeState().TotalBadgeCount());
}

TEST_F(AlertsPanelCtrlTest, IsDirtyAfterAcknowledge) {
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO inventory_items VALUES(1,1,'X',NULL,NULL,0,0,NULL,NULL,NULL,1,1000,1000,NULL)");
        g->Exec("INSERT INTO alert_states VALUES(1,1,'low_stock',1000,NULL,NULL)");
    }
    ctrl_->Refresh(AlertThreshold{}, 2000);
    ctrl_->ClearDirty();
    ASSERT_FALSE(ctrl_->Alerts().empty());
    ctrl_->AcknowledgeAlert(ctrl_->Alerts()[0].alert_id, mgr_ctx_, 3000);
    EXPECT_TRUE(ctrl_->IsDirty());
}
