#include "shelterops/domain/RolePermissions.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace shelterops::domain {

bool CanWrite(UserRole role) noexcept {
    return role != UserRole::Auditor;
}

bool CanAccessAdminPanel(UserRole role) noexcept {
    return role == UserRole::Administrator;
}

bool CanApproveBooking(UserRole role) noexcept {
    return role == UserRole::Administrator ||
           role == UserRole::OperationsManager;
}

bool CanExportReport(UserRole role, ReportType type) noexcept {
    if (role == UserRole::Auditor)
        return type == ReportType::AuditExport;
    if (role == UserRole::InventoryClerk)
        return type == ReportType::InventorySummary;
    return role == UserRole::Administrator ||
           role == UserRole::OperationsManager;
}

bool CanViewAuditLog(UserRole role) noexcept {
    return role == UserRole::Administrator ||
           role == UserRole::OperationsManager ||
           role == UserRole::Auditor;
}

bool CanManageUsers(UserRole role) noexcept {
    return role == UserRole::Administrator;
}

bool CanManagePolicies(UserRole role) noexcept {
    return role == UserRole::Administrator;
}

bool CanAccessInventoryLedger(UserRole role) noexcept {
    return role != UserRole::Auditor;
}

bool CanIssueInventory(UserRole role) noexcept {
    return role == UserRole::Administrator    ||
           role == UserRole::OperationsManager ||
           role == UserRole::InventoryClerk;
}

// ---------------------------------------------------------------------------
// Field masking — hardcoded policy mirrors masked_field_policies table defaults
// ---------------------------------------------------------------------------

MaskingRule GetMaskingRule(
    UserRole           role,
    const std::string& entity_type,
    const std::string& field_name) noexcept
{
    if (role != UserRole::Auditor) return MaskingRule::None;

    if (entity_type == "users") {
        if (field_name == "phone")        return MaskingRule::Last4;
        if (field_name == "email")        return MaskingRule::DomainOnly;
        if (field_name == "display_name") return MaskingRule::InitialsOnly;
    }
    if (entity_type == "bookings") {
        if (field_name == "guest_name")      return MaskingRule::InitialsOnly;
        if (field_name == "guest_phone_enc") return MaskingRule::Last4;
        if (field_name == "guest_email_enc") return MaskingRule::DomainOnly;
    }
    return MaskingRule::None;
}

std::string MaskPhone(const std::string& phone) {
    std::string digits;
    for (char c : phone)
        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    if (digits.size() < 4) return "***-***-****";
    return "***-***-" + digits.substr(digits.size() - 4);
}

std::string MaskEmail(const std::string& email) {
    auto at = email.find('@');
    if (at == std::string::npos) return "****";
    return "****@" + email.substr(at + 1);
}

std::string MaskToInitials(const std::string& display_name) {
    std::string result;
    bool next_is_initial = true;
    for (char c : display_name) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            next_is_initial = true;
        } else if (next_is_initial) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            result += '.';
            next_is_initial = false;
        }
    }
    return result.empty() ? "***" : result;
}

std::string MaskField(const std::string& value, MaskingRule rule) {
    switch (rule) {
    case MaskingRule::Last4:        return MaskPhone(value);
    case MaskingRule::InitialsOnly: return MaskToInitials(value);
    case MaskingRule::DomainOnly:   return MaskEmail(value);
    case MaskingRule::Redact:       return "****";
    case MaskingRule::None:         return value;
    }
    return value;
}

} // namespace shelterops::domain
