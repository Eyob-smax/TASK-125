#include <gtest/gtest.h>
#include "shelterops/services/AuthorizationService.h"

using namespace shelterops::services;
using namespace shelterops::domain;
using namespace shelterops::common;

TEST(AuthorizationService, AdminCanWrite) {
    EXPECT_FALSE(AuthorizationService::RequireWrite(UserRole::Administrator).has_value());
}
TEST(AuthorizationService, AuditorCannotWrite) {
    EXPECT_TRUE(AuthorizationService::RequireWrite(UserRole::Auditor).has_value());
}
TEST(AuthorizationService, InventoryClerkCannotWrite_AdminPanel) {
    EXPECT_TRUE(AuthorizationService::RequireAdminPanel(UserRole::InventoryClerk).has_value());
}
TEST(AuthorizationService, AdminCanAccessAdminPanel) {
    EXPECT_FALSE(AuthorizationService::RequireAdminPanel(UserRole::Administrator).has_value());
}
TEST(AuthorizationService, OperationsManagerCanApproveBooking) {
    EXPECT_FALSE(AuthorizationService::RequireBookingApproval(UserRole::OperationsManager).has_value());
}
TEST(AuthorizationService, InventoryClerkCannotApproveBooking) {
    EXPECT_TRUE(AuthorizationService::RequireBookingApproval(UserRole::InventoryClerk).has_value());
}
TEST(AuthorizationService, AuditorCanViewAuditLog) {
    EXPECT_FALSE(AuthorizationService::RequireAuditLogAccess(UserRole::Auditor).has_value());
}
TEST(AuthorizationService, OperationsManagerCannotAccessAuditLog) {
    EXPECT_TRUE(AuthorizationService::RequireAuditLogAccess(UserRole::OperationsManager).has_value());
}
TEST(AuthorizationService, AuditorCannotDecryptAnyField) {
    EXPECT_FALSE(AuthorizationService::CanDecryptField(
        UserRole::Auditor, "booking", "owner_phone"));
    EXPECT_FALSE(AuthorizationService::CanDecryptField(
        UserRole::Auditor, "user", "email"));
}
TEST(AuthorizationService, AdminCanDecryptField) {
    EXPECT_TRUE(AuthorizationService::CanDecryptField(
        UserRole::Administrator, "booking", "owner_phone"));
}
TEST(AuthorizationService, ForbiddenEnvelopeHasCorrectCode) {
    auto denied = AuthorizationService::RequireAdminPanel(UserRole::Auditor);
    ASSERT_TRUE(denied.has_value());
    EXPECT_EQ(denied->code, ErrorCode::Forbidden);
}
TEST(AuthorizationService, AdminCanExportAuditLog) {
    EXPECT_TRUE(AuthorizationService::CanExportAuditLog(UserRole::Administrator));
}
TEST(AuthorizationService, OperationsManagerCannotExportAuditLog) {
    EXPECT_FALSE(AuthorizationService::CanExportAuditLog(UserRole::OperationsManager));
}
