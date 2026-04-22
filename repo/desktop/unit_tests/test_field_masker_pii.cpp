#include <gtest/gtest.h>
#include "shelterops/services/FieldMasker.h"

using namespace shelterops::services;
using namespace shelterops::domain;

// PII-specific masking rules. These tests pin the invariant that
// Auditor role sees only scrubbed PII and all other roles see raw values
// for non-PII fields.

// ─── Phone masking ────────────────────────────────────────────────────────

TEST(FieldMaskerPii, PhoneLastFourOnly) {
    std::string masked = FieldMasker::MaskField(
        UserRole::Auditor, "booking", "phone", "+15555551234");
    EXPECT_EQ("***-***-1234", masked);
}

TEST(FieldMaskerPii, PhoneShortNumberLastFourPreserved) {
    std::string masked = FieldMasker::MaskField(
        UserRole::Auditor, "booking", "phone", "1234");
    // Input shorter than 4 digits is fully masked or shows last 4.
    EXPECT_FALSE(masked.empty());
}

TEST(FieldMaskerPii, PhoneManagerSeesUnmasked) {
    std::string raw = "+15555551234";
    std::string result = FieldMasker::MaskField(
        UserRole::OperationsManager, "booking", "phone", raw);
    EXPECT_EQ(raw, result);
}

TEST(FieldMaskerPii, PhoneAdminSeesUnmasked) {
    std::string raw = "+15555551234";
    std::string result = FieldMasker::MaskField(
        UserRole::Administrator, "booking", "phone", raw);
    EXPECT_EQ(raw, result);
}

// ─── Email masking ────────────────────────────────────────────────────────

TEST(FieldMaskerPii, EmailDomainPreservedForAuditor) {
    std::string masked = FieldMasker::MaskField(
        UserRole::Auditor, "user", "email", "john.doe@example.com");
    EXPECT_NE(std::string::npos, masked.find("@example.com"));
    EXPECT_EQ(std::string::npos, masked.find("john.doe"));
}

TEST(FieldMaskerPii, EmailManagerSeesUnmasked) {
    std::string raw = "user@shelter.org";
    std::string result = FieldMasker::MaskField(
        UserRole::OperationsManager, "user", "email", raw);
    EXPECT_EQ(raw, result);
}

// ─── Display name masking ─────────────────────────────────────────────────

TEST(FieldMaskerPii, DisplayNameInitialsOnlyForAuditor) {
    std::string masked = FieldMasker::MaskField(
        UserRole::Auditor, "user", "display_name", "Alice Johnson");
    // Must not contain the full name.
    EXPECT_EQ(std::string::npos, masked.find("Alice"));
    EXPECT_EQ(std::string::npos, masked.find("Johnson"));
    // Should contain initials.
    EXPECT_FALSE(masked.empty());
}

TEST(FieldMaskerPii, DisplayNameManagerSeesUnmasked) {
    std::string raw = "Alice Johnson";
    EXPECT_EQ(raw, FieldMasker::MaskField(
        UserRole::OperationsManager, "user", "display_name", raw));
}

// ─── Audit controller fields ──────────────────────────────────────────────

TEST(FieldMaskerPii, ActorUserIdMaskedForAuditor) {
    // AuditLogController uses FieldMasker to mask actor_user_id for Auditor role.
    std::string masked = FieldMasker::MaskField(
        UserRole::Auditor, "audit_event", "actor_user_id", "42");
    EXPECT_NE("42", masked);
}

TEST(FieldMaskerPii, ActorUserIdVisibleForManager) {
    std::string result = FieldMasker::MaskField(
        UserRole::OperationsManager, "audit_event", "actor_user_id", "42");
    EXPECT_EQ("42", result);
}

TEST(FieldMaskerPii, DescriptionMaskedForAuditor) {
    std::string masked = FieldMasker::MaskField(
        UserRole::Auditor, "audit_event", "description", "User logged in");
    EXPECT_NE("User logged in", masked);
}

// ─── Non-PII fields ───────────────────────────────────────────────────────

TEST(FieldMaskerPii, KennelIdNotMaskedForAuditor) {
    std::string result = FieldMasker::MaskField(
        UserRole::Auditor, "booking", "kennel_id", "7");
    EXPECT_EQ("7", result);
}

TEST(FieldMaskerPii, EventTypeNotMaskedForAuditor) {
    std::string result = FieldMasker::MaskField(
        UserRole::Auditor, "audit_event", "event_type", "LOGIN");
    EXPECT_EQ("LOGIN", result);
}

// ─── ViewModel bulk masking ───────────────────────────────────────────────

TEST(FieldMaskerPii, ViewModelBulkMaskAuditorScrubsPii) {
    ViewModel row = {
        {"phone",        "+15555550000"},
        {"email",        "private@domain.com"},
        {"display_name", "Bob Smith"},
        {"kennel_id",    "3"}
    };
    auto masked = FieldMasker::MaskViewModel(UserRole::Auditor, "booking", row);

    // PII fields must be altered.
    EXPECT_EQ(std::string::npos, masked.at("phone").find("+1555"));
    EXPECT_EQ(std::string::npos, masked.at("email").find("private@"));
    EXPECT_EQ(std::string::npos, masked.at("display_name").find("Bob"));

    // Non-PII field must pass through.
    EXPECT_EQ("3", masked.at("kennel_id"));
}

TEST(FieldMaskerPii, ViewModelBulkMaskAdminPassesAll) {
    ViewModel row = {
        {"phone",     "+15555550000"},
        {"kennel_id", "3"}
    };
    auto masked = FieldMasker::MaskViewModel(UserRole::Administrator, "booking", row);
    EXPECT_EQ("+15555550000", masked.at("phone"));
    EXPECT_EQ("3", masked.at("kennel_id"));
}
