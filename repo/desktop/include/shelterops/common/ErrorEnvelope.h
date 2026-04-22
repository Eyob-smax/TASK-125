#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace shelterops::common {

// Error codes emitted across service, repository, and API layers.
// These map to HTTP status codes for the automation endpoint surface.
// See docs/api-spec.md §3 for the authoritative table.
enum class ErrorCode {
    // 400
    InvalidInput,
    SignatureInvalid,
    // 401
    Unauthorized,
    // 403
    Forbidden,
    ExportUnauthorized,
    // 404
    NotFound,
    ItemNotFound,
    // 409
    BookingConflict,
    // 429
    RateLimited,
    // 500
    Internal
};

// Returns the HTTP status code for a given ErrorCode.
int HttpStatusForCode(ErrorCode code) noexcept;

// Returns the machine-readable code string (e.g. "INVALID_INPUT").
std::string CodeString(ErrorCode code) noexcept;

struct ErrorEnvelope {
    ErrorCode   code;
    std::string message;       // user-facing; must not contain secrets

    // Returns the JSON error response shape:
    // {"ok":false,"error":{"code":"…","message":"…"}}
    // Never emits stack traces, tokens, decrypted fields, or secret material.
    nlohmann::json ToJson() const;

    // Returns a JSON success envelope:
    // {"ok":true,"data":<data>}
    static nlohmann::json SuccessJson(const nlohmann::json& data = {});
};

} // namespace shelterops::common
