#include "shelterops/shell/ErrorDisplay.h"

namespace shelterops::shell {

std::string ErrorDisplay::UserMessage(const common::ErrorEnvelope& envelope) {
    return UserMessage(envelope.code);
}

std::string ErrorDisplay::UserMessage(common::ErrorCode code,
                                        int retry_after_seconds) {
    using EC = common::ErrorCode;
    switch (code) {
    case EC::Unauthorized:
        return "Please sign in again. Your session may have expired.";
    case EC::Forbidden:
        return "Your role does not permit this action.";
    case EC::ExportUnauthorized:
        return "Your role does not permit exporting this report.";
    case EC::NotFound:
    case EC::ItemNotFound:
        return "The requested record was not found.";
    case EC::BookingConflict:
        return "This kennel is already booked for the selected dates.";
    case EC::RateLimited:
        if (retry_after_seconds > 0) {
            return "Too many requests. Please wait " +
                   std::to_string(retry_after_seconds) + " seconds.";
        }
        return "Too many requests. Please wait a moment.";
    case EC::InvalidInput:
        return "One or more fields contain invalid input. Please check and retry.";
    case EC::SignatureInvalid:
        return "The update package signature is invalid. Import rejected.";
    case EC::Internal:
        return "An unexpected error occurred. Please contact your administrator.";
    }
    return "An unexpected error occurred.";
}

} // namespace shelterops::shell
