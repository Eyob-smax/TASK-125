#pragma once
#include "shelterops/ui/controllers/AppController.h"
#include "shelterops/shell/SessionContext.h"
#include "shelterops/shell/ShellController.h"
#include "shelterops/shell/TrayBadgeState.h"
#include <cstdint>
#include <memory>

#if defined(_WIN32)
#include "shelterops/ui/views/KennelBoardView.h"
#include "shelterops/ui/views/ItemLedgerView.h"
#include "shelterops/ui/views/ReportsView.h"
#include "shelterops/ui/views/AdminPanelView.h"
#include "shelterops/ui/views/AuditLogView.h"
#include "shelterops/ui/views/AlertsPanelView.h"
#include "shelterops/ui/views/SchedulerPanelView.h"
#endif

namespace shelterops::shell {

// Renders the authenticated desktop shell each frame:
//  - full-screen dockspace
//  - role-aware main menu bar
//  - status bar (user badge, alert count, uptime)
//  - global search popup (Ctrl+F)
//  - notification overlay for job progress
//  - forwards keyboard shortcuts to AppController
// Win32/ImGui only — compiled into ShelterOpsDesk.exe.
class DockspaceShell {
public:
    DockspaceShell(ui::controllers::AppController& app_ctrl,
                   SessionContext&                  session_ctx,
                   ShellController&                 shell_ctrl,
                   TrayBadgeState&                  tray_state);

    // Called once per frame. Returns false when the user requests exit.
    bool Render(int64_t now_unix, int64_t app_start_unix);

private:
    void RenderMenuBar(int64_t now_unix);
    void RenderDockspace();
    void RenderStatusBar(int64_t now_unix, int64_t app_start_unix);
    void RenderGlobalSearchPopup();
    void RenderNotificationOverlay();
    void RenderBusinessWindows(int64_t now_unix);
    void HandleKeyboardShortcuts(int64_t now_unix);
    void ApplyHighDpiScaling();

    ui::controllers::AppController& app_ctrl_;
    SessionContext&                  session_ctx_;
    ShellController&                 shell_ctrl_;
    TrayBadgeState&                  tray_state_;

    char   search_buf_[256] = {};
    bool   search_open_     = false;
    bool   exit_requested_  = false;
    bool   dpi_applied_     = false;

    void RenderKennelBoardWindow(int64_t now_unix);
    void RenderItemLedgerWindow(int64_t now_unix);
    void RenderReportsWindow(int64_t now_unix);
    void RenderAdminPanelWindow(int64_t now_unix);
    void RenderAuditLogWindow(int64_t now_unix);
    void RenderAlertsPanelWindow(int64_t now_unix);
    void RenderSchedulerPanelWindow(int64_t now_unix);

#if defined(_WIN32)
    shell::ClipboardHelper clipboard_helper_;
    // Business window views — constructed with controller + session references.
    std::unique_ptr<ui::views::KennelBoardView> kennel_board_view_;
    std::unique_ptr<ui::views::ItemLedgerView>  item_ledger_view_;
    std::unique_ptr<ui::views::ReportsView>     reports_view_;
    std::unique_ptr<ui::views::AdminPanelView>  admin_panel_view_;
    std::unique_ptr<ui::views::AuditLogView>    audit_log_view_;
    std::unique_ptr<ui::views::AlertsPanelView> alerts_panel_view_;
    std::unique_ptr<ui::views::SchedulerPanelView> scheduler_panel_view_;
#endif
};

} // namespace shelterops::shell
