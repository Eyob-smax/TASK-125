#pragma once
#include "shelterops/services/AuthService.h"
#include <optional>
#include <mutex>
#include <functional>

namespace shelterops::shell {

// Thread-safe holder for the authenticated user context.
// Set once per successful login; cleared on logout.
// Consulted by controllers before any privileged operation.
class SessionContext {
public:
    SessionContext() = default;

    void Set(const services::UserContext& ctx);
    void Clear();

    bool IsAuthenticated() const;

    // Returns a copy of the current UserContext.
    // Check IsAuthenticated() before calling.
    services::UserContext Get() const;

    // Convenience: returns the current role, or Auditor (most restrictive) if not set.
    domain::UserRole CurrentRole() const noexcept;

private:
    mutable std::mutex mu_;
    std::optional<services::UserContext> ctx_;
};

} // namespace shelterops::shell
