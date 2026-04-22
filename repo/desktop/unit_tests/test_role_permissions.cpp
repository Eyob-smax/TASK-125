#include <gtest/gtest.h>
#include "shelterops/domain/RolePermissions.h"
#include "shelterops/domain/Types.h"

using namespace shelterops::domain;

// ---------------------------------------------------------------------------
// Write permission
// ---------------------------------------------------------------------------
TEST(WritePermission, AdministratorCanWrite)    { EXPECT_TRUE(CanWrite(UserRole::Administrator)); }
TEST(WritePermission, OperationsManagerCanWrite){ EXPECT_TRUE(CanWrite(UserRole::OperationsManager)); }
TEST(WritePermission, InventoryClerkCanWrite)   { EXPECT_TRUE(CanWrite(UserRole::InventoryClerk)); }
TEST(WritePermission, AuditorCannotWrite)       { EXPECT_FALSE(CanWrite(UserRole::Auditor)); }

// ---------------------------------------------------------------------------
// Admin panel access
// ---------------------------------------------------------------------------
TEST(AdminPanel, AdministratorAccess)        { EXPECT_TRUE(CanAccessAdminPanel(UserRole::Administrator)); }
TEST(AdminPanel, OperationsManagerNoAccess)  { EXPECT_FALSE(CanAccessAdminPanel(UserRole::OperationsManager)); }
TEST(AdminPanel, InventoryClerkNoAccess)     { EXPECT_FALSE(CanAccessAdminPanel(UserRole::InventoryClerk)); }
TEST(AdminPanel, AuditorNoAccess)            { EXPECT_FALSE(CanAccessAdminPanel(UserRole::Auditor)); }

// ---------------------------------------------------------------------------
// Booking approval
// ---------------------------------------------------------------------------
TEST(BookingApproval, AdministratorCanApprove)       { EXPECT_TRUE(CanApproveBooking(UserRole::Administrator)); }
TEST(BookingApproval, OperationsManagerCanApprove)   { EXPECT_TRUE(CanApproveBooking(UserRole::OperationsManager)); }
TEST(BookingApproval, InventoryClerkCannotApprove)   { EXPECT_FALSE(CanApproveBooking(UserRole::InventoryClerk)); }
TEST(BookingApproval, AuditorCannotApprove)          { EXPECT_FALSE(CanApproveBooking(UserRole::Auditor)); }

// ---------------------------------------------------------------------------
// Export permissions
// ---------------------------------------------------------------------------
TEST(ExportPermission, AdminCanExportAll) {
    EXPECT_TRUE(CanExportReport(UserRole::Administrator, ReportType::Occupancy));
    EXPECT_TRUE(CanExportReport(UserRole::Administrator, ReportType::AuditExport));
    EXPECT_TRUE(CanExportReport(UserRole::Administrator, ReportType::InventorySummary));
}

TEST(ExportPermission, InventoryClerkOnlyInventory) {
    EXPECT_TRUE(CanExportReport(UserRole::InventoryClerk, ReportType::InventorySummary));
    EXPECT_FALSE(CanExportReport(UserRole::InventoryClerk, ReportType::Occupancy));
    EXPECT_FALSE(CanExportReport(UserRole::InventoryClerk, ReportType::AuditExport));
}

TEST(ExportPermission, AuditorOnlyAuditExport) {
    EXPECT_TRUE(CanExportReport(UserRole::Auditor, ReportType::AuditExport));
    EXPECT_FALSE(CanExportReport(UserRole::Auditor, ReportType::Occupancy));
}

// ---------------------------------------------------------------------------
// Audit log access
// ---------------------------------------------------------------------------
TEST(AuditLog, AdministratorCanView)        { EXPECT_TRUE(CanViewAuditLog(UserRole::Administrator)); }
TEST(AuditLog, OperationsManagerCanView)    { EXPECT_TRUE(CanViewAuditLog(UserRole::OperationsManager)); }
TEST(AuditLog, AuditorCanView)              { EXPECT_TRUE(CanViewAuditLog(UserRole::Auditor)); }
TEST(AuditLog, InventoryClerkCannotView)    { EXPECT_FALSE(CanViewAuditLog(UserRole::InventoryClerk)); }

// ---------------------------------------------------------------------------
// Field masking rules
// ---------------------------------------------------------------------------
TEST(MaskingRule_, AuditorGetsLast4ForPhone) {
    EXPECT_EQ(GetMaskingRule(UserRole::Auditor, "users", "phone"), MaskingRule::Last4);
}

TEST(MaskingRule_, AuditorGetsDomainOnlyForEmail) {
    EXPECT_EQ(GetMaskingRule(UserRole::Auditor, "users", "email"), MaskingRule::DomainOnly);
}

TEST(MaskingRule_, AuditorGetsInitialsForDisplayName) {
    EXPECT_EQ(GetMaskingRule(UserRole::Auditor, "users", "display_name"), MaskingRule::InitialsOnly);
}

TEST(MaskingRule_, NonAuditorGetsNone) {
    EXPECT_EQ(GetMaskingRule(UserRole::Administrator, "users", "phone"), MaskingRule::None);
    EXPECT_EQ(GetMaskingRule(UserRole::InventoryClerk, "users", "email"), MaskingRule::None);
}

// ---------------------------------------------------------------------------
// MaskField / phone / email / initials
// ---------------------------------------------------------------------------
TEST(MaskPhone, Last4Correct) {
    EXPECT_EQ(MaskPhone("555-123-4567"), "***-***-4567");
    EXPECT_EQ(MaskPhone("5551234567"),   "***-***-4567");
}

TEST(MaskPhone, ShortPhoneHandled) {
    EXPECT_EQ(MaskPhone("123"), "***-***-****");
}

TEST(MaskEmail, DomainPreserved) {
    EXPECT_EQ(MaskEmail("jane.doe@shelter.org"), "****@shelter.org");
}

TEST(MaskEmail, NoAtSignReturnsRedact) {
    EXPECT_EQ(MaskEmail("notanemail"), "****");
}

TEST(MaskInitials, TwoWordName) {
    EXPECT_EQ(MaskToInitials("Jane Doe"), "J.D.");
}

TEST(MaskInitials, SingleWord) {
    EXPECT_EQ(MaskToInitials("Jane"), "J.");
}

TEST(MaskInitials, ThreeWordName) {
    EXPECT_EQ(MaskToInitials("John Michael Doe"), "J.M.D.");
}

TEST(MaskInitials, EmptyString) {
    EXPECT_EQ(MaskToInitials(""), "***");
}

TEST(MaskField, RedactReturnsStars) {
    EXPECT_EQ(MaskField("anything", MaskingRule::Redact), "****");
}

TEST(MaskField, NoneReturnOriginal) {
    EXPECT_EQ(MaskField("plaintext", MaskingRule::None), "plaintext");
}

// ---------------------------------------------------------------------------
// Inventory access
// ---------------------------------------------------------------------------
TEST(InventoryAccess, AuditorCannotAccessLedger) {
    EXPECT_FALSE(CanAccessInventoryLedger(UserRole::Auditor));
}

TEST(InventoryAccess, ClerkCanAccessLedger) {
    EXPECT_TRUE(CanAccessInventoryLedger(UserRole::InventoryClerk));
}

TEST(InventoryAccess, AuditorCannotIssue) {
    EXPECT_FALSE(CanIssueInventory(UserRole::Auditor));
}

TEST(InventoryAccess, ClerkCanIssue) {
    EXPECT_TRUE(CanIssueInventory(UserRole::InventoryClerk));
}
