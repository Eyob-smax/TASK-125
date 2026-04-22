#pragma once
#include "shelterops/domain/Types.h"
#include <string>

namespace shelterops::domain {

// ---------------------------------------------------------------------------
// Authorization checks (return true = action permitted)
// ---------------------------------------------------------------------------

bool CanWrite(UserRole role) noexcept;
bool CanAccessAdminPanel(UserRole role) noexcept;
bool CanApproveBooking(UserRole role) noexcept;
bool CanExportReport(UserRole role, ReportType type) noexcept;
bool CanViewAuditLog(UserRole role) noexcept;
bool CanManageUsers(UserRole role) noexcept;
bool CanManagePolicies(UserRole role) noexcept;
bool CanAccessInventoryLedger(UserRole role) noexcept;
bool CanIssueInventory(UserRole role) noexcept;

// ---------------------------------------------------------------------------
// Field-level masking
// ---------------------------------------------------------------------------

// Returns the masking rule that applies for the given (entity_type, field, role).
// Returns MaskingRule::None when no masking policy applies.
MaskingRule GetMaskingRule(
    UserRole           role,
    const std::string& entity_type,
    const std::string& field_name) noexcept;

// Applies a masking rule to a plaintext field value.
// The returned string is safe to display in the UI for the given role.
std::string MaskField(const std::string& value, MaskingRule rule);

// Convenience: mask a phone number to ***-***-XXXX (last 4 digits).
std::string MaskPhone(const std::string& phone);

// Convenience: mask an email to ****@domain.com (domain part only).
std::string MaskEmail(const std::string& email);

// Convenience: reduce a display name to initials (e.g. "Jane Doe" → "J.D.").
std::string MaskToInitials(const std::string& display_name);

} // namespace shelterops::domain
