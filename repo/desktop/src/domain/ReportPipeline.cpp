#include "shelterops/domain/ReportPipeline.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace shelterops::domain {

double ComputeOccupancyRate(int occupied, int total) noexcept {
    if (total <= 0) return 0.0;
    return static_cast<double>(occupied) / static_cast<double>(total) * 100.0;
}

double ComputeAverageOccupancyRate(
    const std::vector<OccupancyPoint>& series) noexcept
{
    if (series.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& p : series)
        sum += ComputeOccupancyRate(p.occupied_kennels, p.total_kennels);
    return sum / static_cast<double>(series.size());
}

double ComputeTurnoverRate(
    const std::vector<TurnoverPoint>& series,
    double avg_occupied) noexcept
{
    if (avg_occupied <= 0.0) return 0.0;
    if (series.empty()) return 0.0;
    int64_t total_checkouts = 0;
    for (const auto& p : series) total_checkouts += p.checkouts;
    return static_cast<double>(total_checkouts) / avg_occupied;
}

double ComputeMaintenanceResponseHours(
    int64_t                created_at,
    std::optional<int64_t> first_action_at) noexcept
{
    if (!first_action_at.has_value())
        return std::numeric_limits<double>::quiet_NaN();
    int64_t seconds = *first_action_at - created_at;
    return static_cast<double>(seconds) / 3600.0;
}

double ComputeAvgMaintenanceResponseHours(
    const std::vector<MaintenanceResponsePoint>& tickets,
    int* unacknowledged_count) noexcept
{
    double sum     = 0.0;
    int    valid   = 0;
    int    unack   = 0;
    for (const auto& t : tickets) {
        double h = ComputeMaintenanceResponseHours(t.created_at, t.first_action_at);
        if (std::isnan(h)) { ++unack; continue; }
        if (h < 0.0) continue; // clock anomaly; skip
        sum += h;
        ++valid;
    }
    if (unacknowledged_count) *unacknowledged_count = unack;
    if (valid == 0) return std::numeric_limits<double>::quiet_NaN();
    return sum / static_cast<double>(valid);
}

std::vector<OverdueFeeAgeBucket> ComputeOverdueFeeDistribution(
    const std::vector<OverdueFeePoint>& fees,
    int64_t now_unix)
{
    // Buckets: [0,30], [31,60], [61,90], [91,180], [181,∞)
    std::vector<OverdueFeeAgeBucket> buckets = {
        { 0,   30, 0, 0},
        {31,   60, 0, 0},
        {61,   90, 0, 0},
        {91,  180, 0, 0},
        {181,  -1, 0, 0},
    };

    for (const auto& f : fees) {
        if (f.due_at > now_unix) continue; // not yet overdue
        int64_t age_seconds = now_unix - f.due_at;
        int     age_days    = static_cast<int>(age_seconds / 86400);

        for (auto& b : buckets) {
            if (age_days >= b.min_days_inclusive &&
                (b.max_days_inclusive == -1 || age_days <= b.max_days_inclusive))
            {
                ++b.count;
                b.total_cents += f.amount_cents;
                break;
            }
        }
    }
    return buckets;
}

std::vector<MetricDelta> ComputeVersionDelta(
    const std::vector<std::pair<std::string, double>>& before,
    const std::vector<std::pair<std::string, double>>& after)
{
    std::map<std::string, double> before_map(before.begin(), before.end());
    std::vector<MetricDelta> deltas;
    deltas.reserve(after.size());

    for (const auto& [name, val_after] : after) {
        auto it = before_map.find(name);
        if (it == before_map.end()) continue;

        double val_before  = it->second;
        double abs_delta   = val_after - val_before;
        double pct_delta   = (val_before != 0.0)
            ? (abs_delta / std::abs(val_before)) * 100.0
            : std::numeric_limits<double>::quiet_NaN();

        deltas.push_back({name, val_before, val_after, abs_delta, pct_delta});
    }
    return deltas;
}

} // namespace shelterops::domain
