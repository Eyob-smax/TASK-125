#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/repositories/SessionRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/AuthService.h"
#include "shelterops/shell/SessionContext.h"
#include "shelterops/shell/ShellController.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::shell;
using namespace shelterops::common;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE COLLATE NOCASE, display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL, last_login_at INTEGER, consent_given INTEGER NOT NULL DEFAULT 0, anonymized_at INTEGER, failed_login_attempts INTEGER NOT NULL DEFAULT 0, locked_until INTEGER)");
    g->Exec("CREATE TABLE user_sessions(session_id TEXT PRIMARY KEY, user_id INTEGER NOT NULL, created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, device_fingerprint TEXT, is_active INTEGER NOT NULL DEFAULT 1, absolute_expires_at INTEGER NOT NULL DEFAULT 0)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, description TEXT NOT NULL, session_id TEXT)");
}

class ShellControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        CryptoHelper::Init();
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        ur_   = std::make_unique<UserRepository>(*db_);
        sr_   = std::make_unique<SessionRepository>(*db_);
        ar_   = std::make_unique<AuditRepository>(*db_);
        as_   = std::make_unique<AuditService>(*ar_);
        auth_ = std::make_unique<AuthService>(*ur_, *sr_, *as_, crypto_);
        ctx_  = std::make_unique<SessionContext>();
        std::string hash = CryptoHelper::HashPassword("ValidPassword123!");
        ur_->Insert({"admin", "Admin", hash, "administrator"}, 0);
        ctrl_ = std::make_unique<ShellController>(*auth_, *ur_, *ctx_);
        ctrl_->OnBootstrapComplete();
    }

    std::unique_ptr<Database>           db_;
    std::unique_ptr<UserRepository>     ur_;
    std::unique_ptr<SessionRepository>  sr_;
    std::unique_ptr<AuditRepository>    ar_;
    std::unique_ptr<AuditService>       as_;
    CryptoHelper                        crypto_;
    std::unique_ptr<AuthService>        auth_;
    std::unique_ptr<SessionContext>     ctx_;
    std::unique_ptr<ShellController>    ctrl_;
};

TEST_F(ShellControllerTest, InitialStateIsLoginRequired) {
    EXPECT_EQ(ctrl_->CurrentState(), ShellState::LoginRequired);
}

TEST_F(ShellControllerTest, SuccessfulLoginTransitionsToShellReady) {
    auto err = ctrl_->OnLoginSubmitted("admin", "ValidPassword123!", 1000);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(ctrl_->CurrentState(), ShellState::ShellReady);
}

TEST_F(ShellControllerTest, FailedLoginStaysAtLoginRequired) {
    auto err = ctrl_->OnLoginSubmitted("admin", "WrongPassword", 1000);
    EXPECT_TRUE(err.has_value());
    EXPECT_EQ(ctrl_->CurrentState(), ShellState::LoginRequired);
}

TEST_F(ShellControllerTest, LastErrorSetOnFailure) {
    ctrl_->OnLoginSubmitted("admin", "WrongPassword", 1000);
    ASSERT_TRUE(ctrl_->LastError().has_value());
    EXPECT_EQ(ctrl_->LastError()->code, ErrorCode::Unauthorized);
}

TEST_F(ShellControllerTest, LogoutReturnsToLoginRequired) {
    ctrl_->OnLoginSubmitted("admin", "ValidPassword123!", 1000);
    ASSERT_EQ(ctrl_->CurrentState(), ShellState::ShellReady);
    ctrl_->OnLogout(1001);
    EXPECT_EQ(ctrl_->CurrentState(), ShellState::LoginRequired);
    EXPECT_FALSE(ctx_->IsAuthenticated());
}

TEST_F(ShellControllerTest, RoleBadgeReflectsRole) {
    ctrl_->OnLoginSubmitted("admin", "ValidPassword123!", 1000);
    EXPECT_EQ(ctrl_->RoleBadge(), "Administrator");
}

TEST(ShellControllerBootstrapTest, EmptyUserTableRequiresInitialAdminSetup) {
    CryptoHelper::Init();
    auto db = std::make_unique<Database>(":memory:");
    CreateSchema(*db);
    auto ur = std::make_unique<UserRepository>(*db);
    auto sr = std::make_unique<SessionRepository>(*db);
    auto ar = std::make_unique<AuditRepository>(*db);
    auto as = std::make_unique<AuditService>(*ar);
    CryptoHelper crypto;
    auto auth = std::make_unique<AuthService>(*ur, *sr, *as, crypto);
    auto ctx = std::make_unique<SessionContext>();
    auto ctrl = std::make_unique<ShellController>(*auth, *ur, *ctx);

    ctrl->OnBootstrapComplete();
    EXPECT_EQ(ctrl->CurrentState(), ShellState::InitialAdminSetupRequired);

    auto err = ctrl->CreateInitialAdmin("admin", "ValidPassword123!", "Admin", 1000);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(ctrl->CurrentState(), ShellState::LoginRequired);
}
