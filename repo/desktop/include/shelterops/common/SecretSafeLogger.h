#pragma once
#include <string>
#include <spdlog/spdlog.h>

namespace shelterops::common {

// Thin wrapper around spdlog that scrubs secret-bearing JSON fields before
// forwarding log calls. Fields named password, password_hash, token,
// session_token, api_key, and msi_sha256 are replaced with "***REDACTED***".
// Bearer tokens in plain-string messages are also stripped.
//
// Use LogEvent() to log structured event payloads safely. For plain messages
// the standard spdlog macros are safe (no secret field names present).
class SecretSafeLogger {
public:
    enum class Level { Trace, Debug, Info, Warn, Error, Critical };

    // Scrubs `fields_json` (a JSON object string) and logs it at `level`.
    static void LogEvent(Level level,
                          const std::string& event_name,
                          const std::string& fields_json);

    // Scrubs a plain string (strips Bearer token pattern).
    static std::string ScrubString(const std::string& s);

    // Scrubs a JSON object string (replaces secret field values with
    // "***REDACTED***"). Operates on the raw string without full parsing
    // to remain dependency-light; handles one level of nesting.
    static std::string ScrubJson(const std::string& json);
};

} // namespace shelterops::common
