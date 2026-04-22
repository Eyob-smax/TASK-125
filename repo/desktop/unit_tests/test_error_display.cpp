#include <gtest/gtest.h>
#include "shelterops/shell/ErrorDisplay.h"

using namespace shelterops::shell;
using namespace shelterops::common;

TEST(ErrorDisplay, UnauthorizedMessage) {
    std::string msg = ErrorDisplay::UserMessage(ErrorCode::Unauthorized);
    EXPECT_NE(msg.find("sign in"), std::string::npos);
}

TEST(ErrorDisplay, ForbiddenMessage) {
    std::string msg = ErrorDisplay::UserMessage(ErrorCode::Forbidden);
    EXPECT_NE(msg.find("role"), std::string::npos);
}

TEST(ErrorDisplay, RateLimitedIncludesWaitTime) {
    std::string msg = ErrorDisplay::UserMessage(ErrorCode::RateLimited, 30);
    EXPECT_NE(msg.find("30"), std::string::npos);
}

TEST(ErrorDisplay, InternalErrorNoStackTrace) {
    std::string msg = ErrorDisplay::UserMessage(ErrorCode::Internal);
    EXPECT_EQ(msg.find("stack"), std::string::npos);
    EXPECT_EQ(msg.find("trace"), std::string::npos);
    EXPECT_EQ(msg.find("exception"), std::string::npos);
}

TEST(ErrorDisplay, FromEnvelope) {
    ErrorEnvelope env{ErrorCode::NotFound, "item 42 not found"};
    std::string msg = ErrorDisplay::UserMessage(env);
    EXPECT_FALSE(msg.empty());
}
