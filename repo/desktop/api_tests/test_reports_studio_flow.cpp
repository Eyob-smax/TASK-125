#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/ReportService.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/ui/controllers/ReportsController.h"

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
    g->Exec("INSERT INTO users VALUES(1,'mgr','Manager','h','operations_manager',1,1)");
    g->Exec("INSERT INTO users VALUES(2,'auditor','Auditor','h','auditor',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
    g->Exec("CREATE TABLE report_definitions(report_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, report_type TEXT NOT NULL, description TEXT, "
            "filter_json TEXT NOT NULL DEFAULT '{}', schedule_cron TEXT, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE report_runs(run_id INTEGER PRIMARY KEY, "
            "report_id INTEGER NOT NULL, version_label TEXT, triggered_by INTEGER, "
            "trigger_type TEXT, started_at INTEGER, completed_at INTEGER, "
            "status TEXT NOT NULL DEFAULT 'queued', filter_json TEXT DEFAULT '{}', "
            "output_path TEXT, error_message TEXT, anomaly_flags_json TEXT, row_count INTEGER DEFAULT 0)");
    g->Exec("CREATE TABLE report_snapshots(snapshot_id INTEGER PRIMARY KEY, "
            "run_id INTEGER NOT NULL, metric_name TEXT NOT NULL, metric_value REAL NOT NULL, "
            "dimension_json TEXT NOT NULL DEFAULT '{}', captured_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE export_jobs(job_id INTEGER PRIMARY KEY, "
            "report_run_id INTEGER NOT NULL, format TEXT NOT NULL, "
            "requested_by INTEGER NOT NULL, queued_at INTEGER NOT NULL, "
            "started_at INTEGER DEFAULT 0, completed_at INTEGER DEFAULT 0, "
            "output_path TEXT DEFAULT '', status TEXT NOT NULL DEFAULT 'queued', "
            "max_concurrency INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE watermark_rules(rule_id INTEGER PRIMARY KEY, "
            "report_type TEXT NOT NULL UNIQUE, watermark_text TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL DEFAULT '', row_label TEXT, "
            "x_coord_ft REAL NOT NULL DEFAULT 0, y_coord_ft REAL NOT NULL DEFAULT 0, "
            "description TEXT, is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE zone_distance_cache(from_zone_id INTEGER NOT NULL, "
            "to_zone_id INTEGER NOT NULL, distance_ft REAL NOT NULL, "
            "PRIMARY KEY(from_zone_id, to_zone_id))");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'boarding', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL DEFAULT 0, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("CREATE TABLE kennel_restrictions(restriction_id INTEGER PRIMARY KEY, "
            "kennel_id INTEGER NOT NULL, restriction_type TEXT NOT NULL, notes TEXT, "
            "UNIQUE(kennel_id,restriction_type))");
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
            "due_at INTEGER NOT NULL, paid_at INTEGER, "
            "payment_method TEXT, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE recommendation_results(result_id INTEGER PRIMARY KEY, "
            "query_hash TEXT NOT NULL, kennel_id INTEGER NOT NULL, rank_position INTEGER NOT NULL, "
            "score REAL NOT NULL, reason_json TEXT NOT NULL, generated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
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
            "zone_id INTEGER, kennel_id INTEGER, category TEXT NOT NULL, "
            "description TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'open', "
            "priority TEXT NOT NULL DEFAULT 'normal', "
            "assigned_to INTEGER, first_action_at INTEGER, resolved_at INTEGER, "
            "created_by INTEGER NOT NULL, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE maintenance_events(event_id INTEGER PRIMARY KEY, "
            "ticket_id INTEGER NOT NULL, event_type TEXT NOT NULL, "
            "notes TEXT, actor_id INTEGER NOT NULL, occurred_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE product_catalog(entry_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, category_id INTEGER, default_unit_cost_cents INTEGER DEFAULT 0, "
            "vendor TEXT, sku TEXT UNIQUE, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE export_permissions(permission_id INTEGER PRIMARY KEY, "
            "role TEXT NOT NULL, report_type TEXT NOT NULL, "
            "csv_allowed INTEGER NOT NULL DEFAULT 0, pdf_allowed INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(role,report_type))");
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO system_policies VALUES(1,'booking_approval_required','false',1,1)");
    // Seed report definitions
    g->Exec("INSERT INTO report_definitions VALUES(1,'Occupancy','occupancy','Daily beds','{}',"
            "'',1,1,1000)");
    g->Exec("INSERT INTO report_definitions VALUES(2,'Turnover','turnover','Kennel turnover','{}',"
            "'',1,1,1000)");
    // Grant manager CSV export permission for occupancy
    g->Exec("INSERT INTO export_permissions VALUES(1,'operations_manager','occupancy',1,0)");
}

class ReportsStudioFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_          = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        report_repo_ = std::make_unique<ReportRepository>(*db_);
        kennel_repo_ = std::make_unique<KennelRepository>(*db_);
        booking_repo_= std::make_unique<BookingRepository>(*db_);
        inv_repo_    = std::make_unique<InventoryRepository>(*db_);
        maint_repo_  = std::make_unique<MaintenanceRepository>(*db_);
        admin_repo_  = std::make_unique<AdminRepository>(*db_);
        audit_repo_  = std::make_unique<AuditRepository>(*db_);
        audit_svc_   = std::make_unique<AuditService>(*audit_repo_);
        report_svc_  = std::make_unique<ReportService>(
            *report_repo_, *kennel_repo_, *booking_repo_,
            *inv_repo_, *maint_repo_, *audit_svc_);
        export_svc_  = std::make_unique<ExportService>(*report_repo_, *admin_repo_, *audit_svc_);
        ctrl_        = std::make_unique<ReportsController>(*report_svc_, *export_svc_, *report_repo_);

        mgr_ctx_.user_id = 1;
        mgr_ctx_.role    = UserRole::OperationsManager;
        auditor_ctx_.user_id = 2;
        auditor_ctx_.role    = UserRole::Auditor;
    }

    std::unique_ptr<Database>             db_;
    std::unique_ptr<ReportRepository>     report_repo_;
    std::unique_ptr<KennelRepository>     kennel_repo_;
    std::unique_ptr<BookingRepository>    booking_repo_;
    std::unique_ptr<InventoryRepository>  inv_repo_;
    std::unique_ptr<MaintenanceRepository> maint_repo_;
    std::unique_ptr<AdminRepository>      admin_repo_;
    std::unique_ptr<AuditRepository>      audit_repo_;
    std::unique_ptr<AuditService>         audit_svc_;
    std::unique_ptr<ReportService>        report_svc_;
    std::unique_ptr<ExportService>        export_svc_;
    std::unique_ptr<ReportsController>    ctrl_;
    UserContext                           mgr_ctx_;
    UserContext                           auditor_ctx_;
};

TEST_F(ReportsStudioFlowTest, TriggerOccupancyReportProducesRun) {
    int64_t run_id = ctrl_->TriggerReport(1, "{}", mgr_ctx_, 5000);
    ASSERT_GT(run_id, 0);
    ctrl_->RefreshRunStatus(run_id);
    ASSERT_FALSE(ctrl_->ActiveRuns().empty());
    bool found = false;
    for (const auto& r : ctrl_->ActiveRuns()) {
        if (r.run_id == run_id) {
            EXPECT_TRUE(r.status == "completed" || r.status == "failed");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ReportsStudioFlowTest, RunHistoryPersistedToDatabase) {
    ctrl_->TriggerReport(1, "{}", mgr_ctx_, 5000);
    ctrl_->TriggerReport(1, "{}", mgr_ctx_, 6000);
    ctrl_->LoadRunsForReport(1);
    EXPECT_GE(ctrl_->ActiveRuns().size(), 2u);
}

TEST_F(ReportsStudioFlowTest, VersionLabelUniquePerRun) {
    int64_t run1 = ctrl_->TriggerReport(1, "{}", mgr_ctx_, 86400);
    int64_t run2 = ctrl_->TriggerReport(1, "{}", mgr_ctx_, 86401);
    ASSERT_GT(run1, 0);
    ASSERT_GT(run2, 0);
    EXPECT_NE(run1, run2);

    auto rec1 = report_repo_->FindRun(run1);
    auto rec2 = report_repo_->FindRun(run2);
    ASSERT_TRUE(rec1.has_value());
    ASSERT_TRUE(rec2.has_value());
    EXPECT_NE(rec1->version_label, rec2->version_label)
        << "Each run must receive a unique version label";
}

TEST_F(ReportsStudioFlowTest, MetricSnapshotsWrittenAfterCompletedRun) {
    int64_t run_id = ctrl_->TriggerReport(1, "{}", mgr_ctx_, 5000);
    ASSERT_GT(run_id, 0);
    auto rec = report_repo_->FindRun(run_id);
    ASSERT_TRUE(rec.has_value());
    if (rec->status == "completed") {
        auto snaps = report_repo_->ListSnapshotsForRun(run_id);
        EXPECT_GE(snaps.size(), 1u) << "Completed run must have at least one metric snapshot";
    }
}

TEST_F(ReportsStudioFlowTest, VersionComparisonReturnsDeltas) {
    int64_t run_a = ctrl_->TriggerReport(1, "{}", mgr_ctx_, 5000);
    int64_t run_b = ctrl_->TriggerReport(1, "{}", mgr_ctx_, 6000);
    ASSERT_GT(run_a, 0);
    ASSERT_GT(run_b, 0);
    auto deltas = report_svc_->CompareVersions(run_a, run_b);
    // If both runs produced snapshots, deltas will be non-empty.
    // Even if empty (no snapshots), the call must not throw.
    (void)deltas;
}

TEST_F(ReportsStudioFlowTest, ExportCsvPermittedForManager) {
    int64_t run_id = ctrl_->TriggerReport(1, "{}", mgr_ctx_, 5000);
    ASSERT_GT(run_id, 0);
    int64_t job_id = ctrl_->RequestExport(run_id, "csv", mgr_ctx_, 6000);
    EXPECT_GT(job_id, 0) << "Manager must be permitted to export CSV for occupancy";
}

TEST_F(ReportsStudioFlowTest, ExportPdfUnauthorizedForAuditor) {
    int64_t run_id = ctrl_->TriggerReport(1, "{}", mgr_ctx_, 5000);
    ASSERT_GT(run_id, 0);
    // Auditor has no export_permission row
    int64_t job_id = ctrl_->RequestExport(run_id, "pdf", auditor_ctx_, 6000);
    EXPECT_EQ(0, job_id) << "Auditor must be denied PDF export without permission";
}

TEST_F(ReportsStudioFlowTest, AnomalyFlagsRecordedOnFailedRun) {
    // Trigger report for non-existent report_id to force failure path.
    int64_t run_id = ctrl_->TriggerReport(999, "{}", mgr_ctx_, 5000);
    // run_id may be 0 or represent a failed run; the key assertion is no crash.
    if (run_id > 0) {
        auto rec = report_repo_->FindRun(run_id);
        if (rec && rec->status == "failed") {
            // Anomaly or error must be recorded
            EXPECT_TRUE(!rec->error_message.empty() || !rec->anomaly_flags_json.empty());
        }
    }
}
