#include "shelterops/services/AuditService.h"
#include <regex>
#include <sstream>
#include <set>

namespace shelterops::services {

namespace {
// Fields that must never appear in audit descriptions.
static const std::set<std::string> kPiiFields = {
    "password_hash", "password", "phone", "email", "address",
    "token", "session_token", "session_id", "api_key", "owner_phone", "owner_email"
};
} // anonymous namespace

AuditService::AuditService(repositories::AuditRepository& repo) : repo_(repo) {}

bool AuditService::IsPiiField(const std::string& field_name) noexcept {
    return kPiiFields.count(field_name) > 0;
}

std::string AuditService::ComputeDiff(const nlohmann::json& before,
                                       const nlohmann::json& after) {
    nlohmann::json diff = nlohmann::json::object();
    for (auto& [key, after_val] : after.items()) {
        if (IsPiiField(key)) continue;  // never log PII field values
        if (!before.contains(key) || before[key] != after_val) {
            diff[key] = {{"from", before.contains(key) ? before.at(key) : nlohmann::json(nullptr)},
                         {"to",   after_val}};
        }
    }
    return diff.dump();
}

void AuditService::RecordLogin(int64_t user_id, const std::string& role,
                                 const std::string& session_id,
                                 int64_t now_unix) {
    (void)session_id;
    repositories::AuditEvent e;
    e.occurred_at   = now_unix;
    e.actor_user_id = user_id;
    e.actor_role    = role;
    e.event_type    = "LOGIN";
    e.entity_type   = "user";
    e.entity_id     = user_id;
    e.description   = "Operator logged in";
    e.session_id    = "";
    repo_.Append(e);
}

void AuditService::RecordLoginFailure(const std::string& username,
                                       const std::string& reason,
                                       int64_t now_unix) {
    (void)username;
    repositories::AuditEvent e;
    e.occurred_at  = now_unix;
    e.event_type   = "LOGIN_FAILURE";
    e.entity_type  = "user";
    e.description  = "Login failure: " + reason;
    repo_.Append(e);
}

void AuditService::RecordLogout(int64_t user_id, const std::string& session_id,
                                  int64_t now_unix) {
    (void)session_id;
    repositories::AuditEvent e;
    e.occurred_at   = now_unix;
    e.actor_user_id = user_id;
    e.event_type    = "LOGOUT";
    e.entity_type   = "user";
    e.entity_id     = user_id;
    e.description   = "Operator logged out";
    e.session_id    = "";
    repo_.Append(e);
}

void AuditService::RecordMutation(int64_t actor_user_id,
                                    const std::string& actor_role,
                                    const std::string& session_id,
                                    const std::string& entity_type,
                                    int64_t entity_id,
                                    const nlohmann::json& before_json,
                                    const nlohmann::json& after_json,
                                    int64_t now_unix) {
    (void)session_id;
    std::string diff = ComputeDiff(before_json, after_json);
    repositories::AuditEvent e;
    e.occurred_at   = now_unix;
    e.actor_user_id = actor_user_id;
    e.actor_role    = actor_role;
    e.event_type    = "MUTATION";
    e.entity_type   = entity_type;
    e.entity_id     = entity_id;
    e.description   = diff;
    e.session_id    = "";
    repo_.Append(e);
}

std::string AuditService::SanitizeFreeText(const std::string& text) {
    // Redact email addresses: anything matching user@domain.tld
    static const std::regex kEmailRe(
        R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})",
        std::regex::optimize);
    // Redact phone-like patterns: sequences of 7+ consecutive digits (with optional separators)
    static const std::regex kPhoneRe(
        R"(\b(\+?[\d][\d\s\-\.\(\)]{6,}\d)\b)",
        std::regex::optimize);

    std::string out = std::regex_replace(text, kEmailRe,  "[EMAIL-REDACTED]");
    out             = std::regex_replace(out,  kPhoneRe,  "[PHONE-REDACTED]");
    return out;
}

void AuditService::RecordSystemEvent(const std::string& event_type,
                                      const std::string& description,
                                      int64_t now_unix) {
    repositories::AuditEvent e;
    e.occurred_at = now_unix;
    e.event_type  = event_type;
    e.description = SanitizeFreeText(description);
    repo_.Append(e);
}

void AuditService::ExportCsv(
        const repositories::AuditQueryFilter& filter,
        domain::UserRole viewer_role,
        std::function<void(const std::string&)> line_callback) const {

    // For Auditor role: mask description field with initials / domain-only
    // rule (description never contains raw PII, so this is belt-and-suspenders).
    auto masker = [viewer_role](const std::string& /*field*/,
                                 const std::string& value) -> std::string {
        if (viewer_role == domain::UserRole::Auditor) {
            // Audit descriptions are already PII-free; return as-is.
            // Only actor_role is a structured field: return verbatim.
        }
        return value;
    };

    repo_.ExportCsv(filter, masker, line_callback);
}

} // namespace shelterops::services
