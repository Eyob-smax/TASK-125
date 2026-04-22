#pragma once
#if defined(_WIN32)
#include "shelterops/ui/controllers/AlertsPanelController.h"
#include "shelterops/shell/SessionContext.h"
#include <cstdint>

namespace shelterops::ui::views {

// Alerts Panel view — surfaces low-stock, expiring-soon, and expired alerts.
// Auditors see the alert list read-only; other roles can acknowledge.
class AlertsPanelView {
public:
    AlertsPanelView(controllers::AlertsPanelController& ctrl,
                    shell::SessionContext&               session);

    bool Render(int64_t now_unix);

    bool IsOpen() const noexcept { return open_; }
    void Open()   noexcept { open_ = true; }
    void Close()  noexcept { open_ = false; }

private:
    controllers::AlertsPanelController& ctrl_;
    shell::SessionContext&              session_;
    bool                                open_ = true;
};

} // namespace shelterops::ui::views
#endif // _WIN32
