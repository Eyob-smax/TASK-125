#include "shelterops/domain/AlertRules.h"
#include "shelterops/domain/InventoryRules.h"
#include <sstream>

namespace shelterops::domain {

std::vector<AlertTrigger> EvaluateAlerts(
    const std::vector<AlertCandidate>& candidates,
    const AlertThreshold&              thresholds,
    int64_t                            now_unix)
{
    std::vector<AlertTrigger> triggers;
    const int expiring_days = thresholds.expiring_soon_days > 0
        ? thresholds.expiring_soon_days
        : thresholds.expiration_days;
    const bool use_qty_override = thresholds.low_stock_qty > 0;

    for (const auto& c : candidates) {
        if (c.expiration_unix > 0 && !c.already_alerted_expired) {
            if (IsExpired(c.expiration_unix, now_unix))
                triggers.push_back({c.item_id, AlertType::Expired, "Item has expired"});
        }

        if (c.expiration_unix > 0 && !c.already_alerted_expiring &&
            !IsExpired(c.expiration_unix, now_unix))
        {
            if (IsExpiringSoon(c.expiration_unix, now_unix, expiring_days)) {
                int64_t days_left =
                    (c.expiration_unix - now_unix) / 86400;
                std::ostringstream os;
                os << "Expires in " << days_left << " day(s)";
                triggers.push_back({c.item_id, AlertType::ExpiringSoon, os.str()});
            }
        }

        const bool low_stock = use_qty_override
            ? c.current_quantity < thresholds.low_stock_qty
            : IsLowStock(c.current_quantity, c.average_daily_usage, thresholds.low_stock_days);

        if (!c.already_alerted_low_stock && low_stock) {
            double days_remaining = ComputeDaysOfStock(
                c.current_quantity, c.average_daily_usage);
            std::ostringstream os;
            if (use_qty_override) {
                os << "Quantity " << c.current_quantity
                   << " is below threshold " << thresholds.low_stock_qty;
            } else {
                os << "Stock covers ~"
                   << static_cast<int>(days_remaining)
                   << " day(s) of usage (threshold: "
                   << thresholds.low_stock_days << " days)";
            }
            triggers.push_back({c.item_id, AlertType::LowStock, os.str()});
        }
    }
    return triggers;
}

} // namespace shelterops::domain
