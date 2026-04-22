#include "shelterops/services/AuthorizationService.h"

namespace shelterops::services {

using namespace domain;
using Denied = AuthorizationService::DeniedReason;

static Denied Forbidden(const std::string& msg) {
    return common::ErrorEnvelope{common::ErrorCode::Forbidden, msg};
}

Denied AuthorizationService::RequireWrite(UserRole role) {
    if (!CanWrite(role))
        return Forbidden("Your role does not permit write operations");
    return std::nullopt;
}

Denied AuthorizationService::RequireAdminPanel(UserRole role) {
    if (!CanAccessAdminPanel(role))
        return Forbidden("Administrator access required");
    return std::nullopt;
}

Denied AuthorizationService::RequireBookingApproval(UserRole role) {
    if (!CanApproveBooking(role))
        return Forbidden("Operations Manager or Administrator role required to approve bookings");
    return std::nullopt;
}

Denied AuthorizationService::RequireAuditLogAccess(UserRole role) {
    // Formal audit log access is restricted to Administrator and Auditor.
    // OperationsManager may view events in the UI but not claim formal audit access.
    if (role != UserRole::Administrator && role != UserRole::Auditor)
        return Forbidden("Insufficient role for audit log access");
    return std::nullopt;
}

Denied AuthorizationService::RequireInventoryAccess(UserRole role) {
    if (!CanAccessInventoryLedger(role))
        return Forbidden("Inventory Clerk, Operations Manager, or Administrator required");
    return std::nullopt;
}

Denied AuthorizationService::RequireReportExport(UserRole role,
                                                    ReportType type) {
    if (!CanExportReport(role, type))
        return common::ErrorEnvelope{
            common::ErrorCode::ExportUnauthorized,
            "Your role does not permit exporting this report type"};
    return std::nullopt;
}

Denied AuthorizationService::RequireReportTrigger(UserRole role) {
    if (role != UserRole::Administrator &&
        role != UserRole::OperationsManager) {
        return Forbidden("Operations Manager or Administrator role required");
    }
    return std::nullopt;
}

Denied AuthorizationService::RequireAlertAcknowledge(UserRole role) {
    if (role != UserRole::Administrator &&
        role != UserRole::OperationsManager) {
        return Forbidden("Operations Manager or Administrator role required");
    }
    return std::nullopt;
}

bool AuthorizationService::CanDecryptField(UserRole role,
                                             const std::string& /*entity_type*/,
                                             const std::string& /*field_name*/) noexcept {
    // Auditors may never access decrypted field values, regardless of entity/field.
    return role != UserRole::Auditor;
}

bool AuthorizationService::CanReadBooking(UserRole role) noexcept {
    return role == UserRole::Administrator ||
           role == UserRole::OperationsManager;
}

AuthorizationService::DeniedReason AuthorizationService::RequireReportAccess(UserRole /*role*/) {
    // Any authenticated role may access reports (read or trigger).
    // Authentication is enforced by CommandDispatcher::Dispatch() before routing.
    return std::nullopt;
}

bool AuthorizationService::CanExportAuditLog(UserRole role) noexcept {
    return role == UserRole::Administrator ||
           role == UserRole::Auditor;
}

} // namespace shelterops::services
