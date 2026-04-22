#include <gtest/gtest.h>
#include "shelterops/AppConfig.h"
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/RateLimiter.h"
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

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;

static void CreateAutomationContractSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");

    g->Exec("CREATE TABLE user_sessions(session_id TEXT PRIMARY KEY, "
            "user_id INTEGER NOT NULL, created_at INTEGER NOT NULL, "
            "expires_at INTEGER NOT NULL, device_fingerprint TEXT, is_active INTEGER NOT NULL DEFAULT 1,"
            "absolute_expires_at INTEGER NOT NULL DEFAULT 0)");
    g->Exec("INSERT INTO user_sessions VALUES('sess-valid',1,1,9999999999,'fp-1',1,9999999999)");

    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
}

class AutomationContractBehaviorTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = std::make_unique<Database>(":memory:");
        CreateAutomationContractSchema(*db_);

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

        dispatcher_ = std::make_unique<CommandDispatcher>(
            *booking_svc_, *inv_svc_, *report_svc_, *export_svc_, *alert_svc_,
            *session_repo_, *user_repo_, *rate_limiter_, *audit_svc_);
    }

    CommandEnvelope Env(const std::string& session,
                        const std::string& command,
                        const std::string& fingerprint) {
        CommandEnvelope e;
        e.session_token = session;
        e.command = command;
        e.device_fingerprint = fingerprint;
        e.body = {};
        return e;
    }

    std::unique_ptr<Database> db_;
    std::unique_ptr<InMemoryCredentialVault> booking_vault_;
    std::unique_ptr<KennelRepository> kennel_repo_;
    std::unique_ptr<BookingRepository> booking_repo_;
    std::unique_ptr<InventoryRepository> inv_repo_;
    std::unique_ptr<MaintenanceRepository> maint_repo_;
    std::unique_ptr<ReportRepository> report_repo_;
    std::unique_ptr<AdminRepository> admin_repo_;
    std::unique_ptr<SessionRepository> session_repo_;
    std::unique_ptr<UserRepository> user_repo_;
    std::unique_ptr<AuditRepository> audit_repo_;
    std::unique_ptr<AuditService> audit_svc_;
    std::unique_ptr<BookingService> booking_svc_;
    std::unique_ptr<InventoryService> inv_svc_;
    std::unique_ptr<ReportService> report_svc_;
    std::unique_ptr<ExportService> export_svc_;
    std::unique_ptr<AlertService> alert_svc_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    std::unique_ptr<CommandDispatcher> dispatcher_;
};

TEST_F(AutomationContractBehaviorTest, MissingSessionReturnsUnauthorizedEnvelope) {
    auto result = dispatcher_->Dispatch(Env("missing-session", "kennel.search", "fp-1"), 5000);
    EXPECT_EQ(401, result.http_status);
    EXPECT_FALSE(result.body.value("ok", true));
    ASSERT_TRUE(result.body.contains("error"));
    EXPECT_EQ("UNAUTHORIZED", result.body["error"].value("code", ""));
}

TEST_F(AutomationContractBehaviorTest, MissingFingerprintReturnsUnauthorizedEnvelope) {
    auto result = dispatcher_->Dispatch(Env("sess-valid", "kennel.search", ""), 5000);
    EXPECT_EQ(401, result.http_status);
    EXPECT_FALSE(result.body.value("ok", true));
    ASSERT_TRUE(result.body.contains("error"));
    EXPECT_EQ("UNAUTHORIZED", result.body["error"].value("code", ""));
}

TEST_F(AutomationContractBehaviorTest, UnknownCommandWithValidSessionReturnsNotFoundEnvelope) {
    auto result = dispatcher_->Dispatch(Env("sess-valid", "no.such.command", "fp-1"), 5000);
    EXPECT_EQ(404, result.http_status);
    EXPECT_FALSE(result.body.value("ok", true));
    ASSERT_TRUE(result.body.contains("error"));
    EXPECT_EQ("NOT_FOUND", result.body["error"].value("code", ""));
}

TEST_F(AutomationContractBehaviorTest, RateLimitContractReturns429WithRetryAfter) {
    auto tight_limiter = std::make_unique<RateLimiter>(1);
    CommandDispatcher tight_dispatcher(
        *booking_svc_, *inv_svc_, *report_svc_, *export_svc_, *alert_svc_,
        *session_repo_, *user_repo_, *tight_limiter, *audit_svc_);

    auto first = tight_dispatcher.Dispatch(Env("sess-valid", "no.such.command", "fp-1"), 5000);
    EXPECT_EQ(404, first.http_status);

    auto second = tight_dispatcher.Dispatch(Env("sess-valid", "no.such.command", "fp-1"), 5000);
    EXPECT_EQ(429, second.http_status);
    EXPECT_FALSE(second.body.value("ok", true));
    ASSERT_TRUE(second.body.contains("error"));
    EXPECT_EQ("RATE_LIMITED", second.body["error"].value("code", ""));
    EXPECT_TRUE(second.body["error"].contains("retry_after"));
}

TEST(AutomationContractConfig, DefaultRateLimitAndPortAreSafe) {
    shelterops::AppConfig cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_EQ(cfg.automation_rate_limit_rpm, 60);
    EXPECT_GT(cfg.automation_endpoint_port, static_cast<uint16_t>(1024));
    EXPECT_LT(cfg.automation_endpoint_port, static_cast<uint16_t>(65535));
}
