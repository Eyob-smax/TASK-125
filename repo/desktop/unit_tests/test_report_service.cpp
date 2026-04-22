#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/ReportService.h"
#include "shelterops/services/AuditService.h"
#include <filesystem>

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
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
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL, row_label TEXT, x_coord_ft REAL DEFAULT 0, "
            "y_coord_ft REAL DEFAULT 0, description TEXT, is_active INTEGER DEFAULT 1)");
    g->Exec("INSERT INTO zones VALUES(1,'Zone A','B',NULL,0,0,NULL,1)");
    g->Exec("CREATE TABLE zone_distance_cache(from_zone_id INTEGER NOT NULL, "
            "to_zone_id INTEGER NOT NULL, distance_ft REAL NOT NULL, PRIMARY KEY(from_zone_id,to_zone_id))");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'boarding', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL DEFAULT 0, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("INSERT INTO kennels VALUES(1,1,'K1',1,'boarding',3000,4.5,1,NULL)");
    g->Exec("CREATE TABLE kennel_restrictions(restriction_id INTEGER PRIMARY KEY, "
            "kennel_id INTEGER NOT NULL, restriction_type TEXT NOT NULL, notes TEXT, "
            "UNIQUE(kennel_id,restriction_type))");
    g->Exec("CREATE TABLE animals(animal_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "species TEXT NOT NULL, intake_at INTEGER NOT NULL, intake_type TEXT NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'intake', is_aggressive INTEGER DEFAULT 0, "
            "is_large_dog INTEGER DEFAULT 0, breed TEXT, age_years REAL, weight_lbs REAL, "
            "color TEXT, sex TEXT, microchip_id TEXT, notes TEXT, created_by INTEGER, anonymized_at INTEGER)");
    g->Exec("CREATE TABLE adoptable_listings(listing_id INTEGER PRIMARY KEY, "
            "animal_id INTEGER NOT NULL, kennel_id INTEGER, listing_date INTEGER NOT NULL, "
            "adoption_fee_cents INTEGER NOT NULL DEFAULT 0, description TEXT, "
            "rating REAL, status TEXT NOT NULL DEFAULT 'active', "
            "created_by INTEGER, adopted_at INTEGER)");
    g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER NOT NULL, "
            "animal_id INTEGER, guest_name TEXT, guest_phone_enc TEXT, guest_email_enc TEXT, "
            "check_in_at INTEGER NOT NULL, check_out_at INTEGER NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'active', nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "total_price_cents INTEGER NOT NULL DEFAULT 0, special_requirements TEXT, "
            "created_by INTEGER, created_at INTEGER NOT NULL, "
            "approved_by INTEGER, approved_at INTEGER, notes TEXT)");
    // Active booking overlapping our report window.
    g->Exec("INSERT INTO bookings(booking_id,kennel_id,check_in_at,check_out_at,status,"
            "nightly_price_cents,total_price_cents,created_at) VALUES(1,1,1000,2000,'active',3000,3000,500)");
    g->Exec("CREATE TABLE booking_approvals(approval_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER NOT NULL, requested_by INTEGER NOT NULL, "
            "requested_at INTEGER NOT NULL, approver_id INTEGER, "
            "decision TEXT, decided_at INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE boarding_fees(fee_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER NOT NULL, amount_cents INTEGER NOT NULL, "
            "due_at INTEGER NOT NULL, paid_at INTEGER, "
            "payment_method TEXT, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE recommendation_results(result_id INTEGER PRIMARY KEY, "
            "query_hash TEXT NOT NULL, kennel_id INTEGER NOT NULL, rank_position INTEGER NOT NULL, "
            "score REAL NOT NULL, reason_json TEXT NOT NULL, generated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, is_active INTEGER NOT NULL DEFAULT 1)");
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
    g->Exec("CREATE TABLE maintenance_tickets(ticket_id INTEGER PRIMARY KEY, "
            "zone_id INTEGER, kennel_id INTEGER, title TEXT NOT NULL, description TEXT, "
            "priority TEXT NOT NULL DEFAULT 'normal', status TEXT NOT NULL DEFAULT 'open', "
            "created_at INTEGER NOT NULL, created_by INTEGER, assigned_to INTEGER, "
            "first_action_at INTEGER, resolved_at INTEGER)");
    g->Exec("CREATE TABLE maintenance_events(event_id INTEGER PRIMARY KEY, "
            "ticket_id INTEGER NOT NULL, actor_id INTEGER NOT NULL, event_type TEXT NOT NULL, "
            "old_status TEXT, new_status TEXT, notes TEXT, occurred_at INTEGER NOT NULL)");
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
            "requested_by INTEGER NOT NULL, queued_at INTEGER NOT NULL, "
            "started_at INTEGER, completed_at INTEGER, output_path TEXT, "
            "status TEXT NOT NULL DEFAULT 'queued', max_concurrency INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE watermark_rules(rule_id INTEGER PRIMARY KEY, "
            "report_type TEXT NOT NULL, role TEXT NOT NULL, export_format TEXT NOT NULL, "
            "requires_watermark INTEGER NOT NULL DEFAULT 0, restrictions_json TEXT NOT NULL DEFAULT '{}', "
            "UNIQUE(report_type,role,export_format))");
    // Seed a report definition.
    g->Exec("INSERT INTO report_definitions(report_id,name,report_type,filter_json,is_active,created_by,created_at) "
            "VALUES(1,'Occupancy Report','occupancy','{}',1,1,1000)");
    g->Exec("INSERT INTO report_definitions(report_id,name,report_type,filter_json,is_active,created_by,created_at) "
            "VALUES(2,'Turnover Report','kennel_turnover','{}',1,1,1000)");
}

class ReportSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_           = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        report_repo_  = std::make_unique<ReportRepository>(*db_);
        kennel_repo_  = std::make_unique<KennelRepository>(*db_);
        booking_repo_ = std::make_unique<BookingRepository>(*db_);
        inv_repo_     = std::make_unique<InventoryRepository>(*db_);
        maint_repo_   = std::make_unique<MaintenanceRepository>(*db_);
        audit_repo_   = std::make_unique<AuditRepository>(*db_);
        audit_svc_    = std::make_unique<AuditService>(*audit_repo_);
        svc_          = std::make_unique<ReportService>(
            *report_repo_, *kennel_repo_, *booking_repo_, *inv_repo_, *maint_repo_, *audit_svc_);
        ctx_.user_id = 1; ctx_.role = UserRole::Administrator;
        std::filesystem::create_directories("exports");
    }

    std::unique_ptr<Database>               db_;
    std::unique_ptr<ReportRepository>       report_repo_;
    std::unique_ptr<KennelRepository>       kennel_repo_;
    std::unique_ptr<BookingRepository>      booking_repo_;
    std::unique_ptr<InventoryRepository>    inv_repo_;
    std::unique_ptr<MaintenanceRepository>  maint_repo_;
    std::unique_ptr<AuditRepository>        audit_repo_;
    std::unique_ptr<AuditService>           audit_svc_;
    std::unique_ptr<ReportService>          svc_;
    UserContext                             ctx_;
};

TEST_F(ReportSvcTest, RunPipelineProducesCompletedRun) {
    int64_t now = 1745712000;
    int64_t run_id = svc_->RunPipeline(1, "{}", "manual", ctx_, now);
    EXPECT_GT(run_id, 0);

    auto runs = report_repo_->ListRunsForReport(1);
    ASSERT_EQ(1u, runs.size());
    EXPECT_EQ("completed", runs[0].status);
}

TEST_F(ReportSvcTest, RunPipelineWritesSnapshots) {
    int64_t now = 1745712000;
    int64_t run_id = svc_->RunPipeline(1, "{}", "manual", ctx_, now);
    auto snaps = report_repo_->ListSnapshotsForRun(run_id);
    EXPECT_GE(snaps.size(), 1u);
}

TEST_F(ReportSvcTest, VersionLabelFormattedCorrectly) {
    // Format: <type>-YYYYMMDD-NNN
    std::string lbl = ReportService::GenerateVersionLabel(1, "occupancy", 1745712000, 2);
    // Should be "occupancy-20260427-003" (NNN is 1-indexed from 0 prior runs)
    EXPECT_EQ(0u, lbl.find("occupancy-"));
    EXPECT_NE(std::string::npos, lbl.rfind("-003"));
}

TEST_F(ReportSvcTest, ListRunsReturnsAll) {
    int64_t now = 1745712000;
    svc_->RunPipeline(1, "{}", "manual", ctx_, now);
    svc_->RunPipeline(1, "{}", "manual", ctx_, now + 1);

    auto runs = svc_->ListRuns(1);
    EXPECT_EQ(2u, runs.size());
}

TEST_F(ReportSvcTest, OccupancyFilterByZoneIdsApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO zones VALUES(2,'Zone B','B',NULL,10,0,NULL,1)");
        g->Exec("INSERT INTO kennels VALUES(2,2,'K2',1,'boarding',3000,4.0,1,NULL)");
        g->Exec("INSERT INTO bookings VALUES(100,1,0,'A','','',1000,4000,'approved',100,100,'',1,1000,0,0,'')");
        g->Exec("INSERT INTO bookings VALUES(101,2,0,'B','','',1000,4000,'approved',100,100,'',1,1000,0,0,'')");
        }

        const std::string filter = R"({"date_from_unix":1000,"date_to_unix":4000,"zone_ids":[1]})";
        int64_t run_id = svc_->RunPipeline(1, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_total = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "total_kennels") {
                        saw_total = true;
                        EXPECT_EQ(1.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_total);
}

TEST_F(ReportSvcTest, OccupancyFilterByStaffOwnerApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO bookings VALUES(10,1,0,'Owner1','','',1000,4000,'approved',100,100,'',11,1000,0,0,'')");
        g->Exec("INSERT INTO bookings VALUES(11,1,0,'Owner2','','',1000,4000,'approved',100,100,'',22,1000,0,0,'')");
        }

        const std::string filter = R"({"date_from_unix":1000,"date_to_unix":4000,"staff_owner_id":11})";
        int64_t run_id = svc_->RunPipeline(1, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_occupied = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "occupied_kennels") {
                        saw_occupied = true;
                        EXPECT_EQ(1.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_occupied);
}

TEST_F(ReportSvcTest, OccupancyFilterByPetTypeApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO bookings VALUES(20,1,0,'DogOwner','','',1000,4000,'approved',100,100,'dog',11,1000,0,0,'')");
        g->Exec("INSERT INTO bookings VALUES(21,1,0,'CatOwner','','',1000,4000,'approved',100,100,'cat',22,1000,0,0,'')");
        }

        const std::string filter = R"({"date_from_unix":1000,"date_to_unix":4000,"pet_type":"cat"})";
        int64_t run_id = svc_->RunPipeline(1, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_occupied = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "occupied_kennels") {
                        saw_occupied = true;
                        EXPECT_EQ(1.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_occupied);
}

TEST_F(ReportSvcTest, TurnoverFilterByPetTypeApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO bookings VALUES(30,1,0,'DogOwner','','',1000,1500,'completed',100,100,'dog',11,1000,0,0,'')");
        g->Exec("INSERT INTO bookings VALUES(31,1,0,'CatOwner','','',1000,1500,'completed',100,100,'cat',22,1000,0,0,'')");
        }

        const std::string filter = R"({"date_from_unix":900,"date_to_unix":4000,"pet_type":"cat"})";
        int64_t run_id = svc_->RunPipeline(2, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_completed = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "completed_stays") {
                        saw_completed = true;
                        EXPECT_EQ(1.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_completed);
}

TEST_F(ReportSvcTest, MaintenanceResponseFilterByZoneApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO report_definitions(report_id,name,report_type,filter_json,is_active,created_by,created_at) "
                "VALUES(3,'Maint Report','maintenance_response','{}',1,1,1000)");
        g->Exec("INSERT INTO zones VALUES(2,'Zone B','B',NULL,10,0,NULL,1)");
        // ticket in zone 1, ticket in zone 2 — both within time range
        g->Exec("INSERT INTO maintenance_tickets VALUES(1,1,0,'Fix fence',NULL,'normal','open',500,0,0,NULL,NULL)");
        g->Exec("INSERT INTO maintenance_tickets VALUES(2,2,0,'Broken door',NULL,'normal','open',500,0,0,NULL,NULL)");
        }

        const std::string filter = R"({"date_from_unix":0,"date_to_unix":3000,"zone_ids":[2]})";
        int64_t run_id = svc_->RunPipeline(3, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_count = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "ticket_count") {
                        saw_count = true;
                        EXPECT_EQ(1.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_count);
}

TEST_F(ReportSvcTest, MaintenanceResponseFilterByStaffApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO report_definitions(report_id,name,report_type,filter_json,is_active,created_by,created_at) "
                "VALUES(3,'Maint Report','maintenance_response','{}',1,1,1000)");
        // assigned_to=5 and assigned_to=6
        g->Exec("INSERT INTO maintenance_tickets VALUES(1,1,0,'T1',NULL,'normal','open',500,0,5,NULL,NULL)");
        g->Exec("INSERT INTO maintenance_tickets VALUES(2,1,0,'T2',NULL,'normal','open',500,0,6,NULL,NULL)");
        }

        const std::string filter = R"({"date_from_unix":0,"date_to_unix":3000,"staff_owner_id":5})";
        int64_t run_id = svc_->RunPipeline(3, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_count = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "ticket_count") {
                        saw_count = true;
                        EXPECT_EQ(1.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_count);
}

TEST_F(ReportSvcTest, OverdueFeeFilterByStaffOwnerApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO report_definitions(report_id,name,report_type,filter_json,is_active,created_by,created_at) "
                "VALUES(4,'Overdue Fees','overdue_fees','{}',1,1,1000)");
        // two bookings with different created_by
        g->Exec("INSERT INTO bookings VALUES(101,1,0,'Alice','','',0,100,'completed',0,0,'',11,1,0,0,'')");
        g->Exec("INSERT INTO bookings VALUES(102,1,0,'Bob','','',0,100,'completed',0,0,'',22,1,0,0,'')");
        g->Exec("INSERT INTO boarding_fees VALUES(1,101,500,50,NULL,NULL,1)");
        g->Exec("INSERT INTO boarding_fees VALUES(2,102,800,50,NULL,NULL,1)");
        }

        const std::string filter = R"({"staff_owner_id":11})";
        int64_t run_id = svc_->RunPipeline(4, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_total = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "total_overdue_cents") {
                        saw_total = true;
                        EXPECT_EQ(500.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_total);
}

TEST_F(ReportSvcTest, OverdueFeeFilterByPetTypeApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO report_definitions(report_id,name,report_type,filter_json,is_active,created_by,created_at) "
                "VALUES(4,'Overdue Fees','overdue_fees','{}',1,1,1000)");
        // bookings with different special_requirements
        g->Exec("INSERT INTO bookings VALUES(103,1,0,'C','','',0,100,'completed',0,0,'dog kennel',1,1,0,0,'')");
        g->Exec("INSERT INTO bookings VALUES(104,1,0,'D','','',0,100,'completed',0,0,'cat litter',1,1,0,0,'')");
        g->Exec("INSERT INTO boarding_fees VALUES(3,103,500,50,NULL,NULL,1)");
        g->Exec("INSERT INTO boarding_fees VALUES(4,104,800,50,NULL,NULL,1)");
        }

        const std::string filter = R"({"pet_type":"cat"})";
        int64_t run_id = svc_->RunPipeline(4, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_total = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "total_overdue_cents") {
                        saw_total = true;
                        EXPECT_EQ(800.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_total);
}

TEST_F(ReportSvcTest, InventorySummaryFilterByPetTypeApplied) {
        {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO report_definitions(report_id,name,report_type,filter_json,is_active,created_by,created_at) "
                "VALUES(5,'Inv Summary','inventory_summary','{}',1,1,1000)");
        g->Exec("INSERT INTO inventory_items VALUES(1,1,'Dog Food','dry food',NULL,5,0,NULL,NULL,NULL,1,1,1,NULL)");
        g->Exec("INSERT INTO inventory_items VALUES(2,1,'Cat Food','wet food',NULL,3,0,NULL,NULL,NULL,1,1,1,NULL)");
        }

        const std::string filter = R"({"pet_type":"dog"})";
        int64_t run_id = svc_->RunPipeline(5, filter, "manual", ctx_, 3000);
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);

        bool saw_total = false;
        for (const auto& s : snaps) {
                if (s.metric_name == "total_items") {
                        saw_total = true;
                        EXPECT_EQ(1.0, s.metric_value);
                }
        }
        EXPECT_TRUE(saw_total);
}
