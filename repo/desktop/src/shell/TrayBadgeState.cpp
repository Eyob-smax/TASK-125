#include "shelterops/shell/TrayBadgeState.h"

namespace shelterops::shell {

void TrayBadgeState::Update(
    const std::vector<repositories::AlertStateRecord>& active_alerts)
{
    low_stock_count = expiring_count = expired_count = 0;
    for (const auto& a : active_alerts) {
        if (a.acknowledged_at != 0) continue;
        if      (a.alert_type == "low_stock")     ++low_stock_count;
        else if (a.alert_type == "expiring_soon") ++expiring_count;
        else if (a.alert_type == "expired")       ++expired_count;
    }
}

} // namespace shelterops::shell
