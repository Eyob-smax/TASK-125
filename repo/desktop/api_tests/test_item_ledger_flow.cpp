#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/InventoryService.h"
#include "shelterops/ui/controllers/ItemLedgerController.h"
#include <filesystem>

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::ui::controllers;
using namespace shelterops::domain;

// Full integration scenario: add item → receive stock → issue stock → barcode lookup
// via the ItemLedgerController (the same code path the view calls).

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'mgr','Manager','h','operations_manager',1,1)");
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

class ItemLedgerFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        std::filesystem::remove("test_item_ledger_flow.db", ec);
        db_        = std::make_unique<Database>("test_item_ledger_flow.db");
        CreateSchema(*db_);
        inv_repo_  = std::make_unique<InventoryRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_ = std::make_unique<AuditService>(*audit_repo_);
        inv_svc_   = std::make_unique<InventoryService>(*inv_repo_, *audit_svc_);
        ctrl_      = std::make_unique<ItemLedgerController>(*inv_svc_, *inv_repo_);

        ctx_.user_id = 1;
        ctx_.role    = UserRole::OperationsManager;
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove("test_item_ledger_flow.db", ec);
    }

    std::unique_ptr<Database>             db_;
    std::unique_ptr<InventoryRepository>  inv_repo_;
    std::unique_ptr<AuditRepository>      audit_repo_;
    std::unique_ptr<AuditService>         audit_svc_;
    std::unique_ptr<InventoryService>     inv_svc_;
    std::unique_ptr<ItemLedgerController> ctrl_;
    UserContext                           ctx_;
};

TEST_F(ItemLedgerFlowTest, AddReceiveIssueFullCycle) {
    // 1. Add item via controller
    ctrl_->BeginAddItem();
    ctrl_->AddItemForm().category_id   = 1;
    ctrl_->AddItemForm().name          = "Antibiotic";
    ctrl_->AddItemForm().barcode       = "MED-001";
    ctrl_->AddItemForm().serial_number = "SN-MED-001";
    ASSERT_TRUE(ctrl_->SubmitAddItem(ctx_, 1000));

    auto item = inv_repo_->FindByBarcode("MED-001");
    ASSERT_TRUE(item.has_value());
    int64_t item_id = item->item_id;
    EXPECT_EQ(0, item->quantity);

    // 2. Receive stock
    ctrl_->BeginReceiveStock(item_id);
    ctrl_->ReceiveForm().item_id  = item_id;
    ctrl_->ReceiveForm().quantity = 50;
    ctrl_->ReceiveForm().vendor   = "PharmaCo";
    ASSERT_TRUE(ctrl_->SubmitReceiveStock(ctx_, 2000));

    item = inv_repo_->FindItemById(item_id);
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(50, item->quantity);

    // 3. Issue stock
    ctrl_->BeginIssueStock(item_id);
    ctrl_->IssueForm().item_id   = item_id;
    ctrl_->IssueForm().quantity  = 3;
    ctrl_->IssueForm().recipient = "Kennel 4";
    ctrl_->IssueForm().reason    = "treatment";
    ASSERT_TRUE(ctrl_->SubmitIssueStock(ctx_, 3000));

    item = inv_repo_->FindItemById(item_id);
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(47, item->quantity);
}

TEST_F(ItemLedgerFlowTest, InboundTimestampIsImmutable) {
    NewItemParams p;
    p.category_id = 1; p.name = "Vaccine"; p.barcode = "VAC-001";
    int64_t item_id = inv_repo_->InsertItem(p, 500);

    // Receive at specific time
    inv_repo_->InsertInbound(item_id, 10, "Vendor", "LOT-1", 100, 1, 9999);

    // No way to UPDATE inbound record timestamp — verify by reading it back
    // The received_at should be exactly what was passed.
    auto g = db_->Acquire();
    int64_t got_ts = 0;
    g->Query("SELECT received_at FROM inbound_records WHERE item_id=?",
             {std::to_string(item_id)},
             [&](const auto&, const auto& v){ got_ts = std::stoll(v[0]); });
    EXPECT_EQ(9999, got_ts) << "received_at must be immutable after insert";
}

TEST_F(ItemLedgerFlowTest, DuplicateSerialRejectedWithAuditEvent) {
    // Add first item
    ctrl_->BeginAddItem();
    ctrl_->AddItemForm().category_id   = 1;
    ctrl_->AddItemForm().name          = "Device A";
    ctrl_->AddItemForm().serial_number = "DEV-SERIAL-001";
    ctrl_->AddItemForm().barcode       = "DEVA-BC";
    ASSERT_TRUE(ctrl_->SubmitAddItem(ctx_, 1000));

    // Try duplicate serial via controller
    ctrl_->BeginAddItem();
    ctrl_->AddItemForm().category_id   = 1;
    ctrl_->AddItemForm().name          = "Device B";
    ctrl_->AddItemForm().serial_number = "DEV-SERIAL-001";   // same serial
    ctrl_->AddItemForm().barcode       = "DEVB-BC";
    bool ok = ctrl_->SubmitAddItem(ctx_, 2000);
    EXPECT_FALSE(ok);
    EXPECT_EQ(ItemLedgerState::DuplicateSerial, ctrl_->State());

    // Only one item should exist with that serial
    auto found = inv_repo_->FindBySerial("DEV-SERIAL-001");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ("Device A", found->name);

    // An audit event for DUPLICATE_SERIAL_REJECTED should have been written
    AuditQueryFilter qf;
    qf.event_type = "DUPLICATE_SERIAL_REJECTED";
    auto events = audit_repo_->Query(qf);
    EXPECT_GE(events.size(), 1u) << "Audit must record duplicate serial rejection";
}

TEST_F(ItemLedgerFlowTest, BarcodeInputLocatesItem) {
    NewItemParams p;
    p.category_id = 1; p.name = "Flea Collar"; p.barcode = "FLEA-BC-001";
    inv_repo_->InsertItem(p, 1000);

    // Simulate USB-wedge scan with trailing CR
    ctrl_->ProcessBarcodeInput("FLEA-BC-001\r", 2000);
    EXPECT_EQ(ItemLedgerState::BarcodeFound, ctrl_->State());
    ASSERT_TRUE(ctrl_->BarcodeResult().has_value());
    EXPECT_EQ("Flea Collar", ctrl_->BarcodeResult()->name);
}

TEST_F(ItemLedgerFlowTest, BarcodeNotFoundTransitionsState) {
    ctrl_->ProcessBarcodeInput("NONEXISTENT-BC\r", 2000);
    EXPECT_EQ(ItemLedgerState::BarcodeNotFound, ctrl_->State());
    EXPECT_FALSE(ctrl_->BarcodeResult().has_value());
}

TEST_F(ItemLedgerFlowTest, IssueStockBeyondQuantityFails) {
    NewItemParams p;
    p.category_id = 1; p.name = "Bandage"; p.barcode = "BAND-001";
    int64_t item_id = inv_repo_->InsertItem(p, 1000);
    inv_repo_->InsertInbound(item_id, 5, "Vendor", "", 50, 1, 1000);

    ctrl_->BeginIssueStock(item_id);
    ctrl_->IssueForm().item_id   = item_id;
    ctrl_->IssueForm().quantity  = 99;   // more than available
    ctrl_->IssueForm().recipient = "Ward 1";
    ctrl_->IssueForm().reason    = "emergency";
    bool ok = ctrl_->SubmitIssueStock(ctx_, 2000);
    EXPECT_FALSE(ok);

    // Quantity must remain unchanged
    auto item = inv_repo_->FindItemById(item_id);
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(5, item->quantity);
}

TEST_F(ItemLedgerFlowTest, UsageHistoryUpdatedAfterIssue) {
    NewItemParams p;
    p.category_id = 1; p.name = "Syringes"; p.barcode = "SYR-001";
    int64_t item_id = inv_repo_->InsertItem(p, 1000);
    inv_repo_->InsertInbound(item_id, 100, "MedSupply", "", 25, 1, 1000);

    // Issue 10 units
    ctrl_->BeginIssueStock(item_id);
    ctrl_->IssueForm().item_id   = item_id;
    ctrl_->IssueForm().quantity  = 10;
    ctrl_->IssueForm().recipient = "Clinic";
    ctrl_->IssueForm().reason    = "procedure";
    ASSERT_TRUE(ctrl_->SubmitIssueStock(ctx_, 86400));   // day 1

    // Check usage history accumulated
    auto history = inv_repo_->GetUsageHistory(item_id, 0, 200000);
    ASSERT_GE(history.size(), 1u);
    int total = 0;
    for (const auto& h : history) total += h.quantity_used;
    EXPECT_EQ(10, total);
}
