#include "shelterops/services/AutomationAuthMiddleware.h"
#include <nlohmann/json.hpp>

namespace shelterops::services {

AuthOutcome AutomationAuthMiddleware::VerifyHeaders(
        const HeaderMap& headers,
        const repositories::SessionRepository& session_repo,
        int64_t now_unix) {

    auto token_it = headers.find(kSessionTokenHeader);
    if (token_it == headers.end() || token_it->second.empty()) {
        return {false, "", "UNAUTHORIZED",
                "Missing X-Session-Token header", 401};
    }

    const std::string& token = token_it->second;
    auto session_opt = session_repo.FindById(token);

    if (!session_opt.has_value() || !session_opt->is_active) {
        return {false, "", "UNAUTHORIZED",
                "Session not found or inactive", 401};
    }

    if (now_unix > session_opt->expires_at) {
        return {false, "", "UNAUTHORIZED",
                "Session expired", 401};
    }

    // Device fingerprint is required for all automation requests (spec §2.2).
    auto fp_it = headers.find(kDeviceFingerprintHeader);
    if (fp_it == headers.end() || fp_it->second.empty()) {
        return {false, "", "UNAUTHORIZED",
                "Missing X-Device-Fingerprint header", 401};
    }

    const std::string& provided_fp = fp_it->second;
    // Fingerprint must always match the session record. Sessions without a stored
    // fingerprint are rejected — they predate the binding requirement.
    if (provided_fp != session_opt->device_fingerprint) {
        return {false, "", "UNAUTHORIZED",
                "Device fingerprint mismatch", 401};
    }

    return {true, token, "", "", 200};
}

RateLimitOutcome AutomationAuthMiddleware::ApplyRateLimit(
        const std::string& session_token,
        infrastructure::RateLimiter& limiter) {
    auto result = limiter.TryAcquire(session_token);
    return {result.allowed, result.retry_after_seconds};
}

std::string AutomationAuthMiddleware::BuildErrorResponse(
        const common::ErrorEnvelope& envelope) {
    return envelope.ToJson().dump();
}

std::string AutomationAuthMiddleware::BuildRateLimitResponse(
        int retry_after_seconds) {
    common::ErrorEnvelope env{
        common::ErrorCode::RateLimited,
        "Too many requests. Retry after " +
            std::to_string(retry_after_seconds) + " seconds."
    };
    nlohmann::json j = env.ToJson();
    j["retry_after"] = retry_after_seconds;
    return j.dump();
}

} // namespace shelterops::services
