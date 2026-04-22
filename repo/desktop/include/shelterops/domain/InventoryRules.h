#pragma once
#include "shelterops/domain/Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace shelterops::domain {

struct DailyUsage {
    int64_t period_date_unix = 0;
    int     quantity_used    = 0;
};

struct SerialValidationResult {
    bool        is_valid = false;
    std::string error_message;  // empty when is_valid == true
    int64_t     existing_item_id = 0; // set when duplicate; 0 otherwise
};

struct BarcodeValidationResult {
    bool        is_valid = false;
    std::string error_message;
};

// Validates serial number format and global uniqueness.
// is_globally_duplicate: caller passes true if the serial already exists in DB.
// existing_item_id: the item that owns the duplicate serial (0 if none).
SerialValidationResult ValidateSerial(
    const std::string& serial,
    bool               is_globally_duplicate,
    int64_t            existing_item_id = 0);

// Validates that a barcode string is non-empty and printable ASCII (USB wedge input).
BarcodeValidationResult ValidateBarcode(const std::string& barcode);

// Average units consumed per day over the given history window.
// Entries outside [now - window_days, now] are excluded.
// Returns 0.0 when history is empty (prevents division by zero in callers).
double ComputeAverageDailyUsage(
    const std::vector<DailyUsage>& history,
    int64_t                        now_unix,
    int                            window_days = 30);

// Days of stock remaining at the current average usage rate.
// Returns +INF when average_daily_usage == 0 (stock never runs out).
double ComputeDaysOfStock(int current_quantity, double average_daily_usage) noexcept;

// Returns true when the current stock covers fewer than threshold_days of usage.
bool IsLowStock(
    int    current_quantity,
    double average_daily_usage,
    int    threshold_days) noexcept;

// Returns true when expiration is within alert_days from now.
bool IsExpiringSoon(
    int64_t expiration_unix,
    int64_t now_unix,
    int     alert_days) noexcept;

// Returns true when expiration_unix <= now_unix.
bool IsExpired(int64_t expiration_unix, int64_t now_unix) noexcept;

} // namespace shelterops::domain
