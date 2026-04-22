#include <gtest/gtest.h>
#include "shelterops/services/AutomationAuthMiddleware.h"
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/SessionRepository.h"
#include "shelterops/infrastructure/RateLimiter.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::common;

static void CreateSessionTable(Database& db) {
    db.Acquire()->Exec(
        "CREATE TABLE user_sessions("
        "  session_id TEXT PRIMARY KEY, user_id INTEGER NOT NULL,"
        "  created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL,"
        "  device_fingerprint TEXT, is_active INTEGER NOT NULL DEFAULT 1,"
        "  absolute_expires_at INTEGER NOT NULL DEFAULT 0)");
}

class MiddlewareTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSessionTable(*db_);
        repo_ = std::make_unique<SessionRepository>(*db_);
        limiter_ = std::make_unique<RateLimiter>(60);

        // Insert a valid active session
        SessionRecord r{"valid-token", 1, 1000, 99999, "fp-correct", true};
        repo_->Insert(r);

        // Insert an expired session
        SessionRecord expired{"expired-token", 2, 1000, 500, "", true};
        repo_->Insert(expired);
    }
    std::unique_ptr<Database>           db_;
    std::unique_ptr<SessionRepository>  repo_;
    std::unique_ptr<RateLimiter>        limiter_;
};

TEST_F(MiddlewareTest, MissingTokenHeader_Unauthorized) {
    HeaderMap headers = {};
    auto outcome = AutomationAuthMiddleware::VerifyHeaders(headers, *repo_, 1000);
    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.http_status, 401);
    EXPECT_EQ(outcome.error_code, "UNAUTHORIZED");
}

TEST_F(MiddlewareTest, ValidToken_Authorized) {
    HeaderMap headers = {
        {AutomationAuthMiddleware::kSessionTokenHeader,      "valid-token"},
        {AutomationAuthMiddleware::kDeviceFingerprintHeader, "fp-correct"}
    };
    auto outcome = AutomationAuthMiddleware::VerifyHeaders(headers, *repo_, 1000);
    EXPECT_TRUE(outcome.success);
}

TEST_F(MiddlewareTest, ExpiredToken_Unauthorized) {
    HeaderMap headers = {{AutomationAuthMiddleware::kSessionTokenHeader, "expired-token"}};
    auto outcome = AutomationAuthMiddleware::VerifyHeaders(headers, *repo_, 1000);
    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.http_status, 401);
}

TEST_F(MiddlewareTest, InvalidFingerprint_Unauthorized) {
    HeaderMap headers = {
        {AutomationAuthMiddleware::kSessionTokenHeader, "valid-token"},
        {AutomationAuthMiddleware::kDeviceFingerprintHeader, "wrong-fp"}
    };
    auto outcome = AutomationAuthMiddleware::VerifyHeaders(headers, *repo_, 1000);
    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.http_status, 401);
}

TEST_F(MiddlewareTest, CorrectFingerprint_Authorized) {
    HeaderMap headers = {
        {AutomationAuthMiddleware::kSessionTokenHeader, "valid-token"},
        {AutomationAuthMiddleware::kDeviceFingerprintHeader, "fp-correct"}
    };
    auto outcome = AutomationAuthMiddleware::VerifyHeaders(headers, *repo_, 1000);
    EXPECT_TRUE(outcome.success);
}

TEST_F(MiddlewareTest, RateLimitAllows) {
    auto r = AutomationAuthMiddleware::ApplyRateLimit("tok", *limiter_);
    EXPECT_TRUE(r.allowed);
}

TEST_F(MiddlewareTest, RateLimitBlocks) {
    RateLimiter small(2);
    AutomationAuthMiddleware::ApplyRateLimit("tok", small);
    AutomationAuthMiddleware::ApplyRateLimit("tok", small);
    auto r = AutomationAuthMiddleware::ApplyRateLimit("tok", small);
    EXPECT_FALSE(r.allowed);
    EXPECT_GT(r.retry_after_seconds, 0);
}

TEST_F(MiddlewareTest, ErrorResponseHasNoSecret) {
    ErrorEnvelope env{ErrorCode::Unauthorized, "please sign in"};
    std::string body = AutomationAuthMiddleware::BuildErrorResponse(env);
    EXPECT_EQ(body.find("password"), std::string::npos);
    EXPECT_EQ(body.find("token"),    std::string::npos);
    EXPECT_EQ(body.find("stack"),    std::string::npos);
    EXPECT_NE(body.find("UNAUTHORIZED"), std::string::npos);
}

TEST_F(MiddlewareTest, RateLimitResponseHasRetryAfter) {
    std::string body = AutomationAuthMiddleware::BuildRateLimitResponse(42);
    EXPECT_NE(body.find("42"), std::string::npos);
    EXPECT_NE(body.find("RATE_LIMITED"), std::string::npos);
}

TEST_F(MiddlewareTest, MalformedToken_NotFound) {
    HeaderMap headers = {{AutomationAuthMiddleware::kSessionTokenHeader, "no-such-token"}};
    auto outcome = AutomationAuthMiddleware::VerifyHeaders(headers, *repo_, 1000);
    EXPECT_FALSE(outcome.success);
    EXPECT_EQ(outcome.http_status, 401);
}
