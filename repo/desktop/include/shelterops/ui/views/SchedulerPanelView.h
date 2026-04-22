#pragma once
#if defined(_WIN32)
#include "shelterops/ui/controllers/SchedulerPanelController.h"
#include "shelterops/shell/SessionContext.h"
#include <cstdint>

namespace shelterops::ui::views {

// Scheduler Panel view — job list, dependency stages, run history, failure details.
class SchedulerPanelView {
public:
    SchedulerPanelView(controllers::SchedulerPanelController& ctrl,
                       shell::SessionContext&                  session);

    bool Render(int64_t now_unix);

    bool IsOpen() const noexcept { return open_; }
    void Open()   noexcept { open_ = true; }
    void Close()  noexcept { open_ = false; }

private:
    void RenderJobListPanel(int64_t now_unix);
    void RenderDetailPanel(int64_t now_unix);

    controllers::SchedulerPanelController& ctrl_;
    shell::SessionContext&                 session_;
    bool                                   open_ = true;
};

} // namespace shelterops::ui::views
#endif // _WIN32
