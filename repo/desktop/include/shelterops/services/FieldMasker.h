#pragma once
#include "shelterops/domain/RolePermissions.h"
#include "shelterops/domain/Types.h"
#include <string>
#include <unordered_map>

namespace shelterops::services {

using ViewModel = std::unordered_map<std::string, std::string>;

// Applies field-level masking to a view-model row before the controller
// passes it to the UI layer. Every controller must call MaskViewModel
// before populating view-model structs for the UI.
//
// Default masking invariant:
//   - Unknown (entity, field, role) for Auditor → Redact
//   - Unknown (entity, field, role) for all other roles → pass through
class FieldMasker {
public:
    // Apply masking policies to every field in `row` for the given role.
    static ViewModel MaskViewModel(domain::UserRole role,
                                    const std::string& entity_type,
                                    const ViewModel& row);

    // Apply masking to a single field value.
    // Returns the masked or original string depending on policy.
    static std::string MaskField(domain::UserRole role,
                                   const std::string& entity_type,
                                   const std::string& field_name,
                                   const std::string& value);
};

} // namespace shelterops::services
