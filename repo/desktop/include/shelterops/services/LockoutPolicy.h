#pragma once
#include <cstdint>

namespace shelterops::services {

struct LockoutDecision {
    bool    locked             = false;
    int64_t lock_until         = 0;  // Unix timestamp; 0 = no lock
    int     remaining_attempts = 0;  // before next lock escalation
};

// Pure, stateless policy — injectable and testable without database.
//
// Escalation ladder:
//   1st breach (5 failures): locked_until = now + 15 min
//   2nd breach (5 more while locked): locked_until = now + 60 min
//   3rd+ breach: locked_until = now + 60 min (capped)
//
// A successful login resets the counter externally (UserRepository).
class LockoutPolicy {
public:
    static constexpr int   kThreshold     = 5;         // failures before lock
    static constexpr int64_t kFirstLockSec  = 15 * 60; // 15 minutes
    static constexpr int64_t kEscalateSec   = 60 * 60; // 1 hour

    // Evaluate whether the account should be locked after a failed attempt.
    //
    // failed_attempts:  current value BEFORE incrementing (UserRepository
    //                   does the increment after this call returns).
    // current_lock_until: 0 = not currently locked.
    // now_unix:          current Unix timestamp.
    static LockoutDecision Evaluate(int     failed_attempts,
                                     int64_t current_lock_until,
                                     int64_t now_unix) noexcept;

    // Returns true if the account is currently locked and the lock has not expired.
    static bool IsCurrentlyLocked(int64_t lock_until, int64_t now_unix) noexcept;
};

} // namespace shelterops::services
