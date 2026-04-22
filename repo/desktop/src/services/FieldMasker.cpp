#include "shelterops/services/FieldMasker.h"

namespace shelterops::services {

std::string FieldMasker::MaskField(domain::UserRole role,
                                     const std::string& entity_type,
                                     const std::string& field_name,
                                     const std::string& value) {
    using namespace domain;

    MaskingRule rule = GetMaskingRule(role, entity_type, field_name);

    // Default-masking invariant: unknown fields for Auditor → Redact.
    if (rule == MaskingRule::None && role == UserRole::Auditor) {
        // Fields that are structurally known to carry PII get redacted.
        static const std::unordered_map<std::string, MaskingRule> kAuditorDefaults = {
            {"phone",        MaskingRule::Last4},
            {"owner_phone",  MaskingRule::Last4},
            {"email",        MaskingRule::DomainOnly},
            {"owner_email",  MaskingRule::DomainOnly},
            {"display_name", MaskingRule::InitialsOnly},
            {"address",      MaskingRule::Redact},
            {"actor_user_id", MaskingRule::Redact},
            {"description",   MaskingRule::Redact},
        };
        auto it = kAuditorDefaults.find(field_name);
        if (it != kAuditorDefaults.end()) {
            rule = it->second;
        }
    }

    return domain::MaskField(value, rule);
}

ViewModel FieldMasker::MaskViewModel(domain::UserRole role,
                                      const std::string& entity_type,
                                      const ViewModel& row) {
    ViewModel result;
    result.reserve(row.size());
    for (const auto& [field, value] : row) {
        result[field] = MaskField(role, entity_type, field, value);
    }
    return result;
}

} // namespace shelterops::services
