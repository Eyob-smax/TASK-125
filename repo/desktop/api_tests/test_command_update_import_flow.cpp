#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/RateLimiter.h"
#include "shelterops/infrastructure/UpdateManager.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/SessionRepository.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/BookingService.h"
#include "shelterops/services/InventoryService.h"
#include "shelterops/services/ReportService.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/services/AlertService.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/CommandDispatcher.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

// =============================================================================
// test_command_update_import_flow.cpp
//
// Classification: in-process command test (no HTTP transport)
// Surface: CommandDispatcher → update.import command → UpdateManager
//
// Purpose: Verify the update.import command surface contract:
//   - Administrator role required (403 for all other roles)
//   - msi_path field required (400 when missing)
//   - Accepts a valid path and invokes UpdateManager pipeline
//   - Returns SignatureInvalid (400) for unsigned packages (cross-platform)
//   - Returns 200 with package metadata when signature check passes
//
// NOTE: On Linux/Docker CI, WinVerifyTrust is unavailable. All ImportPackage
// calls return SignatureFailed. Tests assert the signature-failure path
// is correctly translated to SIGNATURE_INVALID (HTTP 400) rather than a
// generic 500, confirming the error routing is correct.
// =============================================================================

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
namespace fs = std::filesystem;

static void CreateMinimalSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL, "
            "last_login_at INTEGER, consent_given INTEGER NOT NULL DEFAULT 0, "
            "anonymized_at INTEGER, failed_login_attempts INTEGER NOT NULL DEFAULT 0, "
            "locked_until INTEGER)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1,NULL,0,NULL,0,NULL)");
    g->Exec("INSERT INTO users VALUES(2,'manager','Manager','h','operations_manager',1,1,NULL,0,NULL,0,NULL)");
    g->Exec("INSERT INTO users VALUES(3,'clerk','Clerk','h','inventory_clerk',1,1,NULL,0,NULL,0,NULL)");
    g->Exec("INSERT INTO users VALUES(4,'auditor','Auditor','h','auditor',1,1,NULL,0,NULL,0,NULL)");
    g->Exec("CREATE TABLE user_sessions(session_id TEXT PRIMARY KEY, "
            "user_id INTEGER NOT NULL, created_at INTEGER NOT NULL, "
            "expires_at INTEGER NOT NULL, device_fingerprint TEXT, "
            "is_active INTEGER NOT NULL DEFAULT 1, "
            "absolute_expires_at INTEGER NOT NULL DEFAULT 0)");
    g->Exec("INSERT INTO user_sessions VALUES('tok-admin',  1,1,9999999999,'fp',1,9999999999)");
    g->Exec("INSERT INTO user_sessions VALUES('tok-manager',2,1,9999999999,'fp',1,9999999999)");
    g->Exec("INSERT INTO user_sessions VALUES('tok-clerk',  3,1,9999999999,'fp',1,9999999999)");
    g->Exec("INSERT INTO user_sessions VALUES('tok-auditor',4,1,9999999999,'fp',1,9999999999)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL DEFAULT '', row_label TEXT, "
            "x_coord_ft REAL DEFAULT 0, y_coord_ft REAL DEFAULT 0, "
            "description TEXT, is_active INTEGER DEFAULT 1)");
    g->Exec("CREATE TABLE zone_distance_cache(from_zone_id INTEGER NOT NULL, "
            "to_zone_id INTEGER NOT NULL, distance_ft REAL NOT NULL, "
            "PRIMARY KEY(from_zone_id,to_zone_id))");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'boarding', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL DEFAULT 0, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("CREATE TABLE kennel_restrictions(restriction_id INTEGER PRIMARY KEY, "
            "kennel_id INTEGER NOT NULL, restriction_type TEXT NOT NULL, notes TEXT, "
            "UNIQUE(kennel_id,restriction_type))");
    g->Exec("CREATE TABLE animals(animal_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "species TEXT NOT NULL, intake_at INTEGER NOT NULL, intake_type TEXT NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'intake', is_aggressive INTEGER DEFAULT 0, "
            "is_large_dog INTEGER DEFAULT 0, breed TEXT, age_years REAL, weight_lbs REAL, "
            "color TEXT, sex TEXT, microchip_id TEXT, notes TEXT, created_by INTEGER, "
            "anonymized_at INTEGER)");
    g->Exec("CREATE TABLE adoptable_listings(listing_id INTEGER PRIMARY KEY, "
            "animal_id INTEGER NOT NULL, kennel_id INTEGER, listing_date INTEGER NOT NULL, "
            "adoption_fee_cents INTEGER NOT NULL DEFAULT 0, description TEXT, "
            "rating REAL, status TEXT NOT NULL DEFAULT 'active', "
            "created_by INTEGER, adopted_at INTEGER)");
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
            "due_at INTEGER NOT NULL, paid_at INTEGER, payment_method TEXT, "
            "created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE recommendation_results(result_id INTEGER PRIMARY KEY, "
            "query_hash TEXT NOT NULL, kennel_id INTEGER NOT NULL, "
            "rank_position INTEGER NOT NULL, score REAL NOT NULL, "
            "reason_json TEXT NOT NULL, generated_at INTEGER NOT NULL)");
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
            "vendor TEXT, unit_cost_cents INTEGER NOT NULL DEFAULT 0, "
            "lot_number TEXT, notes TEXT)");
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
            "is_active INTEGER NOT NULL DEFAULT 1, created_by INTEGER, "
            "created_at INTEGER NOT NULL)");
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
            "requires_watermark INTEGER NOT NULL DEFAULT 0, "
            "restrictions_json TEXT NOT NULL DEFAULT '{}', "
            "UNIQUE(report_type,role,export_format))");
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, "
            "updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO system_policies(key,value,updated_at) "
            "VALUES('booking_approval_required','false',1)");
    g->Exec("CREATE TABLE product_catalog(entry_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, category_id INTEGER, "
            "default_unit_cost_cents INTEGER DEFAULT 0, "
            "vendor TEXT, sku TEXT UNIQUE, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE price_rules(rule_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, applies_to TEXT NOT NULL, "
            "condition_json TEXT NOT NULL DEFAULT '{}', "
            "adjustment_type TEXT NOT NULL, amount REAL NOT NULL, "
            "valid_from INTEGER, valid_to INTEGER, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE after_sales_adjustments(adjustment_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER, amount_cents INTEGER NOT NULL, reason TEXT NOT NULL, "
            "approved_by INTEGER, created_by INTEGER NOT NULL, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE retention_policies(policy_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL UNIQUE, retention_years INTEGER NOT NULL DEFAULT 7, "
            "action TEXT NOT NULL DEFAULT 'anonymize', updated_by INTEGER, "
            "updated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE consent_records(consent_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL, entity_id INTEGER NOT NULL, "
            "consent_type TEXT NOT NULL, given_at INTEGER NOT NULL, withdrawn_at INTEGER)");
    g->Exec("CREATE TABLE export_permissions(permission_id INTEGER PRIMARY KEY, "
            "role TEXT NOT NULL, report_type TEXT NOT NULL, "
            "csv_allowed INTEGER NOT NULL DEFAULT 0, pdf_allowed INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(role,report_type))");
}

class UpdateImportFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / "shelterops_update_import_test";
        fs::create_directories(tmp_dir_);

        std::error_code ec;
        fs::remove("test_update_import_flow.db", ec);
        db_ = std::make_unique<Database>("test_update_import_flow.db");
        CreateMinimalSchema(*db_);

        kennel_repo_  = std::make_unique<KennelRepository>(*db_);
        booking_repo_ = std::make_unique<BookingRepository>(*db_);
        inv_repo_     = std::make_unique<InventoryRepository>(*db_);
        maint_repo_   = std::make_unique<MaintenanceRepository>(*db_);
        report_repo_  = std::make_unique<ReportRepository>(*db_);
        admin_repo_   = std::make_unique<AdminRepository>(*db_);
        session_repo_ = std::make_unique<SessionRepository>(*db_);
        user_repo_    = std::make_unique<UserRepository>(*db_);
        audit_repo_   = std::make_unique<AuditRepository>(*db_);
        audit_svc_    = std::make_unique<AuditService>(*audit_repo_);
        booking_vault_ = std::make_unique<InMemoryCredentialVault>();
        booking_svc_  = std::make_unique<BookingService>(
            *kennel_repo_, *booking_repo_, *admin_repo_, *booking_vault_, *audit_svc_);
        inv_svc_      = std::make_unique<InventoryService>(*inv_repo_, *audit_svc_);
        report_svc_   = std::make_unique<ReportService>(
            *report_repo_, *kennel_repo_, *booking_repo_, *inv_repo_, *maint_repo_, *audit_svc_);
        export_svc_   = std::make_unique<ExportService>(*report_repo_, *admin_repo_, *audit_svc_);
        alert_svc_    = std::make_unique<AlertService>(*inv_repo_, *audit_svc_);
        rate_limiter_ = std::make_unique<RateLimiter>(1000);
        update_mgr_   = std::make_unique<UpdateManager>(tmp_dir_.string());

        dispatcher_ = std::make_unique<CommandDispatcher>(
            *booking_svc_, *inv_svc_, *report_svc_, *export_svc_, *alert_svc_,
            *session_repo_, *user_repo_, *rate_limiter_, *audit_svc_,
            update_mgr_.get());
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove("test_update_import_flow.db", ec);
        fs::remove_all(tmp_dir_, ec);
    }

    fs::path WriteDummyMsi(const std::string& filename,
                           const std::string& content = "MSI_PLACEHOLDER") {
        auto path = tmp_dir_ / filename;
        fs::create_directories(path.parent_path());
        std::ofstream f(path);
        f << content;
        return path;
    }

    CommandEnvelope MakeEnv(const std::string& tok, nlohmann::json body) {
        CommandEnvelope e;
        e.session_token      = tok;
        e.command            = "update.import";
        e.body               = std::move(body);
        e.device_fingerprint = "fp";
        return e;
    }

    fs::path                           tmp_dir_;
    std::unique_ptr<Database>          db_;
    std::unique_ptr<InMemoryCredentialVault> booking_vault_;
    std::unique_ptr<KennelRepository>  kennel_repo_;
    std::unique_ptr<BookingRepository> booking_repo_;
    std::unique_ptr<InventoryRepository> inv_repo_;
    std::unique_ptr<MaintenanceRepository> maint_repo_;
    std::unique_ptr<ReportRepository>  report_repo_;
    std::unique_ptr<AdminRepository>   admin_repo_;
    std::unique_ptr<SessionRepository> session_repo_;
    std::unique_ptr<UserRepository>    user_repo_;
    std::unique_ptr<AuditRepository>   audit_repo_;
    std::unique_ptr<AuditService>      audit_svc_;
    std::unique_ptr<BookingService>    booking_svc_;
    std::unique_ptr<InventoryService>  inv_svc_;
    std::unique_ptr<ReportService>     report_svc_;
    std::unique_ptr<ExportService>     export_svc_;
    std::unique_ptr<AlertService>      alert_svc_;
    std::unique_ptr<RateLimiter>       rate_limiter_;
    std::unique_ptr<UpdateManager>     update_mgr_;
    std::unique_ptr<CommandDispatcher> dispatcher_;
};

// ---------------------------------------------------------------------------
// Authorization surface
// ---------------------------------------------------------------------------

TEST_F(UpdateImportFlowTest, UpdateImportRequiresAdminRole) {
    // operations_manager must receive 403
    auto env = MakeEnv("tok-manager", {{"msi_path", "/some/path.msi"}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(403, result.http_status);
    ASSERT_TRUE(result.body.is_object());
    EXPECT_FALSE(result.body.value("ok", true));
}

TEST_F(UpdateImportFlowTest, UpdateImportForbiddenForInventoryClerk) {
    auto env = MakeEnv("tok-clerk", {{"msi_path", "/some/path.msi"}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(403, result.http_status);
}

TEST_F(UpdateImportFlowTest, UpdateImportForbiddenForAuditor) {
    auto env = MakeEnv("tok-auditor", {{"msi_path", "/some/path.msi"}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(403, result.http_status);
}

// ---------------------------------------------------------------------------
// Input validation
// ---------------------------------------------------------------------------

TEST_F(UpdateImportFlowTest, MissingMsiPathReturns400) {
    auto env = MakeEnv("tok-admin", nlohmann::json::object());
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(400, result.http_status);
    ASSERT_TRUE(result.body.is_object());
    EXPECT_FALSE(result.body.value("ok", true));
}

TEST_F(UpdateImportFlowTest, EmptyMsiPathReturns400) {
    auto env = MakeEnv("tok-admin", {{"msi_path", ""}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(400, result.http_status);
}

TEST_F(UpdateImportFlowTest, NonStringMsiPathReturns400) {
    auto env = MakeEnv("tok-admin", {{"msi_path", 42}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(400, result.http_status);
}

// ---------------------------------------------------------------------------
// Signature failure path (cross-platform: WinVerifyTrust unavailable on Linux)
// ---------------------------------------------------------------------------

TEST_F(UpdateImportFlowTest, UnsignedMsiReturnsSignatureInvalid) {
    auto msi_path = WriteDummyMsi("ShelterOpsDesk-1.1.0.msi");
    auto env = MakeEnv("tok-admin", {{"msi_path", msi_path.string()}});
    auto result = dispatcher_->Dispatch(env, 5000);
    // On all platforms an unsigned dummy .msi fails signature verification.
    // The command must translate this to 400 SIGNATURE_INVALID, not 500.
    EXPECT_EQ(400, result.http_status);
    ASSERT_TRUE(result.body.is_object());
    EXPECT_FALSE(result.body.value("ok", true));
    ASSERT_TRUE(result.body.contains("error"));
    EXPECT_EQ("SIGNATURE_INVALID",
              result.body["error"].value("code", std::string{}));
}

TEST_F(UpdateImportFlowTest, NonExistentMsiPathReturnsError) {
    auto env = MakeEnv("tok-admin",
                       {{"msi_path", "/nonexistent/path/ShelterOpsDesk-2.0.0.msi"}});
    auto result = dispatcher_->Dispatch(env, 5000);
    // Non-existent path → UpdateManager returns failure (not 404 the command itself)
    EXPECT_NE(200, result.http_status);
    EXPECT_NE(404, result.http_status); // 404 would mean the command is not registered
    ASSERT_TRUE(result.body.is_object());
}

TEST_F(UpdateImportFlowTest, NonMsiExtensionReturnsSignatureInvalid) {
    auto zip_path = tmp_dir_ / "update.zip";
    std::ofstream f(zip_path);
    f << "ZIP_CONTENT";
    f.close();

    auto env = MakeEnv("tok-admin", {{"msi_path", zip_path.string()}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(400, result.http_status);
    EXPECT_FALSE(result.body.value("ok", true));
}

// ---------------------------------------------------------------------------
// Command registered and returns JSON object
// ---------------------------------------------------------------------------

TEST_F(UpdateImportFlowTest, UpdateImportCommandIsRegisteredInDispatcher) {
    // Any call to update.import must not return 404 (command not registered).
    auto env = MakeEnv("tok-admin", {{"msi_path", "/any/path.msi"}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_NE(404, result.http_status)
        << "update.import returned 404 — not registered in CommandDispatcher";
}

TEST_F(UpdateImportFlowTest, UpdateImportAlwaysReturnsJsonObject) {
    auto env = MakeEnv("tok-admin", {{"msi_path", "/any/path.msi"}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_TRUE(result.body.is_object())
        << "update.import did not return a JSON object body";
}

TEST_F(UpdateImportFlowTest, UpdateImportBodyContainsOkField) {
    auto env = MakeEnv("tok-admin", {{"msi_path", "/any/path.msi"}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_TRUE(result.body.contains("ok"))
        << "update.import response is missing 'ok' field";
}

// ---------------------------------------------------------------------------
// Version extraction from filename
// ---------------------------------------------------------------------------

TEST_F(UpdateImportFlowTest, VersionExtractedFromFilenameInSignatureFailureResponse) {
    auto msi_path = WriteDummyMsi("ShelterOpsDesk-2.4.1.msi");
    auto env = MakeEnv("tok-admin", {{"msi_path", msi_path.string()}});
    dispatcher_->Dispatch(env, 5000);
    // UpdateManager should have parsed the version from the filename.
    ASSERT_TRUE(update_mgr_->Package().has_value());
    EXPECT_EQ("2.4.1", update_mgr_->Package()->version);
}

TEST_F(UpdateImportFlowTest, UpdateManagerStateIsSignatureFailedAfterUnsignedImport) {
    auto msi_path = WriteDummyMsi("ShelterOpsDesk-1.0.0.msi");
    auto env = MakeEnv("tok-admin", {{"msi_path", msi_path.string()}});
    dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(UpdateState::SignatureFailed, update_mgr_->State());
}

// ---------------------------------------------------------------------------
// Session auth enforcement
// ---------------------------------------------------------------------------

TEST_F(UpdateImportFlowTest, ExpiredSessionReturns401) {
    auto env = MakeEnv("invalid-session-token", {{"msi_path", "/some/path.msi"}});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(401, result.http_status);
}

TEST_F(UpdateImportFlowTest, MissingDeviceFingerprintReturns401) {
    CommandEnvelope e;
    e.session_token      = "tok-admin";
    e.command            = "update.import";
    e.body               = {{"msi_path", "/some/path.msi"}};
    e.device_fingerprint = "";
    auto result = dispatcher_->Dispatch(e, 5000);
    EXPECT_EQ(401, result.http_status);
}
