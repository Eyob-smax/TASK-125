#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/repositories/SessionRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/AuthService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec(
        "CREATE TABLE users("
        "  user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "  display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL,"
        "  is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL,"
        "  last_login_at INTEGER, consent_given INTEGER NOT NULL DEFAULT 0,"
        "  anonymized_at INTEGER, failed_login_attempts INTEGER NOT NULL DEFAULT 0,"
        "  locked_until INTEGER)");
    g->Exec(
        "CREATE TABLE user_sessions("
        "  session_id TEXT PRIMARY KEY, user_id INTEGER NOT NULL,"
        "  created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL,"
        "  device_fingerprint TEXT, is_active INTEGER NOT NULL DEFAULT 1,"
        "  absolute_expires_at INTEGER NOT NULL DEFAULT 0)");
    g->Exec(
        "CREATE TABLE audit_events("
        "  event_id INTEGER PRIMARY KEY, occurred_at INTEGER NOT NULL,"
        "  actor_user_id INTEGER, actor_role TEXT, event_type TEXT NOT NULL,"
        "  entity_type TEXT, entity_id INTEGER, description TEXT NOT NULL,"
        "  session_id TEXT)");
}

class AuthServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        CryptoHelper::Init();
        db_          = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        user_repo_   = std::make_unique<UserRepository>(*db_);
        session_repo_= std::make_unique<SessionRepository>(*db_);
        audit_repo_  = std::make_unique<AuditRepository>(*db_);
        audit_svc_   = std::make_unique<AuditService>(*audit_repo_);
        auth_svc_    = std::make_unique<AuthService>(
            *user_repo_, *session_repo_, *audit_svc_, crypto_);

        // Create test user
        std::string hash = CryptoHelper::HashPassword("CorrectPassword123");
        NewUserParams p{"testuser", "Test User", hash, "administrator"};
        user_id_ = user_repo_->Insert(p, 1000);
    }

    std::unique_ptr<Database>           db_;
    std::unique_ptr<UserRepository>     user_repo_;
    std::unique_ptr<SessionRepository>  session_repo_;
    std::unique_ptr<AuditRepository>    audit_repo_;
    std::unique_ptr<AuditService>       audit_svc_;
    CryptoHelper                        crypto_;
    std::unique_ptr<AuthService>        auth_svc_;
    int64_t user_id_ = 0;
};

TEST_F(AuthServiceTest, HappyPathLoginIssuesSession) {
    auto result = auth_svc_->Login("testuser", "CorrectPassword123", "", 2000);
    ASSERT_TRUE(std::holds_alternative<SessionHandle>(result));
    const auto& handle = std::get<SessionHandle>(result);
    EXPECT_FALSE(handle.session_id.empty());
    EXPECT_EQ(handle.user_id, user_id_);
    EXPECT_EQ(handle.role, "administrator");
    EXPECT_GT(handle.expires_at, 2000);
}

TEST_F(AuthServiceTest, WrongPasswordReturnsError) {
    auto result = auth_svc_->Login("testuser", "WrongPassword!", "", 2000);
    ASSERT_TRUE(std::holds_alternative<AuthError>(result));
    EXPECT_EQ(std::get<AuthError>(result).code, "INVALID_CREDENTIALS");
}

TEST_F(AuthServiceTest, UnknownUsernameReturnsError) {
    auto result = auth_svc_->Login("nobody", "pass", "", 2000);
    ASSERT_TRUE(std::holds_alternative<AuthError>(result));
}

TEST_F(AuthServiceTest, FiveFailuresLocksAccount) {
    for (int i = 0; i < LockoutPolicy::kThreshold; ++i) {
        auth_svc_->Login("testuser", "wrong", "", 2000 + i);
    }
    auto result = auth_svc_->Login("testuser", "CorrectPassword123", "", 2100);
    ASSERT_TRUE(std::holds_alternative<AuthError>(result));
    EXPECT_EQ(std::get<AuthError>(result).code, "ACCOUNT_LOCKED");
}

TEST_F(AuthServiceTest, LogoutInvalidatesSession) {
    auto login = std::get<SessionHandle>(
        auth_svc_->Login("testuser", "CorrectPassword123", "", 2000));
    auth_svc_->Logout(login.session_id, 2001);
    auto sess = session_repo_->FindById(login.session_id);
    ASSERT_TRUE(sess.has_value());
    EXPECT_FALSE(sess->is_active);
}

TEST_F(AuthServiceTest, ExpiredSessionRejectedInValidate) {
    // Force a session with past expires_at.
    SessionRecord rec{"expired-sess", user_id_, 1000, 1500, "", true};
    session_repo_->Insert(rec);
    auto result = auth_svc_->ValidateSession("expired-sess", "", 2000);
    ASSERT_TRUE(std::holds_alternative<AuthError>(result));
    EXPECT_EQ(std::get<AuthError>(result).code, "UNAUTHORIZED");
}

TEST_F(AuthServiceTest, CreateInitialAdminFailsIfUsersExist) {
    EXPECT_FALSE(auth_svc_->CreateInitialAdmin("new", "newpass12345", "N", 9999));
}

TEST_F(AuthServiceTest, WrongPasswordWritesAuditEvent) {
    auth_svc_->Login("testuser", "bad", "", 2000);
    AuditQueryFilter f; f.event_type = "LOGIN_FAILURE";
    auto events = audit_repo_->Query(f);
    EXPECT_FALSE(events.empty());
}

TEST_F(AuthServiceTest, ChangePasswordPersistsNewHash) {
    std::string old_hash_before = user_repo_->FindById(user_id_)->password_hash;
    auto err = auth_svc_->ChangePassword(user_id_, "CorrectPassword123",
                                          "NewStrongPassword456!", 3000);
    EXPECT_TRUE(err.code.empty()) << err.message;

    // Hash stored in DB must differ from the original.
    std::string hash_after = user_repo_->FindById(user_id_)->password_hash;
    EXPECT_NE(hash_after, old_hash_before);

    // Old password must now be rejected.
    auto old_login = auth_svc_->Login("testuser", "CorrectPassword123", "", 3001);
    ASSERT_TRUE(std::holds_alternative<AuthError>(old_login));
    EXPECT_EQ(std::get<AuthError>(old_login).code, "INVALID_CREDENTIALS");

    // New password must be accepted.
    auto new_login = auth_svc_->Login("testuser", "NewStrongPassword456!", "", 3002);
    ASSERT_TRUE(std::holds_alternative<SessionHandle>(new_login));
}

TEST_F(AuthServiceTest, ChangePasswordRejectsWrongOldPassword) {
    auto err = auth_svc_->ChangePassword(user_id_, "WrongOldPass999",
                                          "NewStrongPassword456!", 3000);
    EXPECT_EQ(err.code, "INVALID_CREDENTIALS");

    // Original password must still work.
    auto login = auth_svc_->Login("testuser", "CorrectPassword123", "", 3001);
    ASSERT_TRUE(std::holds_alternative<SessionHandle>(login));
}

TEST_F(AuthServiceTest, ChangePasswordRejectsTooShortNewPassword) {
    auto err = auth_svc_->ChangePassword(user_id_, "CorrectPassword123",
                                          "short", 3000);
    EXPECT_EQ(err.code, "INVALID_INPUT");

    // Original password must still work.
    auto login = auth_svc_->Login("testuser", "CorrectPassword123", "", 3001);
    ASSERT_TRUE(std::holds_alternative<SessionHandle>(login));
}

TEST_F(AuthServiceTest, SessionRejectedAfter12HourAbsoluteCap) {
    int64_t login_time = 1000;
    auto login_result = auth_svc_->Login("testuser", "CorrectPassword123", "", login_time);
    ASSERT_TRUE(std::holds_alternative<SessionHandle>(login_result));
    const std::string session_id = std::get<SessionHandle>(login_result).session_id;

    // Activity within the 1-hour inactivity window should be valid.
    int64_t mid_time = login_time + 30 * 60;  // 30 minutes in
    auto mid_result = auth_svc_->ValidateSession(session_id, "", mid_time);
    EXPECT_TRUE(std::holds_alternative<UserContext>(mid_result))
        << "Session must be valid within the inactivity window";

    // Activity just past the 12-hour absolute cap must be rejected.
    int64_t past_cap = login_time + AuthService::kSessionLifetimeSec + 1;
    auto late_result = auth_svc_->ValidateSession(session_id, "", past_cap);
    ASSERT_TRUE(std::holds_alternative<AuthError>(late_result));
    EXPECT_EQ(std::get<AuthError>(late_result).code, "UNAUTHORIZED");
}

TEST_F(AuthServiceTest, SessionExpiryNeverExceeds12HourCap) {
    int64_t login_time = 2000;
    auto login_result = auth_svc_->Login("testuser", "CorrectPassword123", "", login_time);
    ASSERT_TRUE(std::holds_alternative<SessionHandle>(login_result));
    const std::string session_id = std::get<SessionHandle>(login_result).session_id;

    // Keep refreshing activity every 50 minutes (well within 1-hour window).
    for (int i = 1; i <= 12; ++i) {
        int64_t t = login_time + static_cast<int64_t>(i) * 50 * 60;
        if (t >= login_time + AuthService::kSessionLifetimeSec) break;
        auth_svc_->ValidateSession(session_id, "", t);
    }

    // At exactly 12h + 1s, session must be expired regardless of recent activity.
    int64_t after_cap = login_time + AuthService::kSessionLifetimeSec + 1;
    auto final_result = auth_svc_->ValidateSession(session_id, "", after_cap);
    ASSERT_TRUE(std::holds_alternative<AuthError>(final_result));
    EXPECT_EQ(std::get<AuthError>(final_result).code, "UNAUTHORIZED");
}
