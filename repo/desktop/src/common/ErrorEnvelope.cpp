#include "shelterops/common/ErrorEnvelope.h"

namespace shelterops::common {

int HttpStatusForCode(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidInput:      return 400;
    case ErrorCode::SignatureInvalid:  return 400;
    case ErrorCode::Unauthorized:      return 401;
    case ErrorCode::Forbidden:         return 403;
    case ErrorCode::ExportUnauthorized:return 403;
    case ErrorCode::NotFound:          return 404;
    case ErrorCode::ItemNotFound:      return 404;
    case ErrorCode::BookingConflict:   return 409;
    case ErrorCode::RateLimited:       return 429;
    case ErrorCode::Internal:          return 500;
    }
    return 500;
}

std::string CodeString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidInput:      return "INVALID_INPUT";
    case ErrorCode::SignatureInvalid:  return "SIGNATURE_INVALID";
    case ErrorCode::Unauthorized:      return "UNAUTHORIZED";
    case ErrorCode::Forbidden:         return "FORBIDDEN";
    case ErrorCode::ExportUnauthorized:return "EXPORT_UNAUTHORIZED";
    case ErrorCode::NotFound:          return "NOT_FOUND";
    case ErrorCode::ItemNotFound:      return "ITEM_NOT_FOUND";
    case ErrorCode::BookingConflict:   return "BOOKING_CONFLICT";
    case ErrorCode::RateLimited:       return "RATE_LIMITED";
    case ErrorCode::Internal:          return "INTERNAL";
    }
    return "INTERNAL";
}

nlohmann::json ErrorEnvelope::ToJson() const {
    return {
        {"ok", false},
        {"error", {
            {"code",    CodeString(code)},
            {"message", message}
        }}
    };
}

nlohmann::json ErrorEnvelope::SuccessJson(const nlohmann::json& data) {
    return {{"ok", true}, {"data", data}};
}

} // namespace shelterops::common
