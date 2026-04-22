#include <gtest/gtest.h>
#include "shelterops/common/SecretSafeLogger.h"

using namespace shelterops::common;

TEST(SecretSafeLogger, ScrubJsonPassword) {
    std::string json = R"({"user":"alice","password":"hunter2"})";
    std::string scrubbed = SecretSafeLogger::ScrubJson(json);
    EXPECT_EQ(scrubbed.find("hunter2"), std::string::npos);
    EXPECT_NE(scrubbed.find("REDACTED"), std::string::npos);
    EXPECT_NE(scrubbed.find("alice"), std::string::npos); // non-secret preserved
}

TEST(SecretSafeLogger, ScrubJsonToken) {
    std::string json = R"({"session_token":"abc123xyz","event":"login"})";
    std::string scrubbed = SecretSafeLogger::ScrubJson(json);
    EXPECT_EQ(scrubbed.find("abc123xyz"), std::string::npos);
}

TEST(SecretSafeLogger, ScrubJsonApiKey) {
    std::string json = R"({"api_key":"super_secret","action":"trigger"})";
    std::string scrubbed = SecretSafeLogger::ScrubJson(json);
    EXPECT_EQ(scrubbed.find("super_secret"), std::string::npos);
}

TEST(SecretSafeLogger, ScrubJsonMsiSha256) {
    std::string json = R"({"msi_sha256":"abcdef1234","version":"1.0.0"})";
    std::string scrubbed = SecretSafeLogger::ScrubJson(json);
    EXPECT_EQ(scrubbed.find("abcdef1234"), std::string::npos);
}

TEST(SecretSafeLogger, NonSecretFieldPreserved) {
    std::string json = R"({"user_id":"42","event":"LOGIN"})";
    std::string scrubbed = SecretSafeLogger::ScrubJson(json);
    EXPECT_NE(scrubbed.find("42"), std::string::npos);
    EXPECT_NE(scrubbed.find("LOGIN"), std::string::npos);
}

TEST(SecretSafeLogger, ScrubStringBearerToken) {
    std::string s = "Authorization: Bearer eyJhbGciOiJSUzI1NiJ9.payload.sig";
    std::string scrubbed = SecretSafeLogger::ScrubString(s);
    EXPECT_EQ(scrubbed.find("eyJhbGci"), std::string::npos);
    EXPECT_NE(scrubbed.find("REDACTED"), std::string::npos);
}

TEST(SecretSafeLogger, ScrubStringNoChange) {
    std::string s = "User logged in successfully";
    EXPECT_EQ(SecretSafeLogger::ScrubString(s), s);
}
