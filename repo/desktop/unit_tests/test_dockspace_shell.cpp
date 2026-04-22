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
#include "shelterops/shell/TrayBadgeState.h"

// =============================================================================
// test_dockspace_shell.cpp
//
// Coverage target: DockspaceShell (shell/DockspaceShell.cpp)
//
// DockspaceShell::Render() is the main frame function for the authenticated
// desktop shell. It requires a live Win32 HWND + ImGui context and cannot be
// called in a headless Docker CI environment.
//
// This file tests the cross-platform state contracts that DockspaceShell reads
// from its dependencies each frame:
//
//   1. ShellController state gate — DockspaceShell only renders the authenticated
//      business shell when ShellState::ShellReady; any other state triggers the
//      login view branch.
//
//   2. SessionContext role gate — DockspaceShell calls session_ctx_.CurrentRole()
//      to determine menu visibility and TrayBadge update frequency.
//      All four roles must produce a valid non-empty RoleBadge string.
//
//   3. TrayBadgeState cycle — DockspaceShell reads tray_state_.TotalBadgeCount()
//      each frame. Badge count must reflect the current alert set without
//      accumulating across frames.
//
//   4. Exit protocol — When the user selects "Exit" in the menu bar,
//      exit_requested_ is set. DockspaceShell::Render() returns false.
//      Tests confirm that the ShellController OnLogout path that triggers this
//      clears SessionContext correctly.
//
// Full Render() path verification (ImGui draw calls, DX11 frame, tray icon
// placement) is covered by the native Windows desktop verification checklist
// in repo/README.md.
// =============================================================================

using namespace shelterops;
using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::shell;
using namespace shelterops::common;

static void CreateShellSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, "
            "username TEXT NOT NULL UNIQUE COLLATE NOCASE, display_name TEXT NOT NULL, "
            "password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL, "
            "last_login_at INTEGER, consent_given INTEGER NOT NULL DEFAULT 0, "
            "anonymized_at INTEGER, failed_login_attempts INTEGER NOT NULL DEFAULT 0, "
            "locked_until INTEGER)");
    g->Exec("CREATE TABLE user_sessions(session_id TEXT PRIMARY KEY, "
            "user_id INTEGER NOT NULL, created_at INTEGER NOT NULL, "
            "expires_at INTEGER NOT NULL, device_fingerprint TEXT, "
            "is_active INTEGER NOT NULL DEFAULT 1, "
            "absolute_expires_at INTEGER NOT NULL DEFAULT 0)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
}

class DockspaceShellContractTest : public ::testing::Test {
protected:
    void SetUp() override {
        CryptoHelper::Init();
        db_   = std::make_unique<Database>(":memory:");
        CreateShellSchema(*db_);
        ur_   = std::make_unique<UserRepository>(*db_);
        sr_   = std::make_unique<SessionRepository>(*db_);
        ar_   = std::make_unique<AuditRepository>(*db_);
        as_   = std::make_unique<AuditService>(*ar_);
        auth_ = std::make_unique<AuthService>(*ur_, *sr_, *as_, crypto_);
        ctx_  = std::make_unique<SessionContext>();

        std::string hash = CryptoHelper::HashPassword("Pass123!");
        ur_->Insert({"admin",   "Admin",   hash, "administrator"},    0);
        ur_->Insert({"manager", "Manager", hash, "operations_manager"}, 0);
        ur_->Insert({"clerk",   "Clerk",   hash, "inventory_clerk"},   0);
        ur_->Insert({"auditor", "Auditor", hash, "auditor"},           0);

        ctrl_ = std::make_unique<ShellController>(*auth_, *ur_, *ctx_);
        ctrl_->OnBootstrapComplete();
    }

    std::unique_ptr<Database>          db_;
    std::unique_ptr<UserRepository>    ur_;
    std::unique_ptr<SessionRepository> sr_;
    std::unique_ptr<AuditRepository>   ar_;
    std::unique_ptr<AuditService>      as_;
    CryptoHelper                       crypto_;
    std::unique_ptr<AuthService>       auth_;
    std::unique_ptr<SessionContext>    ctx_;
    std::unique_ptr<ShellController>   ctrl_;
};

// ---------------------------------------------------------------------------
// State gate: DockspaceShell must not render business windows before ShellReady
// ---------------------------------------------------------------------------

TEST_F(DockspaceShellContractTest, ShellNotReadyBeforeLogin) {
    // DockspaceShell checks state == ShellReady before rendering business windows.
    EXPECT_NE(ShellState::ShellReady, ctrl_->CurrentState());
    EXPECT_FALSE(ctx_->IsAuthenticated());
}

TEST_F(DockspaceShellContractTest, ShellReadyAfterSuccessfulLogin) {
    auto err = ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(ShellState::ShellReady, ctrl_->CurrentState());
    EXPECT_TRUE(ctx_->IsAuthenticated());
}

TEST_F(DockspaceShellContractTest, ShellNotReadyAfterFailedLogin) {
    auto err = ctrl_->OnLoginSubmitted("admin", "WrongPass", 1000);
    EXPECT_TRUE(err.has_value());
    EXPECT_NE(ShellState::ShellReady, ctrl_->CurrentState());
    EXPECT_FALSE(ctx_->IsAuthenticated());
}

TEST_F(DockspaceShellContractTest, ShellNotReadyAfterLogout) {
    ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    ASSERT_EQ(ShellState::ShellReady, ctrl_->CurrentState());
    ctrl_->OnLogout(2000);
    EXPECT_NE(ShellState::ShellReady, ctrl_->CurrentState());
    EXPECT_FALSE(ctx_->IsAuthenticated());
}

// ---------------------------------------------------------------------------
// RoleBadge: DockspaceShell displays role in status bar via RoleBadge()
// ---------------------------------------------------------------------------

TEST_F(DockspaceShellContractTest, AdminRoleBadgeIsNonEmpty) {
    ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    EXPECT_FALSE(ctrl_->RoleBadge().empty());
    EXPECT_EQ("Administrator", ctrl_->RoleBadge());
}

TEST_F(DockspaceShellContractTest, ManagerRoleBadgeIsNonEmpty) {
    ctrl_->OnLoginSubmitted("manager", "Pass123!", 1000);
    EXPECT_FALSE(ctrl_->RoleBadge().empty());
    EXPECT_EQ("Operations Manager", ctrl_->RoleBadge());
}

TEST_F(DockspaceShellContractTest, ClerkRoleBadgeIsNonEmpty) {
    ctrl_->OnLoginSubmitted("clerk", "Pass123!", 1000);
    EXPECT_FALSE(ctrl_->RoleBadge().empty());
    EXPECT_EQ("Inventory Clerk", ctrl_->RoleBadge());
}

TEST_F(DockspaceShellContractTest, AuditorRoleBadgeIsNonEmpty) {
    ctrl_->OnLoginSubmitted("auditor", "Pass123!", 1000);
    EXPECT_FALSE(ctrl_->RoleBadge().empty());
    EXPECT_EQ("Auditor", ctrl_->RoleBadge());
}

// ---------------------------------------------------------------------------
// SessionContext role gate: DockspaceShell reads CurrentRole() for menus
// ---------------------------------------------------------------------------

TEST_F(DockspaceShellContractTest, SessionContextCurrentRoleReflectsLoggedInRole) {
    ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    EXPECT_EQ(domain::UserRole::Administrator, ctx_->CurrentRole());
}

TEST_F(DockspaceShellContractTest, SessionContextCurrentRoleIsAuditorWhenNotAuthenticated) {
    // DockspaceShell default (most restrictive) when no session is active.
    EXPECT_EQ(domain::UserRole::Auditor, ctx_->CurrentRole());
}

TEST_F(DockspaceShellContractTest, SessionContextClearedOnLogout) {
    ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    ASSERT_TRUE(ctx_->IsAuthenticated());
    ctrl_->OnLogout(2000);
    EXPECT_FALSE(ctx_->IsAuthenticated());
    // After logout, CurrentRole() must return the most restrictive default.
    EXPECT_EQ(domain::UserRole::Auditor, ctx_->CurrentRole());
}

// ---------------------------------------------------------------------------
// TrayBadgeState cycle: DockspaceShell updates TrayBadgeState each frame
// ---------------------------------------------------------------------------

TEST_F(DockspaceShellContractTest, TrayBadgeStateReadsZeroWhenNoAlerts) {
    TrayBadgeState badge;
    badge.Update({});
    EXPECT_EQ(0, badge.TotalBadgeCount());
    EXPECT_FALSE(badge.HasAlerts());
}

TEST_F(DockspaceShellContractTest, TrayBadgeStateIsIdempotentForSameAlertSet) {
    TrayBadgeState badge;
    std::vector<AlertStateRecord> alerts;
    AlertStateRecord a;
    a.alert_type = "low_stock"; a.item_id = 1; a.triggered_at = 1000;
    alerts.push_back(a);

    badge.Update(alerts);
    int count1 = badge.TotalBadgeCount();
    badge.Update(alerts);
    int count2 = badge.TotalBadgeCount();

    // Calling Update twice with the same set must not double-count.
    EXPECT_EQ(count1, count2);
    EXPECT_EQ(1, count2);
}

// ---------------------------------------------------------------------------
// Exit protocol: DockspaceShell returns false when exit is requested
// The mechanism is: logout clears session; shell checks !IsAuthenticated() →
// sets exit_requested_ → returns false from Render().
// ---------------------------------------------------------------------------

TEST_F(DockspaceShellContractTest, LogoutClearsAuthenticationBeforeExitCheck) {
    ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    ASSERT_TRUE(ctx_->IsAuthenticated());
    ctrl_->OnLogout(2000);
    // After logout, IsAuthenticated() == false; DockspaceShell would set
    // exit_requested_ = true on the next frame and return false from Render().
    EXPECT_FALSE(ctx_->IsAuthenticated());
}

TEST_F(DockspaceShellContractTest, SessionContextGetThrowsOrReturnsValidWhenAuthenticated) {
    ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    ASSERT_TRUE(ctx_->IsAuthenticated());
    EXPECT_NO_THROW({
        auto uc = ctx_->Get();
        EXPECT_FALSE(uc.user_id == 0);
    });
}

// ---------------------------------------------------------------------------
// Multi-login sequence: DockspaceShell re-enters ShellReady after re-login
// ---------------------------------------------------------------------------

TEST_F(DockspaceShellContractTest, ReLoginAfterLogoutTransitionsToShellReady) {
    ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    ctrl_->OnLogout(2000);
    EXPECT_NE(ShellState::ShellReady, ctrl_->CurrentState());

    auto err = ctrl_->OnLoginSubmitted("admin", "Pass123!", 3000);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(ShellState::ShellReady, ctrl_->CurrentState());
}

TEST_F(DockspaceShellContractTest, DifferentRoleReLoginSwitchesContextRole) {
    ctrl_->OnLoginSubmitted("admin", "Pass123!", 1000);
    EXPECT_EQ(domain::UserRole::Administrator, ctx_->CurrentRole());
    ctrl_->OnLogout(2000);
    ctrl_->OnLoginSubmitted("auditor", "Pass123!", 3000);
    EXPECT_EQ(domain::UserRole::Auditor, ctx_->CurrentRole());
}
