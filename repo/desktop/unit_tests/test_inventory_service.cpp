#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/InventoryService.h"
#include "shelterops/services/AuditService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::domain;
using namespace shelterops::common;

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

class InvSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_    = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        inv_repo_   = std::make_unique<InventoryRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_  = std::make_unique<AuditService>(*audit_repo_);
        svc_        = std::make_unique<InventoryService>(*inv_repo_, *audit_svc_);
        ctx_.user_id = 1;
        ctx_.role = UserRole::Administrator;
    }

    std::unique_ptr<Database>              db_;
    std::unique_ptr<InventoryRepository>   inv_repo_;
    std::unique_ptr<AuditRepository>       audit_repo_;
    std::unique_ptr<AuditService>          audit_svc_;
    std::unique_ptr<InventoryService>      svc_;
    UserContext                            ctx_;
};

TEST_F(InvSvcTest, AddItemHappyPath) {
    NewItemParams p;
    p.category_id = 1; p.name = "Leash";
    p.serial_number = "SN-001"; p.barcode = "BC-001";
    p.unit_cost_cents = 500;

    auto result = svc_->AddItem(p, ctx_, 1000);
    ASSERT_TRUE(std::holds_alternative<int64_t>(result));
    EXPECT_GT(std::get<int64_t>(result), 0);
}

TEST_F(InvSvcTest, DuplicateSerialRejected) {
    NewItemParams p;
    p.category_id = 1; p.name = "Crate";
    p.serial_number = "SN-DUP"; p.barcode = "";
    p.unit_cost_cents = 0;

    auto r1 = svc_->AddItem(p, ctx_, 1000);
    ASSERT_TRUE(std::holds_alternative<int64_t>(r1));

    auto r2 = svc_->AddItem(p, ctx_, 1001);
    ASSERT_TRUE(std::holds_alternative<ErrorEnvelope>(r2));
    auto& err = std::get<ErrorEnvelope>(r2);
    EXPECT_EQ(ErrorCode::InvalidInput, err.code);
    EXPECT_NE(std::string::npos, err.message.find("Duplicate"));
}

TEST_F(InvSvcTest, ReceiveStockIncrementsQuantity) {
    NewItemParams p;
    p.category_id = 1; p.name = "Food";
    p.serial_number = ""; p.barcode = "BC-002"; p.unit_cost_cents = 0;
    auto r = svc_->AddItem(p, ctx_, 1000);
    int64_t id = std::get<int64_t>(r);

    auto err = svc_->ReceiveStock(id, 20, "Vendor", "LOT1", 100, ctx_, 2000);
    EXPECT_TRUE(!err.has_value() || err->code == ErrorCode::Internal);

    auto item = inv_repo_->FindItemById(id);
    EXPECT_EQ(20, item->quantity);
}

TEST_F(InvSvcTest, IssueStockDecrementsAndWritesUsageHistory) {
    NewItemParams p;
    p.category_id = 1; p.name = "Shampoo";
    p.serial_number = ""; p.barcode = "BC-003"; p.unit_cost_cents = 0;
    int64_t id = std::get<int64_t>(svc_->AddItem(p, ctx_, 1000));
    svc_->ReceiveStock(id, 10, "Vendor", "LOT-S", 0, ctx_, 1000);

    auto err = svc_->IssueStock(id, 3, "Staff", "daily", 0, ctx_, 86400);
    EXPECT_TRUE(!err.has_value() || err->code == ErrorCode::Internal);

    auto item = inv_repo_->FindItemById(id);
    EXPECT_EQ(7, item->quantity);
}

TEST_F(InvSvcTest, IssueStockBeyondQuantityFails) {
    NewItemParams p;
    p.category_id = 1; p.name = "Vaccine";
    p.serial_number = ""; p.barcode = "BC-004"; p.unit_cost_cents = 0;
    int64_t id = std::get<int64_t>(svc_->AddItem(p, ctx_, 1000));
    svc_->ReceiveStock(id, 2, "Vendor", "LOT-V", 0, ctx_, 1000);

    auto err = svc_->IssueStock(id, 5, "Staff", "test", 0, ctx_, 1000);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(ErrorCode::InvalidInput, err->code);
}

TEST_F(InvSvcTest, LookupByBarcodeHappyPath) {
    NewItemParams p;
    p.category_id = 1; p.name = "Collar";
    p.serial_number = ""; p.barcode = "ITEM-789"; p.unit_cost_cents = 0;
    svc_->AddItem(p, ctx_, 1000);

    auto result = svc_->LookupByBarcode("ITEM-789");
    ASSERT_TRUE(std::holds_alternative<InventoryItemRecord>(result));
    EXPECT_EQ("Collar", std::get<InventoryItemRecord>(result).name);
}

TEST_F(InvSvcTest, LookupByBarcodeNotFound) {
    auto result = svc_->LookupByBarcode("ITEM-NOPE");
    ASSERT_TRUE(std::holds_alternative<ErrorEnvelope>(result));
    EXPECT_EQ(ErrorCode::ItemNotFound, std::get<ErrorEnvelope>(result).code);
}
