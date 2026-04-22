#include "shelterops/common/SecretSafeLogger.h"
#include <regex>

namespace shelterops::common {

namespace {

// Secret field names whose values must be redacted.
static const std::vector<std::string> kSecretFields = {
    "password", "password_hash", "token", "session_token",
    "api_key", "msi_sha256"
};

// Replace the value of a JSON key with "***REDACTED***".
// Handles both string values ("key":"value") and non-string values.
static std::string RedactJsonField(std::string json, const std::string& field) {
    // Match "field":  (optional space) then value (string or non-string)
    std::regex string_val(
        "\"" + field + "\"\\s*:\\s*\"[^\"]*\"",
        std::regex::icase);
    std::regex other_val(
        "\"" + field + "\"\\s*:\\s*[^,}\\]]+",
        std::regex::icase);
    std::string replacement_str = "\"" + field + "\": \"***REDACTED***\"";
    json = std::regex_replace(json, string_val, replacement_str);
    json = std::regex_replace(json, other_val,  replacement_str);
    return json;
}

} // anonymous namespace

std::string SecretSafeLogger::ScrubJson(const std::string& json) {
    std::string result = json;
    for (const auto& field : kSecretFields) {
        result = RedactJsonField(result, field);
    }
    return result;
}

std::string SecretSafeLogger::ScrubString(const std::string& s) {
    // Strip "Bearer <token>" patterns from plain strings.
    static const std::regex bearer_re(R"(Bearer\s+[A-Za-z0-9+/=._-]+)",
                                       std::regex::icase);
    return std::regex_replace(s, bearer_re, "Bearer ***REDACTED***");
}

void SecretSafeLogger::LogEvent(Level level,
                                  const std::string& event_name,
                                  const std::string& fields_json) {
    std::string scrubbed = ScrubJson(fields_json);
    std::string msg = "[" + event_name + "] " + scrubbed;

    switch (level) {
    case Level::Trace:    spdlog::trace(msg);    break;
    case Level::Debug:    spdlog::debug(msg);    break;
    case Level::Info:     spdlog::info(msg);     break;
    case Level::Warn:     spdlog::warn(msg);     break;
    case Level::Error:    spdlog::error(msg);    break;
    case Level::Critical: spdlog::critical(msg); break;
    }
}

} // namespace shelterops::common
