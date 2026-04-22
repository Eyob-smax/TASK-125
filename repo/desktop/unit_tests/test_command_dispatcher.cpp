#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/RateLimiter.h"
#include "shelterops/infrastructure/CredentialVault.h"
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

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;

static void CreateMinimalSchema(Database& db) {
    auto g = db.Acquire();
    // Core tables needed for dispatcher tests.
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE user_sessions(session_id TEXT PRIMARY KEY, "
            "user_id INTEGER NOT NULL, created_at INTEGER NOT NULL, "
            "expires_at INTEGER NOT NULL, device_fingerprint TEXT, is_active INTEGER NOT NULL DEFAULT 1,"
            "absolute_expires_at INTEGER NOT NULL DEFAULT 0)");
    g->Exec("INSERT INTO user_sessions VALUES('sess-valid',1,1,9999999999,'fp-1',1,9999999999)");
    g->Exec("INSERT INTO user_sessions VALUES('sess-expired',1,1,1,'fp-1',0,9999999999)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
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
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, is_active INTEGER NOT NULL DEFAULT 1)");
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
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO system_policies(key,value,updated_at) "
            "VALUES('booking_approval_required','false',1)");
    g->Exec("CREATE TABLE product_catalog(entry_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "category_id INTEGER, default_unit_cost_cents INTEGER DEFAULT 0, "
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
}

class DispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_           = std::make_unique<Database>(":memory:");
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
        rate_limiter_ = std::make_unique<RateLimiter>(60);
        dispatcher_   = std::make_unique<CommandDispatcher>(
            *booking_svc_, *inv_svc_, *report_svc_, *export_svc_, *alert_svc_,
            *session_repo_, *user_repo_, *rate_limiter_, *audit_svc_);
    }

    CommandEnvelope MakeEnvelope(const std::string& cmd, nlohmann::json body) {
        CommandEnvelope env;
        env.command          = cmd;
        env.body             = std::move(body);
        env.session_token    = "sess-valid";
                env.device_fingerprint = "fp-1";
        return env;
    }

    std::unique_ptr<Database>               db_;
    std::unique_ptr<InMemoryCredentialVault> booking_vault_;
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

TEST_F(DispatcherTest, UnknownCommandReturnsNotFound) {
    auto env = MakeEnvelope("no.such.command", {});
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(404, result.http_status);
}

TEST_F(DispatcherTest, MissingSessionTokenReturnsUnauthorized) {
    CommandEnvelope env;
    env.command       = "kennel.search";
    env.body          = {};
    env.session_token = "sess-missing";
        env.device_fingerprint = "fp-1";
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(401, result.http_status);
}

TEST_F(DispatcherTest, ExpiredSessionReturnsUnauthorized) {
    CommandEnvelope env;
    env.command       = "kennel.search";
    env.body          = {};
    env.session_token = "sess-expired";
        env.device_fingerprint = "fp-1";
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(401, result.http_status);
}

TEST_F(DispatcherTest, MissingFingerprintReturnsUnauthorized) {
        CommandEnvelope env;
        env.command       = "kennel.search";
        env.body          = {{"check_in_at", 1000}, {"check_out_at", 2000}};
        env.session_token = "sess-valid";
        env.device_fingerprint = "";

        auto result = dispatcher_->Dispatch(env, 5000);
        EXPECT_EQ(401, result.http_status);
}

TEST_F(DispatcherTest, MismatchedFingerprintReturnsUnauthorized) {
        CommandEnvelope env;
        env.command       = "kennel.search";
        env.body          = {{"check_in_at", 1000}, {"check_out_at", 2000}};
        env.session_token = "sess-valid";
        env.device_fingerprint = "fp-wrong";

        auto result = dispatcher_->Dispatch(env, 5000);
        EXPECT_EQ(401, result.http_status);
}

TEST_F(DispatcherTest, KennelSearchHappyPath) {
    nlohmann::json body;
    body["check_in_at"]  = 1000;
    body["check_out_at"] = 2000;
    auto env = MakeEnvelope("kennel.search", body);
    auto result = dispatcher_->Dispatch(env, 5000);
    EXPECT_EQ(200, result.http_status);
    EXPECT_TRUE(result.body.value("ok", false));
}

TEST_F(DispatcherTest, RateLimitBlocksAfterExcess) {
    // Exhaust the token bucket (RPM=60, capacity=60).
    // The dispatcher uses rate_limiter_ with key=session_token.
    nlohmann::json body;
    body["check_in_at"] = 1000; body["check_out_at"] = 2000;
    auto env = MakeEnvelope("kennel.search", body);

    int rate_limited = 0;
    for (int i = 0; i < 70; ++i) {
        auto r = dispatcher_->Dispatch(env, 5000);
        if (r.http_status == 429) ++rate_limited;
    }
    EXPECT_GT(rate_limited, 0);
}
