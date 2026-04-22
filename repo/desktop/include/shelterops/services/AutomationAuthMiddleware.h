#pragma once
#include "shelterops/infrastructure/RateLimiter.h"
#include "shelterops/common/ErrorEnvelope.h"
#include "shelterops/repositories/SessionRepository.h"
#include "shelterops/infrastructure/DeviceFingerprint.h"
#include <string>
#include <unordered_map>
#include <optional>

namespace shelterops::services {

using HeaderMap = std::unordered_map<std::string, std::string>;

struct AuthOutcome {
    bool        success = false;
    std::string session_id;
    std::string error_code;
    std::string error_message;
    int         http_status = 200;
};

struct RateLimitOutcome {
    bool allowed              = true;
    int  retry_after_seconds  = 0;
};

// Pure middleware functions — no HTTP server dependency.
// The actual server loop plugs these in; they are testable without it.
class AutomationAuthMiddleware {
public:
    static constexpr const char* kSessionTokenHeader     = "X-Session-Token";
    static constexpr const char* kDeviceFingerprintHeader = "X-Device-Fingerprint";

    // Validate session token and device fingerprint headers.
    // Returns AuthOutcome{success=true} on valid session, or an error outcome.
    static AuthOutcome VerifyHeaders(
        const HeaderMap&                        headers,
        const repositories::SessionRepository&  session_repo,
        int64_t now_unix);

    // Check rate limit for the session token.
    // Returns {allowed=true} if under the limit.
    static RateLimitOutcome ApplyRateLimit(
        const std::string&                      session_token,
        infrastructure::RateLimiter&            limiter);

    // Build a stable JSON error response body from an ErrorEnvelope.
    // Never includes tokens, passwords, or stack traces.
    static std::string BuildErrorResponse(
        const common::ErrorEnvelope& envelope);

    // Build the standard rate-limit error response including Retry-After.
    static std::string BuildRateLimitResponse(int retry_after_seconds);
};

} // namespace shelterops::services
