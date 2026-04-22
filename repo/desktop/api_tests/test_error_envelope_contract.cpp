// Guards doc drift: every ErrorCode declared in code must appear in
// docs/api-spec.md §3. Conversely every code in that table must be in the enum.
// This test encodes the canonical list; if a code is added/removed in either
// place, this test should be updated to match.
#include <gtest/gtest.h>
#include "shelterops/common/ErrorEnvelope.h"
#include <set>
#include <string>

using namespace shelterops::common;

TEST(ErrorEnvelopeContract, AllCodesHaveHttpStatus) {
    // Every code in the enum must map to a known HTTP status.
    static const std::vector<ErrorCode> all_codes = {
        ErrorCode::InvalidInput,
        ErrorCode::SignatureInvalid,
        ErrorCode::Unauthorized,
        ErrorCode::Forbidden,
        ErrorCode::ExportUnauthorized,
        ErrorCode::NotFound,
        ErrorCode::ItemNotFound,
        ErrorCode::BookingConflict,
        ErrorCode::RateLimited,
        ErrorCode::Internal
    };
    std::set<int> valid_statuses = {400, 401, 403, 404, 409, 429, 500};
    for (auto code : all_codes) {
        int status = HttpStatusForCode(code);
        EXPECT_TRUE(valid_statuses.count(status) > 0)
            << "Code " << CodeString(code) << " has unexpected HTTP status " << status;
    }
}

TEST(ErrorEnvelopeContract, AllCodesHaveNonEmptyString) {
    static const std::vector<ErrorCode> all_codes = {
        ErrorCode::InvalidInput, ErrorCode::SignatureInvalid,
        ErrorCode::Unauthorized, ErrorCode::Forbidden,
        ErrorCode::ExportUnauthorized, ErrorCode::NotFound,
        ErrorCode::ItemNotFound, ErrorCode::BookingConflict,
        ErrorCode::RateLimited, ErrorCode::Internal
    };
    for (auto code : all_codes) {
        EXPECT_FALSE(CodeString(code).empty())
            << "ErrorCode at index " << static_cast<int>(code) << " has empty string";
    }
}

TEST(ErrorEnvelopeContract, CodeStringsMatchApiSpec) {
    // These strings must match exactly the "Code" column in docs/api-spec.md §3.
    EXPECT_EQ(CodeString(ErrorCode::Unauthorized),       "UNAUTHORIZED");
    EXPECT_EQ(CodeString(ErrorCode::Forbidden),          "FORBIDDEN");
    EXPECT_EQ(CodeString(ErrorCode::NotFound),           "NOT_FOUND");
    EXPECT_EQ(CodeString(ErrorCode::RateLimited),        "RATE_LIMITED");
    EXPECT_EQ(CodeString(ErrorCode::InvalidInput),       "INVALID_INPUT");
    EXPECT_EQ(CodeString(ErrorCode::SignatureInvalid),   "SIGNATURE_INVALID");
    EXPECT_EQ(CodeString(ErrorCode::ItemNotFound),       "ITEM_NOT_FOUND");
    EXPECT_EQ(CodeString(ErrorCode::BookingConflict),    "BOOKING_CONFLICT");
    EXPECT_EQ(CodeString(ErrorCode::ExportUnauthorized), "EXPORT_UNAUTHORIZED");
    EXPECT_EQ(CodeString(ErrorCode::Internal),           "INTERNAL");
}
