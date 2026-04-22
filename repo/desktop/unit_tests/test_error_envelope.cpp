#include <gtest/gtest.h>
#include "shelterops/common/ErrorEnvelope.h"
#include <nlohmann/json.hpp>

using namespace shelterops::common;

TEST(ErrorEnvelope, HttpStatusMapping) {
    EXPECT_EQ(HttpStatusForCode(ErrorCode::InvalidInput),      400);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::SignatureInvalid),  400);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::Unauthorized),      401);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::Forbidden),         403);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::ExportUnauthorized),403);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::NotFound),          404);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::ItemNotFound),      404);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::BookingConflict),   409);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::RateLimited),       429);
    EXPECT_EQ(HttpStatusForCode(ErrorCode::Internal),          500);
}

TEST(ErrorEnvelope, CodeStringMapping) {
    EXPECT_EQ(CodeString(ErrorCode::InvalidInput),      "INVALID_INPUT");
    EXPECT_EQ(CodeString(ErrorCode::SignatureInvalid),  "SIGNATURE_INVALID");
    EXPECT_EQ(CodeString(ErrorCode::Unauthorized),      "UNAUTHORIZED");
    EXPECT_EQ(CodeString(ErrorCode::Forbidden),         "FORBIDDEN");
    EXPECT_EQ(CodeString(ErrorCode::ExportUnauthorized),"EXPORT_UNAUTHORIZED");
    EXPECT_EQ(CodeString(ErrorCode::NotFound),          "NOT_FOUND");
    EXPECT_EQ(CodeString(ErrorCode::ItemNotFound),      "ITEM_NOT_FOUND");
    EXPECT_EQ(CodeString(ErrorCode::BookingConflict),   "BOOKING_CONFLICT");
    EXPECT_EQ(CodeString(ErrorCode::RateLimited),       "RATE_LIMITED");
    EXPECT_EQ(CodeString(ErrorCode::Internal),          "INTERNAL");
}

TEST(ErrorEnvelope, JsonShape) {
    ErrorEnvelope env{ErrorCode::NotFound, "No such item"};
    auto j = env.ToJson();
    EXPECT_FALSE(j.at("ok").get<bool>());
    EXPECT_EQ(j.at("error").at("code").get<std::string>(), "NOT_FOUND");
    EXPECT_EQ(j.at("error").at("message").get<std::string>(), "No such item");
}

TEST(ErrorEnvelope, SuccessJsonShape) {
    auto j = ErrorEnvelope::SuccessJson({{"version", "1.0.0"}});
    EXPECT_TRUE(j.at("ok").get<bool>());
    EXPECT_EQ(j.at("data").at("version").get<std::string>(), "1.0.0");
}

TEST(ErrorEnvelope, JsonDoesNotContainSecretFields) {
    ErrorEnvelope env{ErrorCode::Internal, "An error occurred"};
    std::string json_str = env.ToJson().dump();
    EXPECT_EQ(json_str.find("password"), std::string::npos);
    EXPECT_EQ(json_str.find("token"),    std::string::npos);
    EXPECT_EQ(json_str.find("stack"),    std::string::npos);
}
