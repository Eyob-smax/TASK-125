#include "shelterops/domain/InventoryRules.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

namespace shelterops::domain {

SerialValidationResult ValidateSerial(
    const std::string& serial,
    bool               is_globally_duplicate,
    int64_t            existing_item_id)
{
    if (serial.empty())
        return {false, "Serial number cannot be empty", 0};

    if (serial.size() > 64)
        return {false, "Serial number exceeds 64-character limit", 0};

    for (char c : serial) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
            return {false,
                "Serial number contains invalid character '" + std::string(1, c) +
                "' (allowed: alphanumeric, hyphen, underscore)", 0};
    }

    if (is_globally_duplicate)
        return {false,
            "Serial number is already registered to another item",
            existing_item_id};

    return {true, "", 0};
}

BarcodeValidationResult ValidateBarcode(const std::string& barcode) {
    if (barcode.empty())
        return {false, "Barcode cannot be empty"};

    for (char c : barcode) {
        if (static_cast<unsigned char>(c) < 0x20 ||
            static_cast<unsigned char>(c) > 0x7E)
            return {false, "Barcode contains non-printable character"};
    }

    if (barcode.size() > 128)
        return {false, "Barcode exceeds 128-character limit"};

    return {true, ""};
}

double ComputeAverageDailyUsage(
    const std::vector<DailyUsage>& history,
    int64_t now_unix,
    int     window_days)
{
    if (history.empty() || window_days <= 0) return 0.0;

    const int64_t window_start = now_unix - static_cast<int64_t>(window_days) * 86400LL;
    double total = 0.0;
    int    count = 0;
    for (const auto& h : history) {
        if (h.period_date_unix >= window_start && h.period_date_unix <= now_unix) {
            total += h.quantity_used;
            ++count;
        }
    }
    if (count == 0) return 0.0;
    return total / static_cast<double>(window_days);
}

double ComputeDaysOfStock(int current_quantity, double average_daily_usage) noexcept {
    if (average_daily_usage <= 0.0) return std::numeric_limits<double>::infinity();
    return static_cast<double>(current_quantity) / average_daily_usage;
}

bool IsLowStock(
    int    current_quantity,
    double average_daily_usage,
    int    threshold_days) noexcept
{
    double days = ComputeDaysOfStock(current_quantity, average_daily_usage);
    return std::isfinite(days) && days < static_cast<double>(threshold_days);
}

bool IsExpiringSoon(
    int64_t expiration_unix,
    int64_t now_unix,
    int     alert_days) noexcept
{
    if (expiration_unix <= 0) return false;
    int64_t alert_window_end = expiration_unix - static_cast<int64_t>(alert_days) * 86400LL;
    return now_unix >= alert_window_end && now_unix < expiration_unix;
}

bool IsExpired(int64_t expiration_unix, int64_t now_unix) noexcept {
    if (expiration_unix <= 0) return false;
    return now_unix >= expiration_unix;
}

} // namespace shelterops::domain
