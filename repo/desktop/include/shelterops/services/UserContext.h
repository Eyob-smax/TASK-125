#pragma once
#include "shelterops/domain/Types.h"
#include <string>
#include <cstdint>

namespace shelterops::services {

// Canonical authenticated-user context.
// Created on successful login; passed to every service method that requires authz.
struct UserContext {
    int64_t             user_id            = 0;
    std::string         username;
    std::string         display_name;
    domain::UserRole    role               = domain::UserRole::Auditor;
    std::string         role_string;
    std::string         session_id;
    std::string         device_fingerprint;
};

} // namespace shelterops::services
