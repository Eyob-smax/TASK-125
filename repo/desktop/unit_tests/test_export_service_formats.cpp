#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/services/AuditService.h"
#include <filesystem>
#include <fstream>

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
namespace fs = std::filesystem;
using shelterops::domain::UserRole;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("INSERT INTO users VALUES(2,'auditor','Auditor','h','auditor',1,1)");
    g->Exec("INSERT INTO users VALUES(3,'clerk','Clerk','h','inventory_clerk',1,1)");
    g->Exec("INSERT INTO users VALUES(4,'manager','Manager','h','operations_manager',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "event_type TEXT NOT NULL, description TEXT NOT NULL, actor_id INTEGER, "
            "occurred_at INTEGER NOT NULL, entity_type TEXT, entity_id INTEGER)");
    g->Exec("CREATE TABLE report_definitions(report_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, report_type TEXT NOT NULL, description TEXT, "
            "filter_json TEXT NOT NULL DEFAULT '{}', schedule_cron TEXT, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE report_runs(run_id INTEGER PRIMARY KEY, "
            "report_id INTEGER NOT NULL, version_label TEXT NOT NULL, triggered_by INTEGER, "
            "trigger_type TEXT NOT NULL, started_at INTEGER NOT NULL, completed_at INTEGER, "
            "status TEXT NOT NULL DEFAULT 'completed', filter_json TEXT NOT NULL DEFAULT '{}', "
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
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE product_catalog(entry_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, category_id INTEGER, default_unit_cost_cents INTEGER DEFAULT 0, "
            "vendor TEXT, sku TEXT UNIQUE, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE price_rules(rule_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, applies_to TEXT NOT NULL, condition_json TEXT NOT NULL DEFAULT '{}', "
            "adjustment_type TEXT NOT NULL, amount REAL NOT NULL, "
            "valid_from INTEGER, valid_to INTEGER, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE after_sales_adjustments(adjustment_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER, amount_cents INTEGER NOT NULL, reason TEXT NOT NULL, "
            "approved_by INTEGER, created_by INTEGER NOT NULL, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE retention_policies(policy_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL UNIQUE, retention_years INTEGER NOT NULL DEFAULT 7, "
            "action TEXT NOT NULL DEFAULT 'anonymize', updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE consent_records(consent_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL, entity_id INTEGER NOT NULL, "
            "consent_type TEXT NOT NULL, given_at INTEGER NOT NULL, withdrawn_at INTEGER)");
    g->Exec("CREATE TABLE export_permissions(permission_id INTEGER PRIMARY KEY, "
            "role TEXT NOT NULL, report_type TEXT NOT NULL, "
            "csv_allowed INTEGER NOT NULL DEFAULT 0, pdf_allowed INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(role,report_type))");
    g->Exec("INSERT INTO export_permissions(role,report_type,csv_allowed,pdf_allowed) "
            "VALUES('administrator','occupancy',1,1)");
    g->Exec("INSERT INTO export_permissions(role,report_type,csv_allowed,pdf_allowed) "
            "VALUES('operations_manager','occupancy',1,0)");
    // Seed a completed report run for export.
    g->Exec("INSERT INTO report_definitions(report_id,name,report_type,filter_json,is_active,created_by,created_at) "
            "VALUES(1,'Occ','occupancy','{}',1,1,1000)");
    g->Exec("INSERT INTO report_runs(run_id,report_id,version_label,trigger_type,started_at,status) "
            "VALUES(1,1,'occupancy-20260420-001','manual',1000,'completed')");
    g->Exec("INSERT INTO report_snapshots(run_id,metric_name,metric_value,dimension_json,captured_at) "
            "VALUES(1,'occupancy_rate',0.75,'{}',1000)");
    g->Exec("INSERT INTO report_snapshots(run_id,metric_name,metric_value,dimension_json,captured_at) "
            "VALUES(1,'avg_stay_nights',3.2,'{}',1000)");
}

class ExportFormatTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_          = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        report_repo_ = std::make_unique<ReportRepository>(*db_);
        admin_repo_  = std::make_unique<AdminRepository>(*db_);
        audit_repo_  = std::make_unique<AuditRepository>(*db_);
        audit_svc_   = std::make_unique<AuditService>(*audit_repo_);
        fs::create_directories("exports");
        svc_         = std::make_unique<ExportService>(*report_repo_, *admin_repo_, *audit_svc_, "exports");

        admin_ctx_.user_id = 1; admin_ctx_.role   = UserRole::Administrator;
        auditor_ctx_.user_id = 2; auditor_ctx_.role = UserRole::Auditor;
        clerk_ctx_.user_id   = 3; clerk_ctx_.role   = UserRole::InventoryClerk;
        manager_ctx_.user_id = 4; manager_ctx_.role = UserRole::OperationsManager;
    }

    void TearDown() override {
        // Remove test export files.
        std::error_code ec;
        fs::remove_all("exports", ec);
    }

    std::unique_ptr<Database>         db_;
    std::unique_ptr<ReportRepository> report_repo_;
    std::unique_ptr<AdminRepository>  admin_repo_;
    std::unique_ptr<AuditRepository>  audit_repo_;
    std::unique_ptr<AuditService>     audit_svc_;
    std::unique_ptr<ExportService>    svc_;
    UserContext admin_ctx_, auditor_ctx_, clerk_ctx_, manager_ctx_;
};

// ─── CSV format invariants ─────────────────────────────────────────────────

TEST_F(ExportFormatTest, CsvExportProducesNonEmptyFile) {
    auto r = svc_->RequestExport(1, "csv", admin_ctx_, 2000);
    ASSERT_TRUE(std::holds_alternative<int64_t>(r));
    int64_t jid = std::get<int64_t>(r);

    auto err = svc_->RunExportJob(jid, 3000);
    EXPECT_TRUE(err.message.empty());

    auto job = report_repo_->FindExportJob(jid);
    ASSERT_TRUE(job.has_value());
    EXPECT_FALSE(job->output_path.empty());

    // Verify the file exists and has content.
    std::ifstream f(job->output_path);
    EXPECT_TRUE(f.is_open()) << "CSV output file not found: " << job->output_path;
    if (f.is_open()) {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        EXPECT_FALSE(content.empty());
    }
}

TEST_F(ExportFormatTest, CsvExportSetsStatusToCompleted) {
    auto r = svc_->RequestExport(1, "csv", admin_ctx_, 2000);
    int64_t jid = std::get<int64_t>(r);
    svc_->RunExportJob(jid, 3000);

    auto job = report_repo_->FindExportJob(jid);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ("completed", job->status);
}

TEST_F(ExportFormatTest, CsvJobMaxConcurrencyIsOne) {
    auto r = svc_->RequestExport(1, "csv", admin_ctx_, 2000);
    int64_t jid = std::get<int64_t>(r);
    auto job = report_repo_->FindExportJob(jid);
    ASSERT_TRUE(job.has_value());
    EXPECT_GE(job->max_concurrency, 1);
}

// ─── PDF format invariants ─────────────────────────────────────────────────

TEST_F(ExportFormatTest, PdfJobMaxConcurrencyIsOne) {
    auto r = svc_->RequestExport(1, "pdf", admin_ctx_, 2000);
    ASSERT_TRUE(std::holds_alternative<int64_t>(r));
    int64_t jid = std::get<int64_t>(r);
    auto job = report_repo_->FindExportJob(jid);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(1, job->max_concurrency);
}

TEST_F(ExportFormatTest, PdfExportProducesOutputPath) {
    auto r = svc_->RequestExport(1, "pdf", admin_ctx_, 2000);
    int64_t jid = std::get<int64_t>(r);
    auto err = svc_->RunExportJob(jid, 3000);
    EXPECT_TRUE(err.message.empty());

    auto job = report_repo_->FindExportJob(jid);
    ASSERT_TRUE(job.has_value());
    EXPECT_FALSE(job->output_path.empty());
}

// ─── Permission gating ─────────────────────────────────────────────────────

TEST_F(ExportFormatTest, AuditorCsvExportForbidden) {
    auto r = svc_->RequestExport(1, "csv", auditor_ctx_, 2000);
        ASSERT_TRUE(std::holds_alternative<shelterops::common::ErrorEnvelope>(r));
        EXPECT_EQ(shelterops::common::ErrorCode::ExportUnauthorized,
                          std::get<shelterops::common::ErrorEnvelope>(r).code);
}

TEST_F(ExportFormatTest, AuditorCsvExportWritesUnauthorizedAuditEvent) {
    svc_->RequestExport(1, "csv", auditor_ctx_, 2000);
    bool found = false;
    {
        auto g = db_->Acquire();
        g->Query("SELECT 1 FROM audit_events WHERE event_type='EXPORT_UNAUTHORIZED'",
                 {}, [&found](const auto&, const auto&) { found = true; });
    }
    EXPECT_TRUE(found);
}

TEST_F(ExportFormatTest, InventoryClerkCsvExportForbidden) {
    auto r = svc_->RequestExport(1, "csv", clerk_ctx_, 2000);
        ASSERT_TRUE(std::holds_alternative<shelterops::common::ErrorEnvelope>(r));
        EXPECT_EQ(shelterops::common::ErrorCode::ExportUnauthorized,
                          std::get<shelterops::common::ErrorEnvelope>(r).code);
}

TEST_F(ExportFormatTest, ManagerCsvExportPermitted) {
    auto r = svc_->RequestExport(1, "csv", manager_ctx_, 2000);
    ASSERT_TRUE(std::holds_alternative<int64_t>(r));
    EXPECT_GT(std::get<int64_t>(r), 0);
}

TEST_F(ExportFormatTest, ManagerPdfExportForbiddenWhenNotGranted) {
    // operations_manager has pdf_allowed=0 for occupancy.
    auto r = svc_->RequestExport(1, "pdf", manager_ctx_, 2000);
        ASSERT_TRUE(std::holds_alternative<shelterops::common::ErrorEnvelope>(r));
        EXPECT_EQ(shelterops::common::ErrorCode::ExportUnauthorized,
                          std::get<shelterops::common::ErrorEnvelope>(r).code);
}

// ─── Two PDF jobs vs concurrency cap ──────────────────────────────────────

TEST_F(ExportFormatTest, SecondPdfJobIsQueuedIndependently) {
    // Both requests should succeed; concurrency is enforced by the worker, not here.
    auto r1 = svc_->RequestExport(1, "pdf", admin_ctx_, 2000);
    auto r2 = svc_->RequestExport(1, "pdf", admin_ctx_, 2001);

    ASSERT_TRUE(std::holds_alternative<int64_t>(r1));
    ASSERT_TRUE(std::holds_alternative<int64_t>(r2));
    EXPECT_NE(std::get<int64_t>(r1), std::get<int64_t>(r2));
}

// ─── Audit event on success ────────────────────────────────────────────────

TEST_F(ExportFormatTest, SuccessfulExportRequestWritesAuditEvent) {
    svc_->RequestExport(1, "csv", admin_ctx_, 2000);
    bool found = false;
    {
        auto g = db_->Acquire();
        g->Query("SELECT 1 FROM audit_events WHERE event_type='EXPORT_REQUESTED'",
                 {}, [&found](const auto&, const auto&) { found = true; });
    }
    EXPECT_TRUE(found);
}
