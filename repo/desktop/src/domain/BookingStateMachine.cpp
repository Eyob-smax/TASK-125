#include "shelterops/domain/BookingStateMachine.h"
#include <stdexcept>

namespace shelterops::domain {

TransitionResult Transition(BookingStatus current,
                             BookingEvent event,
                             CanApproveCallback can_approve) {
    // Legal transition table:
    // Pending   + SubmitForApproval → Pending (stays, approval row created by service)
    // Pending   + Approve           → Approved  (requires CanApprove)
    // Pending   + Reject            → Cancelled  (requires CanApprove)
    // Pending   + Cancel            → Cancelled
    // Approved  + Activate          → Active
    // Approved  + Reject            → Cancelled  (requires CanApprove)
    // Approved  + Cancel            → Cancelled
    // Approved  + NoShow            → NoShow
    // Active    + Complete          → Completed
    // Active    + Cancel            → Cancelled

    auto approval_check = [&]() -> std::optional<StateTransitionError> {
        if (can_approve && !can_approve()) {
            return StateTransitionError{
                "APPROVAL_ROLE_REQUIRED",
                "Role does not have permission to approve or reject bookings"
            };
        }
        return std::nullopt;
    };

    switch (current) {
    case BookingStatus::Pending:
        switch (event) {
        case BookingEvent::SubmitForApproval:
            return BookingStatus::Pending;
        case BookingEvent::Approve: {
            if (auto e = approval_check()) return *e;
            return BookingStatus::Approved;
        }
        case BookingEvent::Reject: {
            if (auto e = approval_check()) return *e;
            return BookingStatus::Cancelled;
        }
        case BookingEvent::Cancel:
            return BookingStatus::Cancelled;
        default:
            break;
        }
        break;

    case BookingStatus::Approved:
        switch (event) {
        case BookingEvent::Activate:
            return BookingStatus::Active;
        case BookingEvent::Reject: {
            if (auto e = approval_check()) return *e;
            return BookingStatus::Cancelled;
        }
        case BookingEvent::Cancel:
            return BookingStatus::Cancelled;
        case BookingEvent::NoShow:
            return BookingStatus::NoShow;
        default:
            break;
        }
        break;

    case BookingStatus::Active:
        switch (event) {
        case BookingEvent::Complete:
            return BookingStatus::Completed;
        case BookingEvent::Cancel:
            return BookingStatus::Cancelled;
        default:
            break;
        }
        break;

    case BookingStatus::Completed:
    case BookingStatus::Cancelled:
    case BookingStatus::NoShow:
        // Terminal states — no transitions allowed.
        break;
    }

    return StateTransitionError{
        "ILLEGAL_TRANSITION",
        "Transition from " + BookingStatusName(current) +
        " via " + BookingEventName(event) + " is not permitted"
    };
}

std::string BookingEventName(BookingEvent event) noexcept {
    switch (event) {
    case BookingEvent::SubmitForApproval: return "SUBMITTED";
    case BookingEvent::Approve:           return "APPROVED";
    case BookingEvent::Reject:            return "REJECTED";
    case BookingEvent::Activate:          return "ACTIVATED";
    case BookingEvent::Complete:          return "COMPLETED";
    case BookingEvent::Cancel:            return "CANCELLED";
    case BookingEvent::NoShow:            return "NO_SHOW";
    default:                               return "UNKNOWN";
    }
}

std::string BookingStatusName(BookingStatus status) noexcept {
    switch (status) {
    case BookingStatus::Pending:   return "pending";
    case BookingStatus::Approved:  return "approved";
    case BookingStatus::Active:    return "active";
    case BookingStatus::Completed: return "completed";
    case BookingStatus::Cancelled: return "cancelled";
    case BookingStatus::NoShow:    return "no_show";
    default:                        return "unknown";
    }
}

BookingStatus ParseBookingStatus(const std::string& s) {
    if (s == "pending")   return BookingStatus::Pending;
    if (s == "approved")  return BookingStatus::Approved;
    if (s == "active")    return BookingStatus::Active;
    if (s == "completed") return BookingStatus::Completed;
    if (s == "cancelled") return BookingStatus::Cancelled;
    if (s == "no_show")   return BookingStatus::NoShow;
    throw std::invalid_argument("Unknown booking status: " + s);
}

} // namespace shelterops::domain
