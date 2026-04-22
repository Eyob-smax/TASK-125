#pragma once
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/repositories/SessionRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/LockoutPolicy.h"
#include "shelterops/services/UserContext.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include "shelterops/domain/Types.h"
#include <string>
#include <variant>

namespace shelterops::services {

struct SessionHandle {
    std::string session_id;
    int64_t     user_id       = 0;
    std::string role;
    int64_t     expires_at    = 0;
};

struct AuthError {
    std::string code;     // e.g. "INVALID_CREDENTIALS", "ACCOUNT_LOCKED"
    std::string message;
    int64_t     locked_until = 0;  // non-zero when code = "ACCOUNT_LOCKED"
};

template <typename T>
using AuthResult = std::variant<T, AuthError>;

// AuthService owns the login/logout/session-validation flow.
// Session lifetime: 12 hours (43200 seconds) by default; refreshed on activity.
// Account lockout is delegated to LockoutPolicy.
// No hardcoded users; first-run bootstrap must be performed interactively.
class AuthService {
public:
    static constexpr int64_t kSessionLifetimeSec   = 12 * 3600; // 12 hours
    static constexpr int64_t kInactivityTimeoutSec =  1 * 3600; //  1 hour

    AuthService(repositories::UserRepository&    user_repo,
                repositories::SessionRepository&  session_repo,
                AuditService&                    audit,
                infrastructure::CryptoHelper&    crypto);

    // Authenticate and create a new session.
    // device_fingerprint may be empty for GUI logins (not required for GUI path).
    AuthResult<SessionHandle> Login(const std::string& username,
                                     const std::string& password,
                                     const std::string& device_fingerprint,
                                     int64_t now_unix);

    // Invalidate an active session.
    void Logout(const std::string& session_id, int64_t now_unix);

    // Validate a session and return the user context.
    // Refreshes the expiry on success.
    AuthResult<UserContext> ValidateSession(const std::string& session_id,
                                             const std::string& device_fingerprint,
                                             int64_t now_unix);

    // Change password for an authenticated user. Requires old password.
    // Invalidates all other sessions on success.
    AuthError ChangePassword(int64_t user_id,
                              const std::string& old_password,
                              const std::string& new_password,
                              int64_t now_unix);

    // First-run only: register the initial Administrator account.
    // Returns false if any user already exists (prevents privilege escalation).
    bool CreateInitialAdmin(const std::string& username,
                             const std::string& password,
                             const std::string& display_name,
                             int64_t now_unix);

private:
    static domain::UserRole ParseRole(const std::string& role_string);

    repositories::UserRepository&    user_repo_;
    repositories::SessionRepository& session_repo_;
    AuditService&                    audit_;
    infrastructure::CryptoHelper&    crypto_;
};

} // namespace shelterops::services
