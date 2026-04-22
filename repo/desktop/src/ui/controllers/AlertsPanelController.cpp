#include "shelterops/ui/controllers/AlertsPanelController.h"
#include <spdlog/spdlog.h>

namespace shelterops::ui::controllers {

AlertsPanelController::AlertsPanelController(
    services::AlertService& alert_svc,
    shell::TrayBadgeState&  tray_badge)
    : alert_svc_(alert_svc), tray_badge_(tray_badge)
{}

void AlertsPanelController::Refresh(
    const domain::AlertThreshold& thresholds, int64_t now_unix)
{
    state_ = AlertsPanelState::Loading;
    auto report = alert_svc_.Scan(now_unix, thresholds);
    alerts_ = alert_svc_.ListActive();
    tray_badge_.Update(alerts_);
    state_    = AlertsPanelState::Loaded;
    is_dirty_ = false;
    spdlog::debug("AlertsPanelController: {} active alerts, {} new",
                  alerts_.size(), report.new_alerts.size());
}

bool AlertsPanelController::AcknowledgeAlert(
    int64_t alert_id, const services::UserContext& ctx, int64_t now_unix)
{
    if (ctx.role == domain::UserRole::Auditor) {
        last_error_ = { common::ErrorCode::Forbidden,
                        "Auditors cannot acknowledge alerts." };
        return false;
    }

    state_ = AlertsPanelState::Acknowledging;
    if (auto err = alert_svc_.AcknowledgeAlert(alert_id, ctx, now_unix)) {
        last_error_ = *err;
        state_ = AlertsPanelState::Error;
        return false;
    }

    // Remove from local list immediately for responsive UI.
    alerts_.erase(
        std::remove_if(alerts_.begin(), alerts_.end(),
            [alert_id](const repositories::AlertStateRecord& a) {
                return a.alert_id == alert_id;
            }),
        alerts_.end());
    tray_badge_.Update(alerts_);
    state_    = AlertsPanelState::Loaded;
    is_dirty_ = true;
    return true;
}

int AlertsPanelController::TotalUnacknowledged() const noexcept {
    return tray_badge_.TotalBadgeCount();
}

} // namespace shelterops::ui::controllers
