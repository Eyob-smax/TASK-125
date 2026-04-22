#pragma once
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/domain/RolePermissions.h"
#include <string>
#include <nlohmann/json.hpp>
#include <regex>

namespace shelterops::services {

// High-level helpers for writing audit events.
// No PII, decrypted fields, or passwords are ever written to audit_events.
// Field-level diff computes changed keys between before/after JSON objects,
// then strips any field governed by a Redact masking rule before persistence.
class AuditService {
public:
    explicit AuditService(repositories::AuditRepository& repo);

    void RecordLogin(int64_t user_id, const std::string& role,
                     const std::string& session_id, int64_t now_unix);

    void RecordLoginFailure(const std::string& username,
                             const std::string& reason, int64_t now_unix);

    void RecordLogout(int64_t user_id, const std::string& session_id,
                      int64_t now_unix);

    // Computes field-level diff between before_json and after_json,
    // masks PII fields per masking policy, then appends one audit event.
    void RecordMutation(int64_t actor_user_id,
                         const std::string& actor_role,
                         const std::string& session_id,
                         const std::string& entity_type,
                         int64_t entity_id,
                         const nlohmann::json& before_json,
                         const nlohmann::json& after_json,
                         int64_t now_unix);

    void RecordSystemEvent(const std::string& event_type,
                            const std::string& description,
                            int64_t now_unix);

    // Export audit events as CSV lines with masking applied per viewer role.
    void ExportCsv(const repositories::AuditQueryFilter& filter,
                   domain::UserRole viewer_role,
                   std::function<void(const std::string&)> line_callback) const;

private:
    static std::string ComputeDiff(const nlohmann::json& before,
                                    const nlohmann::json& after);
    static bool IsPiiField(const std::string& field_name) noexcept;
    // Redacts email addresses and phone-like patterns from free-text descriptions.
    static std::string SanitizeFreeText(const std::string& text);

    repositories::AuditRepository& repo_;
};

} // namespace shelterops::services
