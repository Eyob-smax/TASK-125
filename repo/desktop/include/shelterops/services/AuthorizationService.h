#pragma once
#include "shelterops/domain/RolePermissions.h"
#include "shelterops/domain/Types.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <string>
#include <optional>

namespace shelterops::services {

// Service-level authorization gating.
// Wraps domain::RolePermissions with service-level object-scope checks.
// Returns ErrorEnvelope{FORBIDDEN,…} when access is denied.
class AuthorizationService {
public:
    // Returns nullopt if access is granted; ErrorEnvelope if denied.
    using DeniedReason = std::optional<common::ErrorEnvelope>;

    static DeniedReason RequireWrite(domain::UserRole role);
    static DeniedReason RequireAdminPanel(domain::UserRole role);
    static DeniedReason RequireBookingApproval(domain::UserRole role);
    static DeniedReason RequireAuditLogAccess(domain::UserRole role);
    static DeniedReason RequireInventoryAccess(domain::UserRole role);
    static DeniedReason RequireReportExport(domain::UserRole role,
                                             domain::ReportType type);
    static DeniedReason RequireReportTrigger(domain::UserRole role);
    static DeniedReason RequireAlertAcknowledge(domain::UserRole role);

    // Any authenticated role may read/trigger reports.
    // Calling this makes the authorization intent explicit and testable.
    static DeniedReason RequireReportAccess(domain::UserRole role);

    // Object-scope: Auditors can never access decrypted fields.
    // Any other role may access decrypted fields for entities they can read.
    static bool CanDecryptField(domain::UserRole role,
                                  const std::string& entity_type,
                                  const std::string& field_name) noexcept;

    // Returns true if the role may read a booking record.
    static bool CanReadBooking(domain::UserRole role) noexcept;

    // Returns true if the role may export the audit log.
    static bool CanExportAuditLog(domain::UserRole role) noexcept;
};

} // namespace shelterops::services
