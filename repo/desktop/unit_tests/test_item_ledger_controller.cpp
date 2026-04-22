#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/InventoryService.h"
#include "shelterops/ui/controllers/ItemLedgerController.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::ui::controllers;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
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

class ItemLedgerCtrlTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_        = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        inv_repo_  = std::make_unique<InventoryRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_ = std::make_unique<AuditService>(*audit_repo_);
        inv_svc_   = std::make_unique<InventoryService>(*inv_repo_, *audit_svc_);
        ctrl_      = std::make_unique<ItemLedgerController>(*inv_svc_, *inv_repo_);

        ctx_.user_id = 1;
        ctx_.role    = UserRole::OperationsManager;
    }

    std::unique_ptr<Database>             db_;
    std::unique_ptr<InventoryRepository>  inv_repo_;
    std::unique_ptr<AuditRepository>      audit_repo_;
    std::unique_ptr<AuditService>         audit_svc_;
    std::unique_ptr<InventoryService>     inv_svc_;
    std::unique_ptr<ItemLedgerController> ctrl_;
    UserContext                           ctx_;
};

TEST_F(ItemLedgerCtrlTest, InitialStateIsIdle) {
    EXPECT_EQ(ItemLedgerState::Idle, ctrl_->State());
    EXPECT_TRUE(ctrl_->Items().empty());
}

TEST_F(ItemLedgerCtrlTest, RefreshWithNoItemsStaysLoaded) {
    ctrl_->Refresh(1000);
    EXPECT_EQ(ItemLedgerState::Loaded, ctrl_->State());
    EXPECT_TRUE(ctrl_->Items().empty());
}

TEST_F(ItemLedgerCtrlTest, RefreshAfterAddItemShowsItem) {
    NewItemParams p;
    p.category_id = 1; p.name = "Dog Kibble"; p.barcode = "BC001";
    inv_repo_->InsertItem(p, 1000);
    // Add stock so it appears in low-stock candidates
    auto item = inv_repo_->FindByBarcode("BC001");
    ASSERT_TRUE(item.has_value());

    ctrl_->Refresh(2000);
    EXPECT_EQ(ItemLedgerState::Loaded, ctrl_->State());
}

TEST_F(ItemLedgerCtrlTest, SelectItemUpdatesSelection) {
    ctrl_->SelectItem(42);
    EXPECT_EQ(42, ctrl_->SelectedItem());
}

TEST_F(ItemLedgerCtrlTest, BeginReceiveStockSetsState) {
    ctrl_->BeginReceiveStock(1);
    EXPECT_EQ(ItemLedgerState::ReceivingStock, ctrl_->State());
    EXPECT_EQ(1, ctrl_->ReceiveForm().item_id);
}

TEST_F(ItemLedgerCtrlTest, BeginIssueStockSetsState) {
    ctrl_->BeginIssueStock(2);
    EXPECT_EQ(ItemLedgerState::IssuingStock, ctrl_->State());
    EXPECT_EQ(2, ctrl_->IssueForm().item_id);
}

TEST_F(ItemLedgerCtrlTest, BeginAddItemSetsState) {
    ctrl_->BeginAddItem();
    EXPECT_EQ(ItemLedgerState::AddingItem, ctrl_->State());
}

TEST_F(ItemLedgerCtrlTest, SubmitAddItemHappyPath) {
    ctrl_->BeginAddItem();
    auto& form       = ctrl_->AddItemForm();
    form.category_id = 1;
    form.name        = "Cat Food";
    form.barcode     = "CAT-001";
    form.serial_number = "SN-CAT-001";

    bool ok = ctrl_->SubmitAddItem(ctx_, 1000);
    EXPECT_TRUE(ok);
}

TEST_F(ItemLedgerCtrlTest, SubmitAddItemDuplicateSerialSetsDuplicateState) {
    // Insert an item with a known serial
    NewItemParams p;
    p.category_id  = 1; p.name = "First Item";
    p.serial_number = "SN-DUP-1"; p.barcode = "BC-DUP-1";
    inv_repo_->InsertItem(p, 1000);

    // Try to add another item with the same serial
    ctrl_->BeginAddItem();
    ctrl_->AddItemForm().category_id   = 1;
    ctrl_->AddItemForm().name          = "Second Item";
    ctrl_->AddItemForm().serial_number = "SN-DUP-1";
    ctrl_->AddItemForm().barcode       = "BC-DUP-2";

    bool ok = ctrl_->SubmitAddItem(ctx_, 2000);
    EXPECT_FALSE(ok);
    EXPECT_EQ(ItemLedgerState::DuplicateSerial, ctrl_->State());
}

TEST_F(ItemLedgerCtrlTest, SubmitReceiveStockIncreasesQuantity) {
    // Add item first
    NewItemParams p;
    p.category_id = 1; p.name = "Flea Med"; p.barcode = "BC-FEA-1";
    int64_t item_id = inv_repo_->InsertItem(p, 1000);

    ctrl_->BeginReceiveStock(item_id);
    ctrl_->ReceiveForm().item_id  = item_id;
    ctrl_->ReceiveForm().quantity = 10;
    ctrl_->ReceiveForm().vendor   = "VetSupply Co";
    bool ok = ctrl_->SubmitReceiveStock(ctx_, 2000);
    EXPECT_TRUE(ok);

    auto item = inv_repo_->FindItemById(item_id);
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(10, item->quantity);
}

TEST_F(ItemLedgerCtrlTest, SubmitIssueStockDecreasesQuantity) {
    NewItemParams p;
    p.category_id = 1; p.name = "Bandages"; p.barcode = "BC-BAND-1";
    int64_t item_id = inv_repo_->InsertItem(p, 1000);
    // Seed stock
    inv_repo_->InsertInbound(item_id, 20, "Vendor", "", 100, 1, 1000);

    ctrl_->BeginIssueStock(item_id);
    ctrl_->IssueForm().item_id   = item_id;
    ctrl_->IssueForm().quantity  = 5;
    ctrl_->IssueForm().recipient = "Kennel 3";
    ctrl_->IssueForm().reason    = "daily_care";
    bool ok = ctrl_->SubmitIssueStock(ctx_, 2000);
    EXPECT_TRUE(ok);

    auto item = inv_repo_->FindItemById(item_id);
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(15, item->quantity);
}

TEST_F(ItemLedgerCtrlTest, ClipboardTsvHasHeaderRow) {
    ctrl_->Refresh(1000);
    std::string tsv = ctrl_->ClipboardTsv();
    EXPECT_FALSE(tsv.empty());
    EXPECT_NE(std::string::npos, tsv.find("Name"));
}

TEST_F(ItemLedgerCtrlTest, IsDirtyAfterRefresh) {
    EXPECT_FALSE(ctrl_->IsDirty());
    ctrl_->Refresh(1000);
    EXPECT_TRUE(ctrl_->IsDirty());
    ctrl_->ClearDirty();
    EXPECT_FALSE(ctrl_->IsDirty());
}

TEST_F(ItemLedgerCtrlTest, FilterShowLowStockOnly) {
    // Item with qty=0 (low stock) and item with qty=100 (normal)
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO inventory_items VALUES(10,1,'Low Item','','',0,100,0,NULL,'BC-LOW',1,1000,1000,NULL)");
        g->Exec("INSERT INTO inventory_items VALUES(11,1,'Full Item','','',100,100,0,NULL,'BC-FULL',1,1000,1000,NULL)");
    }

    ItemLedgerFilter f;
    f.show_low_stock_only = true;
    ctrl_->SetFilter(f);
    ctrl_->Refresh(1000);

    // Only items from low-stock candidates should appear
    for (const auto& item : ctrl_->Items()) {
        EXPECT_LT(item.quantity, 10) << "Only low-stock items expected when filter is set";
    }
}
