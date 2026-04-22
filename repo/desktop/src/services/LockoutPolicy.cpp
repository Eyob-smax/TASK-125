#include "shelterops/services/LockoutPolicy.h"

namespace shelterops::services {

bool LockoutPolicy::IsCurrentlyLocked(int64_t lock_until,
                                        int64_t now_unix) noexcept {
    return lock_until > 0 && now_unix < lock_until;
}

LockoutDecision LockoutPolicy::Evaluate(int     failed_attempts,
                                          int64_t current_lock_until,
                                          int64_t now_unix) noexcept {
    // failed_attempts is the count BEFORE the current failure, so after
    // incrementing it will be failed_attempts+1. Lock when that crosses
    // a multiple of kThreshold.
    int next_count = failed_attempts + 1;

    if (next_count % kThreshold != 0) {
        // Not yet at a lock threshold.
        int remaining = kThreshold - (next_count % kThreshold);
        return {false, 0, remaining};
    }

    // Determine lock duration based on escalation.
    // If the account was already locked when this failure occurred → escalate.
    int64_t duration = (current_lock_until > 0 && now_unix < current_lock_until)
                       ? kEscalateSec
                       : kFirstLockSec;

    int64_t lock_until = now_unix + duration;
    return {true, lock_until, 0};
}

} // namespace shelterops::services
