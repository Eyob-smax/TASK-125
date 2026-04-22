#pragma once
#include "shelterops/domain/Types.h"
#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <utility>

namespace shelterops::domain {

struct OccupancyPoint {
    int64_t date_unix        = 0;
    int     total_kennels    = 0;
    int     occupied_kennels = 0;
};

struct TurnoverPoint {
    int64_t date_unix    = 0;
    int     checkouts    = 0;
    int     new_checkins = 0;
};

struct MaintenanceResponsePoint {
    int64_t ticket_id    = 0;
    int64_t created_at   = 0;
    std::optional<int64_t> first_action_at;
    std::optional<int64_t> resolved_at;
};

struct OverdueFeePoint {
    int64_t booking_id   = 0;
    int64_t amount_cents = 0;
    int64_t due_at       = 0;
};

// Occupancy rate as a percentage (0–100).
// Returns 0.0 when total_kennels <= 0 (safe denominator guard).
double ComputeOccupancyRate(int occupied_kennels, int total_kennels) noexcept;

// Average occupancy rate across the series.
double ComputeAverageOccupancyRate(
    const std::vector<OccupancyPoint>& series) noexcept;

// Kennel turnover rate: total checkouts / average occupied count.
// avg_occupied must be > 0; returns 0.0 if avg_occupied <= 0.
double ComputeTurnoverRate(
    const std::vector<TurnoverPoint>& series,
    double avg_occupied) noexcept;

// Maintenance response time in decimal hours for a single ticket.
// Returns quiet_NaN() when first_action_at is absent (ticket unacknowledged).
double ComputeMaintenanceResponseHours(
    int64_t                created_at,
    std::optional<int64_t> first_action_at) noexcept;

// Average response time in hours across a collection of tickets.
// NaN entries (unacknowledged tickets) are excluded from the average.
// unacknowledged_count is set to the number of tickets with no first_action_at.
double ComputeAvgMaintenanceResponseHours(
    const std::vector<MaintenanceResponsePoint>& tickets,
    int*                                          unacknowledged_count = nullptr) noexcept;

// Groups unpaid boarding fees by age-from-due (days since due_at).
// Buckets: 0–30, 31–60, 61–90, 91–180, 181+ days.
// Fees with due_at > now_unix are excluded (not yet overdue).
std::vector<OverdueFeeAgeBucket> ComputeOverdueFeeDistribution(
    const std::vector<OverdueFeePoint>& fees,
    int64_t                             now_unix);

// Computes metric-level deltas between two report run snapshots.
// before and after are (metric_name, value) pairs. Only metrics present
// in both sets produce a delta entry. delta_pct is NaN when value_before == 0.
std::vector<MetricDelta> ComputeVersionDelta(
    const std::vector<std::pair<std::string, double>>& before,
    const std::vector<std::pair<std::string, double>>& after);

} // namespace shelterops::domain
