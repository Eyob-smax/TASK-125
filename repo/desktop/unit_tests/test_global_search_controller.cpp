#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/ui/controllers/GlobalSearchController.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::ui::controllers;
using namespace shelterops::domain;
using namespace shelterops::shell;

static void CreateFullSchema(Database& db) {
    auto g = db.Acquire();
    // Zones + kennels
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL DEFAULT '', row_label TEXT, "
            "x_coord_ft REAL NOT NULL DEFAULT 0, y_coord_ft REAL NOT NULL DEFAULT 0, "
            "description TEXT, is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE zone_distance_cache(from_zone_id INTEGER NOT NULL, "
            "to_zone_id INTEGER NOT NULL, distance_ft REAL NOT NULL, "
            "PRIMARY KEY(from_zone_id, to_zone_id))");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'empty', nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("CREATE TABLE kennel_restrictions(restriction_id INTEGER PRIMARY KEY, "
            "kennel_id INTEGER NOT NULL, restriction_type TEXT NOT NULL, notes TEXT, "
            "UNIQUE(kennel_id, restriction_type))");
        // Bookings
        g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER NOT NULL, "
            "animal_id INTEGER, guest_name TEXT, guest_phone_enc TEXT, guest_email_enc TEXT, "
            "check_in_at INTEGER NOT NULL, check_out_at INTEGER NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'pending', nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "total_price_cents INTEGER NOT NULL DEFAULT 0, special_requirements TEXT, "
            "created_by INTEGER, created_at INTEGER NOT NULL, "
            "approved_by INTEGER, approved_at INTEGER, notes TEXT)");
        g->Exec("CREATE TABLE booking_approvals(approval_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER NOT NULL, requested_by INTEGER NOT NULL, "
            "requested_at INTEGER NOT NULL, approver_id INTEGER, "
            "decision TEXT, decided_at INTEGER, notes TEXT)");
        g->Exec("CREATE TABLE boarding_fees(fee_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER NOT NULL, amount_cents INTEGER NOT NULL, "
            "due_at INTEGER NOT NULL, paid_at INTEGER, payment_method TEXT, created_at INTEGER NOT NULL)");
    // Inventory
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
    g->Exec("CREATE TABLE outbound_records(record_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, quantity INTEGER NOT NULL, "
            "issued_at INTEGER NOT NULL, issued_by INTEGER NOT NULL, "
            "recipient TEXT, reason TEXT NOT NULL, booking_id INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE alert_states(alert_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, alert_type TEXT NOT NULL, "
            "triggered_at INTEGER NOT NULL, acknowledged_at INTEGER, acknowledged_by INTEGER, "
            "UNIQUE(item_id,alert_type,triggered_at))");
        // Reports
        g->Exec("CREATE TABLE report_definitions(report_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, report_type TEXT NOT NULL, description TEXT, "
            "filter_json TEXT NOT NULL DEFAULT '{}', schedule_cron TEXT, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_by INTEGER, created_at INTEGER NOT NULL)");
        g->Exec("CREATE TABLE report_runs(run_id INTEGER PRIMARY KEY, "
            "report_id INTEGER NOT NULL, version_label TEXT NOT NULL, triggered_by INTEGER, "
            "trigger_type TEXT NOT NULL, started_at INTEGER NOT NULL, completed_at INTEGER, "
            "status TEXT NOT NULL DEFAULT 'running', filter_json TEXT NOT NULL DEFAULT '{}', "
            "output_path TEXT, error_message TEXT, anomaly_flags_json TEXT, row_count INTEGER)");
        g->Exec("CREATE TABLE report_snapshots(snapshot_id INTEGER PRIMARY KEY, "
            "run_id INTEGER NOT NULL, metric_name TEXT NOT NULL, "
            "metric_value REAL NOT NULL, dimension_json TEXT NOT NULL DEFAULT '{}', "
            "captured_at INTEGER NOT NULL)");
        g->Exec("CREATE TABLE export_jobs(job_id INTEGER PRIMARY KEY, "
            "report_run_id INTEGER NOT NULL, format TEXT NOT NULL, "
            "requested_by INTEGER NOT NULL, queued_at INTEGER NOT NULL, started_at INTEGER, "
            "completed_at INTEGER, output_path TEXT, status TEXT NOT NULL DEFAULT 'queued', "
            "max_concurrency INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE watermark_rules(rule_id INTEGER PRIMARY KEY, "
            "report_type TEXT NOT NULL, metric_name TEXT NOT NULL, "
            "lower_bound REAL, upper_bound REAL, is_active INTEGER NOT NULL DEFAULT 1)");
    // Audit
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
}

class GlobalSearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_        = std::make_unique<Database>(":memory:");
        CreateFullSchema(*db_);
        kennel_repo_  = std::make_unique<KennelRepository>(*db_);
        booking_repo_ = std::make_unique<BookingRepository>(*db_);
        inv_repo_     = std::make_unique<InventoryRepository>(*db_);
        report_repo_  = std::make_unique<ReportRepository>(*db_);
        audit_repo_   = std::make_unique<AuditRepository>(*db_);
        ctrl_ = std::make_unique<GlobalSearchController>(
            *kennel_repo_, *booking_repo_, *inv_repo_, *report_repo_, *audit_repo_);
    }
    std::unique_ptr<Database>              db_;
    std::unique_ptr<KennelRepository>      kennel_repo_;
    std::unique_ptr<BookingRepository>     booking_repo_;
    std::unique_ptr<InventoryRepository>   inv_repo_;
    std::unique_ptr<ReportRepository>      report_repo_;
    std::unique_ptr<AuditRepository>       audit_repo_;
    std::unique_ptr<GlobalSearchController> ctrl_;
};

TEST_F(GlobalSearchTest, EmptyQueryReturnsNoResults) {
    auto res = ctrl_->Search("", UserRole::OperationsManager, 1000);
    EXPECT_TRUE(res.items.empty());
}

TEST_F(GlobalSearchTest, KennelNameMatchReturnsResult) {
    int64_t z = kennel_repo_->InsertZone("Alpha Wing", "Main", "A", 0, 0);
    kennel_repo_->InsertKennel(z, "Suite-7", 1, 4000);

    auto res = ctrl_->Search("suite", UserRole::OperationsManager, 1000);
    ASSERT_FALSE(res.items.empty());
    bool found = false;
    for (const auto& item : res.items) {
        if (item.category == SearchCategory::Kennel &&
            item.display_text.find("Suite-7") != std::string::npos) {
            found = true;
            EXPECT_EQ(WindowId::KennelBoard, item.target_window);
        }
    }
    EXPECT_TRUE(found) << "Expected kennel 'Suite-7' in search results";
}

TEST_F(GlobalSearchTest, KennelNonMatchExcluded) {
    int64_t z = kennel_repo_->InsertZone("Wing B", "B", "", 0, 0);
    kennel_repo_->InsertKennel(z, "Room-3", 1, 2000);

    auto res = ctrl_->Search("zzznomatch", UserRole::OperationsManager, 1000);
    EXPECT_TRUE(res.items.empty());
}

TEST_F(GlobalSearchTest, BookingNameMatchUsesBookingRepository) {
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO bookings(booking_id,kennel_id,animal_id,guest_name,guest_phone_enc,guest_email_enc, "
                "check_in_at,check_out_at,status,nightly_price_cents,total_price_cents,special_requirements, "
                "created_by,created_at,approved_by,approved_at,notes) "
                "VALUES(42,1,NULL,'Fluffy Owner','x','y',10,20,'completed',100,200,'none',1,500,NULL,NULL,'note')");
    }

    auto res = ctrl_->Search("Fluffy", UserRole::OperationsManager, 1000);
    bool found = false;
    for (const auto& item : res.items) {
        if (item.category == SearchCategory::Booking && item.record_id == 42) {
            found = true;
            EXPECT_EQ(WindowId::KennelBoard, item.target_window);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(GlobalSearchTest, AuditorBookingResultIsMasked) {
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO bookings(booking_id,kennel_id,animal_id,guest_name,guest_phone_enc,guest_email_enc, "
                "check_in_at,check_out_at,status,nightly_price_cents,total_price_cents,special_requirements, "
                "created_by,created_at,approved_by,approved_at,notes) "
                "VALUES(10,1,NULL,'John Smith','x','y',10,20,'completed',100,200,'none',1,500,NULL,NULL,'note')");
    }

    auto res = ctrl_->Search("John", UserRole::Auditor, 1000);
    for (const auto& item : res.items) {
        if (item.category == SearchCategory::Booking) {
            EXPECT_TRUE(item.is_masked)
                << "Auditor role must receive masked booking results";
            // Masked display must not contain the full guest name
            EXPECT_EQ(std::string::npos, item.display_text.find("John Smith"))
                << "Full name must not appear in masked result";
        }
    }
}

TEST_F(GlobalSearchTest, InventoryLowStockMatchReturnsResult) {
    // Search should work across all active inventory rows, not only low-stock subsets.
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO inventory_items VALUES(1,1,'Dog Food','','Shelf-A',25,100,0,NULL,'BC01',1,1000,1000,NULL)");
    }

    auto res = ctrl_->Search("Dog Food", UserRole::OperationsManager, 1000);
    bool found = false;
    for (const auto& item : res.items) {
        if (item.category == SearchCategory::Inventory &&
            item.display_text.find("Dog Food") != std::string::npos) {
            found = true;
            EXPECT_EQ(WindowId::ItemLedger, item.target_window);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(GlobalSearchTest, ReportAuditEventMatch) {
    AuditEvent ev;
    ev.occurred_at = 600;
    ev.event_type  = "REPORT_COMPLETED";
    ev.entity_type = "report";
    ev.entity_id   = 5;
    ev.description = "Occupancy report run completed";
    audit_repo_->Append(ev);

    auto res = ctrl_->Search("occupancy", UserRole::OperationsManager, 1000);
    bool found = false;
    for (const auto& item : res.items) {
        if (item.category == SearchCategory::Report && item.record_id == 5) {
            found = true;
            EXPECT_EQ(WindowId::ReportsStudio, item.target_window);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(GlobalSearchTest, SearchResultsContainSearchedAtTimestamp) {
    auto res = ctrl_->Search("anything", UserRole::OperationsManager, 9999);
    EXPECT_EQ(9999, res.searched_at);
    EXPECT_TRUE(res.completed);
}

TEST_F(GlobalSearchTest, CaseInsensitiveKennelSearch) {
    int64_t z = kennel_repo_->InsertZone("South Wing", "S", "", 0, 0);
    kennel_repo_->InsertKennel(z, "Kennel-VIP", 2, 5000);

    auto lower = ctrl_->Search("kennel-vip", UserRole::OperationsManager, 1000);
    auto upper = ctrl_->Search("KENNEL-VIP", UserRole::OperationsManager, 1000);

    auto count_kennels = [](const SearchResults& r) {
        int n = 0;
        for (const auto& i : r.items) if (i.category == SearchCategory::Kennel) ++n;
        return n;
    };
    EXPECT_EQ(count_kennels(lower), count_kennels(upper));
    EXPECT_GT(count_kennels(lower), 0);
}
