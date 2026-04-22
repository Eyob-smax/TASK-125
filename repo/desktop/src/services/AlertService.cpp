#include "shelterops/services/AlertService.h"
#include "shelterops/services/AuthorizationService.h"

namespace shelterops::services {

AlertService::AlertService(repositories::InventoryRepository& inventory,
                            AuditService& audit)
    : inventory_(inventory), audit_(audit) {}

ScanReport AlertService::Scan(int64_t now_unix,
                               const domain::AlertThreshold& thresholds) {
    ScanReport report;

    auto candidates = inventory_.BuildAlertCandidates(now_unix);
    auto triggers   = domain::EvaluateAlerts(candidates, thresholds, now_unix);

    for (const auto& t : triggers) {
        auto alert_type_str = [&]() -> std::string {
            switch (t.type) {
            case domain::AlertType::LowStock:      return "low_stock";
            case domain::AlertType::ExpiringSoon:  return "expiring_soon";
            case domain::AlertType::Expired:       return "expired";
            default:                                return "low_stock";
            }
        }();

        inventory_.InsertAlertState(t.item_id, alert_type_str, now_unix);
        report.new_alerts.push_back(t);
    }

    auto active = inventory_.ListActiveAlerts();
    report.still_active_count = static_cast<int>(active.size());

    return report;
}

std::optional<common::ErrorEnvelope> AlertService::AcknowledgeAlert(
    int64_t alert_id,
    const UserContext& user_ctx,
    int64_t now_unix) {
    if (auto denied = AuthorizationService::RequireAlertAcknowledge(user_ctx.role)) {
        return *denied;
    }
    inventory_.AcknowledgeAlert(alert_id, user_ctx.user_id, now_unix);
    audit_.RecordSystemEvent("ALERT_ACKNOWLEDGED",
        "Alert " + std::to_string(alert_id) + " acknowledged",
        now_unix);
    return std::nullopt;
}

std::vector<repositories::AlertStateRecord> AlertService::ListActive() const {
    return inventory_.ListActiveAlerts();
}

} // namespace shelterops::services
