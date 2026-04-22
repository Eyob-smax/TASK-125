#pragma once
#include "shelterops/domain/Types.h"
#include <functional>
#include <string>
#include <variant>

namespace shelterops::domain {

enum class BookingEvent {
    SubmitForApproval,
    Approve,
    Reject,
    Activate,
    Complete,
    Cancel,
    NoShow
};

struct StateTransitionError {
    std::string code;     // e.g. "ILLEGAL_TRANSITION", "APPROVAL_ROLE_REQUIRED"
    std::string message;
};

// Returns the new status on success, or a StateTransitionError on failure.
using TransitionResult = std::variant<BookingStatus, StateTransitionError>;

// CanApproveCallback: returns true if the actor role may approve/reject a booking.
using CanApproveCallback = std::function<bool()>;

// Pure state machine — no I/O.
// Validates whether the transition from `current` via `event` is legal.
// For Approve/Reject events, calls `can_approve` to validate the actor role.
TransitionResult Transition(BookingStatus current,
                             BookingEvent event,
                             CanApproveCallback can_approve = nullptr);

// Returns the string name of the event for audit log descriptions.
std::string BookingEventName(BookingEvent event) noexcept;

// Returns the string name of the status for SQL/JSON serialization.
std::string BookingStatusName(BookingStatus status) noexcept;

// Parses a status string back to enum; throws std::invalid_argument if unknown.
BookingStatus ParseBookingStatus(const std::string& s);

} // namespace shelterops::domain
