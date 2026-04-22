#pragma once
#include "shelterops/repositories/InventoryRepository.h"
#include <vector>

namespace shelterops::shell {

// Pure state: counts of unacknowledged alerts by type.
// Updated by the main loop; consumed by TrayManager and the status bar.
struct TrayBadgeState {
    int low_stock_count  = 0;
    int expiring_count   = 0;
    int expired_count    = 0;

    void Update(const std::vector<repositories::AlertStateRecord>& active_alerts);

    int  TotalBadgeCount() const noexcept {
        return low_stock_count + expiring_count + expired_count;
    }
    bool HasAlerts() const noexcept { return TotalBadgeCount() > 0; }
};

} // namespace shelterops::shell
