#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/InventoryRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
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

class InvRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_ = std::make_unique<InventoryRepository>(*db_);
    }
    std::unique_ptr<Database>              db_;
    std::unique_ptr<InventoryRepository>   repo_;
};

TEST_F(InvRepoTest, InsertAndFindById) {
    NewItemParams p;
    p.category_id = 1; p.name = "Dog Food";
    p.barcode = "BC001"; p.serial_number = "";
    p.unit_cost_cents = 500;

    int64_t id = repo_->InsertItem(p, 1000);
    EXPECT_GT(id, 0);
    auto item = repo_->FindItemById(id);
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ("Dog Food", item->name);
    EXPECT_EQ(0, item->quantity);  // new items start at zero; stock added via InsertInbound
}

TEST_F(InvRepoTest, FindByBarcode) {
    NewItemParams p;
    p.category_id = 1; p.name = "Cat Food"; p.barcode = "BC002";
    p.serial_number = ""; p.unit_cost_cents = 0;
    int64_t id = repo_->InsertItem(p, 1000);

    auto item = repo_->FindByBarcode("BC002");
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(id, item->item_id);
}

TEST_F(InvRepoTest, FindBySerial) {
    NewItemParams p;
    p.category_id = 1; p.name = "Crate"; p.serial_number = "SN-9999";
    p.barcode = ""; p.unit_cost_cents = 0;
    int64_t id = repo_->InsertItem(p, 1000);

    auto item = repo_->FindBySerial("SN-9999");
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(id, item->item_id);
}

TEST_F(InvRepoTest, DecrementQuantityBelowZeroThrows) {
    NewItemParams p;
    p.category_id = 1; p.name = "Vaccine";
    p.serial_number = ""; p.barcode = ""; p.unit_cost_cents = 0;
    int64_t id = repo_->InsertItem(p, 1000);
    repo_->InsertInbound(id, 2, "VendorA", "LOT-V", 0, 1, 1000);

    EXPECT_THROW(repo_->DecrementQuantity(id, 5), std::runtime_error);
}

TEST_F(InvRepoTest, InsertInboundIncreasesQuantity) {
    NewItemParams p;
    p.category_id = 1; p.name = "Leash";
    p.serial_number = ""; p.barcode = ""; p.unit_cost_cents = 0;
    int64_t id = repo_->InsertItem(p, 1000);

    repo_->InsertInbound(id, 10, "Vendor", "LOT1", 0, 1, 1000);
    auto item = repo_->FindItemById(id);
    EXPECT_EQ(10, item->quantity);
}

TEST_F(InvRepoTest, UsageHistoryUpsertAggregatesPerDate) {
    NewItemParams p;
    p.category_id = 1; p.name = "Shampoo";
    p.serial_number = ""; p.barcode = ""; p.unit_cost_cents = 0;
    int64_t id = repo_->InsertItem(p, 1000);

    int64_t day = 86400;
    repo_->UpsertUsageHistory(id, day, 3);
    repo_->UpsertUsageHistory(id, day, 4);

    bool found = false;
    {
        auto g = db_->Acquire();
        g->Query("SELECT quantity_used FROM item_usage_history WHERE item_id=? AND period_date=?",
                 {std::to_string(id), std::to_string(day)},
                 [&found](const auto&, const auto& vals) {
                     found = (vals[0] == "7");
                 });
    }
    EXPECT_TRUE(found);
}

TEST_F(InvRepoTest, InsertOutboundDoesNotModifyQuantity) {
    NewItemParams p;
    p.category_id = 1; p.name = "Brush";
    p.serial_number = ""; p.barcode = ""; p.unit_cost_cents = 0;
    int64_t id = repo_->InsertItem(p, 1000);
    repo_->InsertInbound(id, 10, "Supplier", "LOT-B", 0, 1, 1000);

    repo_->InsertOutbound(id, 2, "Staff", "daily", 0, 1, 2000);
    auto item = repo_->FindItemById(id);
    EXPECT_EQ(10, item->quantity); // quantity unchanged by InsertOutbound alone
}
