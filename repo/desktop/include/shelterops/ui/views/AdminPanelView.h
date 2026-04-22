#pragma once
#if defined(_WIN32)
#include "shelterops/ui/controllers/AdminPanelController.h"
#include "shelterops/shell/SessionContext.h"
#include <cstdint>

namespace shelterops::ui::views {

// Admin Panel view.
// Tabs: Catalog | Price Rules | Approval Queue | Retention | Export Permissions
class AdminPanelView {
public:
    AdminPanelView(controllers::AdminPanelController& ctrl,
                   shell::SessionContext&              session);

    bool Render(int64_t now_unix);

    bool IsOpen() const noexcept { return open_; }
    void Open()   noexcept { open_ = true; }
    void Close()  noexcept { open_ = false; }

private:
    void RenderCatalogTab(int64_t now_unix);
    void RenderPriceRulesTab(int64_t now_unix);
    void RenderApprovalQueueTab(int64_t now_unix);
    void RenderRetentionTab(int64_t now_unix);
    void RenderExportPermsTab(int64_t now_unix);

    controllers::AdminPanelController& ctrl_;
    shell::SessionContext&             session_;
    bool                               open_ = true;
};

} // namespace shelterops::ui::views
#endif // _WIN32
