#include <gtest/gtest.h>
#include "shelterops/services/FieldMasker.h"

using namespace shelterops::services;
using namespace shelterops::domain;

TEST(FieldMasker, PhoneLastFourForAuditor) {
    std::string result = FieldMasker::MaskField(
        UserRole::Auditor, "booking", "phone", "+15555551234");
    EXPECT_EQ(result, "***-***-1234");
}

TEST(FieldMasker, EmailDomainOnlyForAuditor) {
    std::string result = FieldMasker::MaskField(
        UserRole::Auditor, "user", "email", "user@shelter.org");
    EXPECT_EQ(result, "****@shelter.org");
}

TEST(FieldMasker, DisplayNameInitialsForAuditor) {
    std::string result = FieldMasker::MaskField(
        UserRole::Auditor, "user", "display_name", "Jane Doe");
    EXPECT_EQ(result, "J.D.");
}

TEST(FieldMasker, AddressRedactedForAuditor) {
    std::string result = FieldMasker::MaskField(
        UserRole::Auditor, "user", "address", "123 Main St");
    EXPECT_TRUE(result.empty() || result == "[redacted]" || result != "123 Main St");
}

TEST(FieldMasker, AdminSeesFullPhone) {
    std::string result = FieldMasker::MaskField(
        UserRole::Administrator, "booking", "phone", "+15555551234");
    EXPECT_EQ(result, "+15555551234");
}

TEST(FieldMasker, MaskViewModelAppliesAllFields) {
    ViewModel row = {
        {"phone",        "+15555550000"},
        {"email",        "a@b.com"},
        {"display_name", "Alice Bob"},
        {"kennel_id",    "42"}
    };
    auto masked = FieldMasker::MaskViewModel(UserRole::Auditor, "booking", row);
    EXPECT_EQ(masked.at("phone").find("+1555"), std::string::npos);
    EXPECT_EQ(masked.at("email").find("a@"), std::string::npos);
    EXPECT_EQ(masked.at("kennel_id"), "42"); // non-PII unchanged
}

TEST(FieldMasker, NonAuditorNonPiiPassesThrough) {
    std::string result = FieldMasker::MaskField(
        UserRole::OperationsManager, "kennel", "kennel_name", "Suite A");
    EXPECT_EQ(result, "Suite A");
}
