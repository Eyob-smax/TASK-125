#pragma once
#include "shelterops/services/AlertService.h"
#include "shelterops/shell/TrayBadgeState.h"
#include "shelterops/services/UserContext.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <vector>
#include <cstdint>

namespace shelterops::ui::controllers {

enum class AlertsPanelState {
    Idle,
    Loading,
    Loaded,
    Acknowledging,
    Error
};

// Controller for the Alerts Panel window and tray badge.
// Surfaced via tray notification when unacknowledged alerts exist.
// Cross-platform: no ImGui dependency.
class AlertsPanelController {
public:
    AlertsPanelController(services::AlertService&  alert_svc,
                           shell::TrayBadgeState&   tray_badge);

    AlertsPanelState                                    State()    const noexcept { return state_; }
    const std::vector<repositories::AlertStateRecord>& Alerts()   const noexcept { return alerts_; }
    const common::ErrorEnvelope&                        LastError() const noexcept { return last_error_; }
    const shell::TrayBadgeState&                        BadgeState() const noexcept { return tray_badge_; }
    bool                                                IsDirty()  const noexcept { return is_dirty_; }

    // Scan for new alerts and reload the active list. Updates tray badge.
    void Refresh(const domain::AlertThreshold& thresholds, int64_t now_unix);

    // Acknowledge a single alert. Auditors cannot acknowledge.
    bool AcknowledgeAlert(int64_t alert_id,
                          const services::UserContext& ctx,
                          int64_t now_unix);

    // Number of unacknowledged alerts (for tray badge and menu count).
    int TotalUnacknowledged() const noexcept;

    void ClearDirty() noexcept { is_dirty_ = false; }
    void ClearError() noexcept { last_error_ = {}; state_ = AlertsPanelState::Idle; }

private:
    services::AlertService& alert_svc_;
    shell::TrayBadgeState&  tray_badge_;

    AlertsPanelState                           state_    = AlertsPanelState::Idle;
    std::vector<repositories::AlertStateRecord> alerts_;
    common::ErrorEnvelope                       last_error_;
    bool                                        is_dirty_ = false;
};

} // namespace shelterops::ui::controllers
