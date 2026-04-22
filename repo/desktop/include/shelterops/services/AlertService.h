#pragma once
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/domain/AlertRules.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/common/ErrorEnvelope.h"
#include "shelterops/services/UserContext.h"
#include <vector>
#include <cstdint>
#include <optional>

namespace shelterops::services {

struct ScanReport {
    std::vector<domain::AlertTrigger> new_alerts;
    int                               still_active_count = 0;
};

class AlertService {
public:
    AlertService(repositories::InventoryRepository& inventory,
                 AuditService&                      audit);

    // Scan all items for low-stock and expiring-soon conditions.
    // Inserts new alert_states rows for triggered conditions not already active.
    ScanReport Scan(int64_t now_unix, const domain::AlertThreshold& thresholds);

    std::optional<common::ErrorEnvelope> AcknowledgeAlert(
        int64_t alert_id, const UserContext& user_ctx, int64_t now_unix);

    std::vector<repositories::AlertStateRecord> ListActive() const;

private:
    repositories::InventoryRepository& inventory_;
    AuditService&                      audit_;
};

} // namespace shelterops::services
