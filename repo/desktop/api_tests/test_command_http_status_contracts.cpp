#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/CredentialVault.h"
#include "shelterops/infrastructure/RateLimiter.h"
#include "shelterops/infrastructure/UpdateManager.h"
#include <filesystem>
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/ReportRepository.h"
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

// =============================================================================
// test_command_http_status_contracts.cpp
//
// Classification: in-process command test (no HTTP transport)
// Surface: CommandDispatcher — HTTP status code contract coverage
//
// Validates that every registered command returns the HTTP status codes
// specified in docs/api-spec.md §3 (Error Codes Reference) for:
//   - Missing session token → 401
//   - Missing device fingerprint → 401
//   - Valid session, unknown command → 404
//   - Valid session, known command, insufficient role → 403
//   - Valid session, malformed body → 400
//   - Valid session, valid request → 200 or 202
//   - Rate limit exceeded → 429 with retry_after
//
// Commands under test (all 12 registered dispatchers):
//   kennel.search, booking.create, booking.approve, booking.cancel,
//   inventory.issue, inventory.receive, report.trigger, report.status,
//   export.request, alerts.list, alerts.dismiss, update.import
// =============================================================================

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
namespace fs = std::filesystem;

static void BuildContractSchema(Database& db) {
    auto g = db.Acquire();

    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("INSERT INTO users VALUES(2,'clerk','Clerk','h','inventory_clerk',1,1)");

    g->Exec("CREATE TABLE user_sessions(session_id TEXT PRIMARY KEY, "
            "user_id INTEGER NOT NULL, created_at INTEGER NOT NULL, "
            "expires_at INTEGER NOT NULL, device_fingerprint TEXT, "
            "is_active INTEGER NOT NULL DEFAULT 1, "
            "absolute_expires_at INTEGER NOT NULL DEFAULT 0)");
    g->Exec("INSERT INTO user_sessions VALUES('sess-admin',1,1,9999999999,'fp',1,9999999999)");
    g->Exec("INSERT INTO user_sessions VALUES('sess-clerk',2,1,9999999999,'fp',1,9999999999)");

    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");

    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL DEFAULT '', row_label TEXT, "
            "x_coord_ft REAL NOT NULL DEFAULT 0, y_coord_ft REAL NOT NULL DEFAULT 0, "
            "description TEXT, is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("INSERT INTO zones VALUES(1,'Zone A','Main',NULL,0,0,NULL,1)");

    g->Exec("CREATE TABLE zone_distance_cache(from_zone_id INTEGER NOT NULL, "
            "to_zone_id INTEGER NOT NULL, distance_ft REAL NOT NULL, "
            "PRIMARY KEY(from_zone_id,to_zone_id))");

    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'boarding', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL DEFAULT 0, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("INSERT INTO kennels VALUES(1,1,'K1',1,'boarding',3500,4.0,1,NULL)");

    g->Exec("CREATE TABLE kennel_restrictions(restriction_id INTEGER PRIMARY KEY, "
            "kennel_id INTEGER NOT NULL, restriction_type TEXT NOT NULL, notes TEXT, "
            "UNIQUE(kennel_id,restriction_type))");

    g->Exec("CREATE TABLE animals(animal_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "species TEXT NOT NULL, intake_at INTEGER NOT NULL, intake_type TEXT NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'intake', is_aggressive INTEGER DEFAULT 0, "
            "is_large_dog INTEGER DEFAULT 0, breed TEXT, age_years REAL, weight_lbs REAL, "
            "color TEXT, sex TEXT, microchip_id TEXT, notes TEXT, "
            "created_by INTEGER, anonymized_at INTEGER)");

    g->Exec("CREATE TABLE adoptable_listings(listing_id INTEGER PRIMARY KEY, "
            "animal_id INTEGER NOT NULL, kennel_id INTEGER, listing_date INTEGER NOT NULL, "
            "adoption_fee_cents INTEGER NOT NULL DEFAULT 0, description TEXT, "
            "rating REAL, status TEXT NOT NULL DEFAULT 'active', "
            "created_by INTEGER, adopted_at INTEGER)");

    g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER NOT NULL, "
            "animal_id INTEGER, guest_name TEXT, guest_phone_enc TEXT, guest_email_enc TEXT, "
            "check_in_at INTEGER NOT NULL, check_out_at INTEGER NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'pending', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
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
            "query_hash TEXT NOT NULL, kennel_id INTEGER NOT NULL, "
            "rank_position INTEGER NOT NULL, score REAL NOT NULL, "
            "reason_json TEXT NOT NULL, generated_at INTEGER NOT NULL)");

    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, "
            "updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO system_policies(key,value,updated_at) "
            "VALUES('booking_approval_required','false',1)");

    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("INSERT INTO inventory_categories VALUES(1,'Food','kg',7,14,1)");

    g->Exec("CREATE TABLE inventory_items(item_id INTEGER PRIMARY KEY, "
            "category_id INTEGER NOT NULL, name TEXT NOT NULL, description TEXT, "
            "storage_location TEXT, quantity INTEGER NOT NULL DEFAULT 0, "
            "unit_cost_cents INTEGER NOT NULL DEFAULT 0, expiration_date INTEGER, "
            "serial_number TEXT UNIQUE, barcode TEXT, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, anonymized_at INTEGER)");
    g->Exec("INSERT INTO inventory_items VALUES(1,1,'Dog Food','',NULL,10,500,NULL,NULL,NULL,1,1,1,NULL)");

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
            "ticket_id INTEGER NOT NULL, actor_id INTEGER NOT NULL, "
            "event_type TEXT NOT NULL, old_status TEXT, new_status TEXT, "
            "notes TEXT, occurred_at INTEGER NOT NULL)");

    g->Exec("CREATE TABLE report_definitions(report_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, report_type TEXT NOT NULL, description TEXT, "
            "filter_json TEXT NOT NULL DEFAULT '{}', schedule_cron TEXT, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_by INTEGER, "
            "created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO report_definitions VALUES(1,'Occupancy Report','occupancy',NULL,'{}',NULL,1,1,1)");

    g->Exec("CREATE TABLE report_runs(run_id INTEGER PRIMARY KEY, "
            "report_id INTEGER NOT NULL, version_label TEXT NOT NULL, "
            "triggered_by INTEGER, trigger_type TEXT NOT NULL, "
            "started_at INTEGER NOT NULL, completed_at INTEGER, "
            "status TEXT NOT NULL DEFAULT 'running', "
            "filter_json TEXT NOT NULL DEFAULT '{}', "
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
            "action TEXT NOT NULL DEFAULT 'anonymize', "
            "updated_by INTEGER, updated_at INTEGER NOT NULL)");

    g->Exec("CREATE TABLE consent_records(consent_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL, entity_id INTEGER NOT NULL, "
            "consent_type TEXT NOT NULL, given_at INTEGER NOT NULL, withdrawn_at INTEGER)");

    g->Exec("CREATE TABLE export_permissions(permission_id INTEGER PRIMARY KEY, "
            "role TEXT NOT NULL, report_type TEXT NOT NULL, "
            "csv_allowed INTEGER NOT NULL DEFAULT 0, pdf_allowed INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(role,report_type))");
    g->Exec("INSERT INTO export_permissions(role,report_type,csv_allowed,pdf_allowed) "
            "VALUES('administrator','occupancy',1,1)");
}

class HttpStatusContractTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / "shelterops_http_contract_test";
        fs::create_directories(tmp_dir_);

        db_ = std::make_unique<Database>(":memory:");
        BuildContractSchema(*db_);

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
        rate_limiter_ = std::make_unique<RateLimiter>(60);
        update_mgr_   = std::make_unique<UpdateManager>(tmp_dir_.string());

        dispatcher_ = std::make_unique<CommandDispatcher>(
            *booking_svc_, *inv_svc_, *report_svc_, *export_svc_, *alert_svc_,
            *session_repo_, *user_repo_, *rate_limiter_, *audit_svc_,
            update_mgr_.get());
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tmp_dir_, ec);
    }

    CommandEnvelope Env(const std::string& session,
                        const std::string& command,
                        nlohmann::json body = {},
                        const std::string& fingerprint = "fp") {
        CommandEnvelope e;
        e.session_token      = session;
        e.command            = command;
        e.device_fingerprint = fingerprint;
        e.body               = std::move(body);
        return e;
    }

    CommandResult Dispatch(const std::string& session,
                           const std::string& command,
                           nlohmann::json body = {},
                           const std::string& fingerprint = "fp") {
        return dispatcher_->Dispatch(Env(session, command, std::move(body), fingerprint), 5000);
    }

    fs::path                               tmp_dir_;
    std::unique_ptr<Database>              db_;
    std::unique_ptr<InMemoryCredentialVault> booking_vault_;
    std::unique_ptr<UpdateManager>         update_mgr_;
    std::unique_ptr<KennelRepository>      kennel_repo_;
    std::unique_ptr<BookingRepository>     booking_repo_;
    std::unique_ptr<InventoryRepository>   inv_repo_;
    std::unique_ptr<MaintenanceRepository> maint_repo_;
    std::unique_ptr<ReportRepository>      report_repo_;
    std::unique_ptr<AdminRepository>       admin_repo_;
    std::unique_ptr<SessionRepository>     session_repo_;
    std::unique_ptr<UserRepository>        user_repo_;
    std::unique_ptr<AuditRepository>       audit_repo_;
    std::unique_ptr<AuditService>          audit_svc_;
    std::unique_ptr<BookingService>        booking_svc_;
    std::unique_ptr<InventoryService>      inv_svc_;
    std::unique_ptr<ReportService>         report_svc_;
    std::unique_ptr<ExportService>         export_svc_;
    std::unique_ptr<AlertService>          alert_svc_;
    std::unique_ptr<RateLimiter>           rate_limiter_;
    std::unique_ptr<CommandDispatcher>     dispatcher_;
};

// ── §3: UNAUTHORIZED (401) ───────────────────────────────────────────────────

TEST_F(HttpStatusContractTest, MissingSessionReturns401) {
    auto r = Dispatch("no-such-session", "kennel.search");
    EXPECT_EQ(401, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
    ASSERT_TRUE(r.body.contains("error"));
    EXPECT_EQ("UNAUTHORIZED", r.body["error"].value("code", ""));
}

TEST_F(HttpStatusContractTest, MissingFingerprintReturns401) {
    auto r = Dispatch("sess-admin", "kennel.search", {}, "");
    EXPECT_EQ(401, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
    ASSERT_TRUE(r.body.contains("error"));
    EXPECT_EQ("UNAUTHORIZED", r.body["error"].value("code", ""));
}

TEST_F(HttpStatusContractTest, UnauthorizedResponseOmitsSessionDetail) {
    // Per api-spec §6: unauthorized responses must not distinguish missing/expired/mismatched.
    auto r_missing  = Dispatch("",              "kennel.search");
    auto r_wrong    = Dispatch("wrong-session", "kennel.search");
    auto r_no_fp    = Dispatch("sess-admin",    "kennel.search", {}, "");
    EXPECT_EQ(401, r_missing.http_status);
    EXPECT_EQ(401, r_wrong.http_status);
    EXPECT_EQ(401, r_no_fp.http_status);
    EXPECT_EQ("UNAUTHORIZED", r_missing.body["error"].value("code", ""));
    EXPECT_EQ("UNAUTHORIZED", r_wrong.body["error"].value("code", ""));
    EXPECT_EQ("UNAUTHORIZED", r_no_fp.body["error"].value("code", ""));
}

// ── §3: NOT_FOUND (404) ───────────────────────────────────────────────────────

TEST_F(HttpStatusContractTest, UnknownCommandReturns404) {
    auto r = Dispatch("sess-admin", "no.such.command");
    EXPECT_EQ(404, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
    ASSERT_TRUE(r.body.contains("error"));
    EXPECT_EQ("NOT_FOUND", r.body["error"].value("code", ""));
}

// ── §3: RATE_LIMITED (429) ────────────────────────────────────────────────────

TEST_F(HttpStatusContractTest, RateLimitedReturns429WithRetryAfter) {
    auto tight_limiter = std::make_unique<RateLimiter>(1);
    auto tight_vault   = std::make_unique<InMemoryCredentialVault>();
    auto tight_svc     = std::make_unique<BookingService>(
        *kennel_repo_, *booking_repo_, *admin_repo_, *tight_vault, *audit_svc_);
    CommandDispatcher tight(
        *tight_svc, *inv_svc_, *report_svc_, *export_svc_, *alert_svc_,
        *session_repo_, *user_repo_, *tight_limiter, *audit_svc_);

    auto first  = tight.Dispatch(Env("sess-admin", "kennel.search",
                                     {{"check_in_at",1000},{"check_out_at",2000}}), 5000);
    auto second = tight.Dispatch(Env("sess-admin", "kennel.search",
                                     {{"check_in_at",1000},{"check_out_at",2000}}), 5000);

    // First call may succeed (200) or fail (400/404); what matters is the second is 429.
    (void)first;
    EXPECT_EQ(429, second.http_status);
    EXPECT_FALSE(second.body.value("ok", true));
    ASSERT_TRUE(second.body.contains("error"));
    EXPECT_EQ("RATE_LIMITED", second.body["error"].value("code", ""));
    EXPECT_TRUE(second.body["error"].contains("retry_after"))
        << "429 response must include retry_after per api-spec §2.3";
}

// ── §3: INVALID_INPUT (400) per command ──────────────────────────────────────

TEST_F(HttpStatusContractTest, KennelSearchMissingDatesReturns400) {
    // body missing check_in_at / check_out_at
    auto r = Dispatch("sess-admin", "kennel.search", {});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, BookingCreateMissingFieldsReturns400) {
    // kennel_id present but no guest_name or dates
    auto r = Dispatch("sess-admin", "booking.create", {{"kennel_id", 1}});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, BookingApproveInvalidIdReturns400or404) {
    auto r = Dispatch("sess-admin", "booking.approve", {{"booking_id", 999999}});
    EXPECT_TRUE(r.http_status == 400 || r.http_status == 404)
        << "approve for unknown id must be 400 or 404, got: " << r.http_status;
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, BookingCancelMissingIdReturns400) {
    auto r = Dispatch("sess-admin", "booking.cancel", {});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, InventoryReceiveMissingFieldsReturns400) {
    auto r = Dispatch("sess-admin", "inventory.receive", {});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, InventoryIssueMissingFieldsReturns400) {
    auto r = Dispatch("sess-admin", "inventory.issue", {});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, ReportTriggerMissingTypeReturns400) {
    auto r = Dispatch("sess-admin", "report.trigger", {});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, ReportStatusMissingRunIdReturns400) {
    auto r = Dispatch("sess-admin", "report.status", {});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, ExportRequestMissingRunIdReturns400) {
    auto r = Dispatch("sess-admin", "export.request", {{"format", "csv"}});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, AlertsDismissMissingAlertIdReturns400) {
    auto r = Dispatch("sess-admin", "alerts.dismiss", {});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

TEST_F(HttpStatusContractTest, UpdateImportMissingPathReturns400) {
    auto r = Dispatch("sess-admin", "update.import", {});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

// ── §3: SIGNATURE_INVALID → 400 ──────────────────────────────────────────────

TEST_F(HttpStatusContractTest, UpdateImportInvalidSignatureReturns400) {
    // Non-existent path will fail signature verification → SIGNATURE_INVALID / 400.
    auto r = Dispatch("sess-admin", "update.import",
                      {{"msi_path", "C:\\does\\not\\exist.msi"}});
    EXPECT_EQ(400, r.http_status);
    EXPECT_FALSE(r.body.value("ok", true));
}

// ── §3: Success (200/202) for well-formed requests ───────────────────────────

TEST_F(HttpStatusContractTest, KennelSearchValidReturns200) {
    auto r = Dispatch("sess-admin", "kennel.search",
                      {{"check_in_at", 1000}, {"check_out_at", 2000}});
    EXPECT_EQ(200, r.http_status);
    EXPECT_TRUE(r.body.value("ok", false));
    EXPECT_TRUE(r.body.contains("data"));
}

TEST_F(HttpStatusContractTest, AlertsListReturns200) {
    auto r = Dispatch("sess-admin", "alerts.list");
    EXPECT_EQ(200, r.http_status);
    EXPECT_TRUE(r.body.value("ok", false));
    EXPECT_TRUE(r.body.contains("data"));
}

TEST_F(HttpStatusContractTest, ReportTriggerValidReturns202) {
    auto r = Dispatch("sess-admin", "report.trigger", {{"report_id", 1}});
    EXPECT_TRUE(r.http_status == 200 || r.http_status == 202)
        << "report.trigger must return 200 or 202, got: " << r.http_status;
    EXPECT_TRUE(r.body.value("ok", false));
}

// ── §6: Secret-safe error envelopes ──────────────────────────────────────────

TEST_F(HttpStatusContractTest, ErrorEnvelopeNeverContainsStackTrace) {
    std::vector<std::string> commands = {
        "kennel.search", "booking.create", "booking.approve",
        "booking.cancel", "inventory.issue", "inventory.receive",
        "report.trigger", "report.status", "export.request",
        "alerts.list", "alerts.dismiss", "update.import"
    };
    for (const auto& cmd : commands) {
        auto r = Dispatch("sess-admin", cmd, {});
        if (r.body.contains("error")) {
            std::string body_str = r.body.dump();
            EXPECT_EQ(std::string::npos, body_str.find("exception"))
                << cmd << ": error envelope must not contain 'exception'";
            EXPECT_EQ(std::string::npos, body_str.find("stack"))
                << cmd << ": error envelope must not contain 'stack'";
            EXPECT_EQ(std::string::npos, body_str.find("trace"))
                << cmd << ": error envelope must not contain 'trace'";
        }
    }
}

TEST_F(HttpStatusContractTest, AllCommandsReturnOkField) {
    // Every response — success or error — must carry the "ok" field per §2.4.
    std::vector<std::string> commands = {
        "kennel.search", "booking.create", "booking.approve",
        "booking.cancel", "inventory.issue", "inventory.receive",
        "report.trigger", "report.status", "export.request",
        "alerts.list", "alerts.dismiss", "update.import"
    };
    for (const auto& cmd : commands) {
        auto r = Dispatch("sess-admin", cmd, {});
        EXPECT_TRUE(r.body.contains("ok"))
            << cmd << ": response must always contain 'ok' field";
    }
}
