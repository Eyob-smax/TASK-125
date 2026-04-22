#include "shelterops/services/AuthService.h"
#include "shelterops/common/Uuid.h"
#include <spdlog/spdlog.h>

namespace shelterops::services {

AuthService::AuthService(repositories::UserRepository&   user_repo,
                          repositories::SessionRepository& session_repo,
                          AuditService&                   audit,
                          infrastructure::CryptoHelper&   crypto)
    : user_repo_(user_repo)
    , session_repo_(session_repo)
    , audit_(audit)
    , crypto_(crypto) {}

domain::UserRole AuthService::ParseRole(const std::string& role_string) {
    if (role_string == "administrator")      return domain::UserRole::Administrator;
    if (role_string == "operations_manager") return domain::UserRole::OperationsManager;
    if (role_string == "inventory_clerk")    return domain::UserRole::InventoryClerk;
    return domain::UserRole::Auditor;
}

AuthResult<SessionHandle> AuthService::Login(
        const std::string& username,
        const std::string& password,
        const std::string& device_fingerprint,
        int64_t now_unix) {

    auto user_opt = user_repo_.FindByUsername(username);
    if (!user_opt.has_value()) {
        // Perform a dummy hash to resist timing-based username enumeration.
        infrastructure::CryptoHelper::HashPassword("__dummy__");
        audit_.RecordLoginFailure(username, "user not found", now_unix);
        return AuthError{"INVALID_CREDENTIALS", "Invalid username or password"};
    }

    const auto& user = *user_opt;

    if (!user.is_active) {
        return AuthError{"ACCOUNT_INACTIVE", "Account is disabled"};
    }

    if (LockoutPolicy::IsCurrentlyLocked(user.locked_until, now_unix)) {
        return AuthError{
            "ACCOUNT_LOCKED",
            "Account is temporarily locked",
            user.locked_until};
    }

    if (!infrastructure::CryptoHelper::VerifyPassword(password, user.password_hash)) {
        auto decision = LockoutPolicy::Evaluate(
            user.failed_login_attempts, user.locked_until, now_unix);
        user_repo_.RecordFailedLogin(
            user.user_id,
            decision.locked ? decision.lock_until : 0);
        audit_.RecordLoginFailure(username, "wrong password", now_unix);

        if (decision.locked) {
            return AuthError{
                "ACCOUNT_LOCKED",
                "Account locked after too many failed attempts",
                decision.lock_until};
        }
        return AuthError{"INVALID_CREDENTIALS", "Invalid username or password"};
    }

    // Successful authentication.
    user_repo_.ResetFailedLoginCount(user.user_id);
    user_repo_.UpdateLastLogin(user.user_id, now_unix);

    std::string session_id = common::GenerateUuidV4();
    // Sliding window starts at 1h; hard cap is 12h from login.
    int64_t expires_at          = now_unix + kInactivityTimeoutSec;
    int64_t absolute_expires_at = now_unix + kSessionLifetimeSec;

    repositories::SessionRecord rec;
    rec.session_id             = session_id;
    rec.user_id                = user.user_id;
    rec.created_at             = now_unix;
    rec.expires_at             = expires_at;
    rec.absolute_expires_at    = absolute_expires_at;
    rec.device_fingerprint     = device_fingerprint;
    session_repo_.Insert(rec);

    audit_.RecordLogin(user.user_id, user.role, session_id, now_unix);
    spdlog::info("Login: user_id={} role={}", user.user_id, user.role);

    return SessionHandle{session_id, user.user_id, user.role, expires_at};
}

void AuthService::Logout(const std::string& session_id, int64_t now_unix) {
    auto sess = session_repo_.FindById(session_id);
    if (sess.has_value()) {
        session_repo_.MarkInactive(session_id);
        audit_.RecordLogout(sess->user_id, session_id, now_unix);
    }
}

AuthResult<UserContext> AuthService::ValidateSession(
        const std::string& session_id,
        const std::string& device_fingerprint,
        int64_t now_unix) {

    auto sess = session_repo_.FindById(session_id);
    if (!sess.has_value() || !sess->is_active) {
        return AuthError{"UNAUTHORIZED", "Session not found or expired"};
    }

    // Check both the sliding inactivity window and the hard 12-hour absolute cap.
    const bool sliding_expired  = now_unix > sess->expires_at;
    const bool absolute_expired = sess->absolute_expires_at > 0 &&
                                  now_unix > sess->absolute_expires_at;
    if (sliding_expired || absolute_expired) {
        session_repo_.MarkInactive(session_id);
        return AuthError{"UNAUTHORIZED", "Session expired"};
    }

    // Device fingerprint check: only enforced when both sides have a value.
    if (!device_fingerprint.empty() && !sess->device_fingerprint.empty()) {
        if (device_fingerprint != sess->device_fingerprint) {
            return AuthError{"UNAUTHORIZED", "Device fingerprint mismatch"};
        }
    }

    auto user_opt = user_repo_.FindById(sess->user_id);
    if (!user_opt.has_value() || !user_opt->is_active) {
        return AuthError{"UNAUTHORIZED", "User account not found or inactive"};
    }

    // Refresh sliding window on activity, but never past the absolute 12-hour cap.
    int64_t refreshed_expires_at = now_unix + kInactivityTimeoutSec;
    if (sess->absolute_expires_at > 0 && refreshed_expires_at > sess->absolute_expires_at)
        refreshed_expires_at = sess->absolute_expires_at;
    session_repo_.UpdateActivity(session_id, refreshed_expires_at);

    UserContext ctx;
    ctx.user_id            = user_opt->user_id;
    ctx.username           = user_opt->username;
    ctx.display_name       = user_opt->display_name;
    ctx.role               = ParseRole(user_opt->role);
    ctx.role_string        = user_opt->role;
    ctx.session_id         = session_id;
    ctx.device_fingerprint = device_fingerprint;
    return ctx;
}

AuthError AuthService::ChangePassword(int64_t user_id,
                                        const std::string& old_password,
                                        const std::string& new_password,
                                        int64_t now_unix) {
    auto user_opt = user_repo_.FindById(user_id);
    if (!user_opt.has_value()) {
        return {"NOT_FOUND", "User not found"};
    }
    if (!infrastructure::CryptoHelper::VerifyPassword(
            old_password, user_opt->password_hash)) {
        audit_.RecordLoginFailure(user_opt->username,
                                   "password change: wrong old password",
                                   now_unix);
        return {"INVALID_CREDENTIALS", "Current password is incorrect"};
    }
    if (new_password.size() < 12) {
        return {"INVALID_INPUT", "New password must be at least 12 characters"};
    }

    std::string new_hash = infrastructure::CryptoHelper::HashPassword(new_password);
    user_repo_.UpdatePasswordHash(user_id, new_hash);
    user_repo_.ResetFailedLoginCount(user_id);
    session_repo_.ExpireAllForUser(user_id);

    audit_.RecordSystemEvent(
        "PASSWORD_CHANGED",
        "Password changed for user_id=" + std::to_string(user_id),
        now_unix);

    return {"", ""};
}

bool AuthService::CreateInitialAdmin(const std::string& username,
                                      const std::string& password,
                                      const std::string& display_name,
                                      int64_t now_unix) {
    if (!user_repo_.IsEmpty()) {
        spdlog::warn("CreateInitialAdmin called but users table is not empty — rejected");
        return false;
    }
    if (password.size() < 12) {
        return false;
    }
    try {
        std::string hash = infrastructure::CryptoHelper::HashPassword(password);
        repositories::NewUserParams p;
        p.username      = username;
        p.display_name  = display_name;
        p.password_hash = hash;
        p.role          = "administrator";
        int64_t new_id = user_repo_.Insert(p, now_unix);
        audit_.RecordSystemEvent(
            "INITIAL_ADMIN_CREATED",
            "Initial admin user_id=" + std::to_string(new_id),
            now_unix);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace shelterops::services
