#pragma once
#include "shelterops/domain/Types.h"
#include <vector>
#include <string>
#include <cstdint>

namespace shelterops::domain {

struct AlertCandidate {
    int64_t item_id               = 0;
    int     current_quantity      = 0;
    double  average_daily_usage   = 0.0;
    int64_t expiration_unix       = 0;    // 0 = no expiry
    bool    already_alerted_low_stock  = false;
    bool    already_alerted_expiring   = false;
    bool    already_alerted_expired    = false;
};

struct AlertTrigger {
    int64_t   item_id = 0;
    AlertType type;
    std::string reason;     // human-readable; does not contain PII
};

// Evaluates all candidates against the given thresholds and returns triggers
// for conditions not already alerted. Callers pass already_alerted_* = true
// for conditions that have an unacknowledged alert_states row.
std::vector<AlertTrigger> EvaluateAlerts(
    const std::vector<AlertCandidate>& candidates,
    const AlertThreshold&              thresholds,
    int64_t                            now_unix);

} // namespace shelterops::domain
