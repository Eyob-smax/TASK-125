#if defined(_WIN32)
#include "shelterops/shell/DockspaceShell.h"
#include "shelterops/shell/ClipboardHelper.h"
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <string>
#include <cstdio>

namespace shelterops::shell {

DockspaceShell::DockspaceShell(
    ui::controllers::AppController& app_ctrl,
    SessionContext&                  session_ctx,
    ShellController&                 shell_ctrl,
    TrayBadgeState&                  tray_state)
    : app_ctrl_(app_ctrl), session_ctx_(session_ctx),
      shell_ctrl_(shell_ctrl), tray_state_(tray_state)
{
    // Construct business window views using controller references from AppController.
    kennel_board_view_ = std::make_unique<ui::views::KennelBoardView>(
        app_ctrl_.KennelBoard(), session_ctx_);
    item_ledger_view_  = std::make_unique<ui::views::ItemLedgerView>(
        app_ctrl_.ItemLedger(), session_ctx_);
    reports_view_      = std::make_unique<ui::views::ReportsView>(
        app_ctrl_.Reports(), app_ctrl_.Reports().GetReportRepo(), session_ctx_);
    admin_panel_view_ = std::make_unique<ui::views::AdminPanelView>(
        app_ctrl_.AdminPanel(), session_ctx_);
    audit_log_view_ = std::make_unique<ui::views::AuditLogView>(
        app_ctrl_.AuditLog(), clipboard_helper_, session_ctx_);
    alerts_panel_view_ = std::make_unique<ui::views::AlertsPanelView>(
        app_ctrl_.AlertsPanel(), session_ctx_);
    scheduler_panel_view_ = std::make_unique<ui::views::SchedulerPanelView>(
        app_ctrl_.SchedulerPanel(), session_ctx_);
}

bool DockspaceShell::Render(int64_t now_unix, int64_t app_start_unix) {
    if (!dpi_applied_) {
        ApplyHighDpiScaling();
        dpi_applied_ = true;
    }

    HandleKeyboardShortcuts(now_unix);
    RenderMenuBar(now_unix);
    RenderDockspace();
    RenderStatusBar(now_unix, app_start_unix);

    if (search_open_) RenderGlobalSearchPopup();
    RenderNotificationOverlay();
    RenderBusinessWindows(now_unix);

    return !exit_requested_;
}

void DockspaceShell::ApplyHighDpiScaling() {
    ImGuiIO& io = ImGui::GetIO();
    // Scale ImGui style by DPI factor. Monitor DPI is available via
    // ImGui_ImplWin32_GetDpiScaleForMonitor on docking branch.
    float dpi_scale = 1.0f;
#if defined(ImGui_ImplWin32_GetDpiScaleForHwnd)
    HWND hwnd = (HWND)ImGui::GetMainViewport()->PlatformHandle;
    if (hwnd) dpi_scale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
#endif
    if (dpi_scale > 1.0f) {
        ImGui::GetStyle().ScaleAllSizes(dpi_scale);
        io.FontGlobalScale = dpi_scale;
    }
}

void DockspaceShell::HandleKeyboardShortcuts(int64_t now_unix) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard && ImGui::IsAnyItemActive()) return;

    bool ctrl  = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt   = io.KeyAlt;

    for (int vk = 0; vk < 512; ++vk) {
        if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(vk))) continue;

        auto action = app_ctrl_.ProcessKeyEvent(ctrl, shift, alt, vk, now_unix);

        switch (action) {
        case ShortcutAction::BeginGlobalSearch:
            search_open_ = true;
            search_buf_[0] = '\0';
            ImGui::SetNextWindowFocus();
            break;
        case ShortcutAction::ExportTable: {
            std::string tsv = app_ctrl_.GetActiveWindowTsv();
            if (!tsv.empty()) ClipboardHelper::SetText(tsv);
            break;
        }
        case ShortcutAction::BeginLogout:
            shell_ctrl_.OnLogout(now_unix);
            break;
        default:
            break;
        }
    }
}

void DockspaceShell::RenderMenuBar(int64_t now_unix) {
    if (!ImGui::BeginMainMenuBar()) return;

    ImGui::TextUnformatted("ShelterOps");
    ImGui::Separator();

    auto ctx = session_ctx_.Get();

    if (ImGui::BeginMenu("Windows")) {
        if (ImGui::MenuItem("Kennel Board", "Ctrl+1",
                app_ctrl_.IsWindowOpen(WindowId::KennelBoard)))
            app_ctrl_.OpenWindow(WindowId::KennelBoard);

        if (ImGui::MenuItem("Item Ledger", "Ctrl+2",
                app_ctrl_.IsWindowOpen(WindowId::ItemLedger)))
            app_ctrl_.OpenWindow(WindowId::ItemLedger);

        // Reports Studio and Admin Panel visible to OperationsManager+.
        if (ctx.role <= domain::UserRole::OperationsManager) {
            if (ImGui::MenuItem("Reports Studio", "Ctrl+3",
                    app_ctrl_.IsWindowOpen(WindowId::ReportsStudio)))
                app_ctrl_.OpenWindow(WindowId::ReportsStudio);

            if (ImGui::MenuItem("Scheduler", nullptr,
                app_ctrl_.IsWindowOpen(WindowId::SchedulerPanel)))
            app_ctrl_.OpenWindow(WindowId::SchedulerPanel);
        }

        // Admin Panel: Administrator only.
        if (ctx.role == domain::UserRole::Administrator) {
            if (ImGui::MenuItem("Admin Panel", nullptr,
                    app_ctrl_.IsWindowOpen(WindowId::AdminPanel)))
                app_ctrl_.OpenWindow(WindowId::AdminPanel);
        }

        // Audit Log: all roles.
        if (ImGui::MenuItem("Audit Log", nullptr,
                app_ctrl_.IsWindowOpen(WindowId::AuditLog)))
            app_ctrl_.OpenWindow(WindowId::AuditLog);

        if (ImGui::MenuItem("Alerts", nullptr,
                app_ctrl_.IsWindowOpen(WindowId::AlertsPanel))) {
            app_ctrl_.OpenWindow(WindowId::AlertsPanel);
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Search", "Ctrl+F"))
            search_open_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Sign Out"))
            shell_ctrl_.OnLogout(now_unix);
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            exit_requested_ = true;
        ImGui::EndMenu();
    }

    // Right-aligned role badge.
    {
        std::string badge = shell_ctrl_.RoleBadge();
        if (!badge.empty()) {
            float avail  = ImGui::GetContentRegionAvail().x;
            float text_w = ImGui::CalcTextSize(badge.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - text_w - 8.0f);
            ImGui::TextUnformatted(badge.c_str());
        }
    }

    ImGui::EndMainMenuBar();
}

void DockspaceShell::RenderDockspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking  | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DockspaceHost", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dock_id = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dock_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

void DockspaceShell::RenderStatusBar(int64_t now_unix, int64_t app_start_unix) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float bar_h = ImGui::GetFrameHeight() + 4.0f;
    ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - bar_h});
    ImGui::SetNextWindowSize({vp->WorkSize.x, bar_h});
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove  |
        ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::Begin("##StatusBar", nullptr, flags);
    ImGui::PopStyleVar(2);

    // User/role.
    if (session_ctx_.IsAuthenticated()) {
        auto ctx = session_ctx_.Get();
        ImGui::Text("%s  |  %s", ctx.display_name.c_str(),
                    shell_ctrl_.RoleBadge().c_str());
    }
    ImGui::SameLine();

    // Alert badge.
    int badge = tray_state_.TotalBadgeCount();
    if (badge > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
        ImGui::Text("  [!] %d alert%s", badge, badge == 1 ? "" : "s");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();

    // Uptime.
    int64_t uptime_sec = now_unix - app_start_unix;
    int h = static_cast<int>(uptime_sec / 3600);
    int m = static_cast<int>((uptime_sec % 3600) / 60);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "  up %dh %02dm", h, m);
    float avail = ImGui::GetContentRegionAvail().x;
    float text_w = ImGui::CalcTextSize(buf).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - text_w - 4.0f);
    ImGui::TextUnformatted(buf);

    ImGui::End();
}

void DockspaceShell::RenderGlobalSearchPopup() {
    ImGui::SetNextWindowSize({520.0f, 400.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.3f});

    if (!ImGui::Begin("Global Search##popup",
                       &search_open_,
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    bool should_search = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##searchq", search_buf_, sizeof(search_buf_),
                          ImGuiInputTextFlags_EnterReturnsTrue))
        should_search = true;

    if (search_buf_[0] == '\0' && ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere(-1);

    ImGui::Separator();

    if (should_search && search_buf_[0] != '\0') {
        auto role = session_ctx_.CurrentRole();
        int64_t now_ts = 0; // passed in on real call; placeholder here
        auto results = app_ctrl_.GlobalSearch().Search(search_buf_, role, now_ts);

        if (results.items.empty()) {
            ImGui::TextDisabled("No results for \"%s\".", search_buf_);
        } else {
            for (const auto& item : results.items) {
                std::string label = item.display_text;
                if (item.is_masked) label += "  [masked]";
                if (ImGui::Selectable(label.c_str())) {
                    app_ctrl_.OpenWindow(item.target_window);
                    app_ctrl_.SetActiveWindow(item.target_window);
                    search_open_ = false;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("  %s", item.detail_snippet.c_str());
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) search_open_ = false;
    ImGui::End();
}

void DockspaceShell::RenderNotificationOverlay() {
    // Lightweight overlay for background-job progress.
    // Only shown when there are active (running) report jobs.
    const auto& runs = app_ctrl_.Reports().ActiveRuns();
    int running_count = 0;
    for (const auto& r : runs)
        if (r.status == "running") ++running_count;

    if (running_count == 0) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {vp->WorkPos.x + vp->WorkSize.x - 10.0f,
         vp->WorkPos.y + 30.0f},
        ImGuiCond_Always, {1.0f, 0.0f});
    ImGui::SetNextWindowBgAlpha(0.75f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("##JobOverlay", nullptr, flags);
    ImGui::Text("Running: %d report job%s...",
                running_count, running_count == 1 ? "" : "s");
    ImGui::End();
}

void DockspaceShell::RenderBusinessWindows(int64_t now_unix) {
    if (app_ctrl_.IsWindowOpen(WindowId::KennelBoard))
        RenderKennelBoardWindow(now_unix);
    if (app_ctrl_.IsWindowOpen(WindowId::ItemLedger))
        RenderItemLedgerWindow(now_unix);
    if (app_ctrl_.IsWindowOpen(WindowId::ReportsStudio))
        RenderReportsWindow(now_unix);
    if (app_ctrl_.IsWindowOpen(WindowId::AdminPanel))
        RenderAdminPanelWindow(now_unix);
    if (app_ctrl_.IsWindowOpen(WindowId::AuditLog))
        RenderAuditLogWindow(now_unix);
    if (app_ctrl_.IsWindowOpen(WindowId::AlertsPanel))
        RenderAlertsPanelWindow(now_unix);
    if (app_ctrl_.IsWindowOpen(WindowId::SchedulerPanel))
        RenderSchedulerPanelWindow(now_unix);
}

void DockspaceShell::RenderKennelBoardWindow(int64_t now_unix) {
    if (kennel_board_view_ && !kennel_board_view_->Render(now_unix))
        app_ctrl_.CloseWindow(WindowId::KennelBoard);
}

void DockspaceShell::RenderItemLedgerWindow(int64_t now_unix) {
    if (item_ledger_view_ && !item_ledger_view_->Render(now_unix))
        app_ctrl_.CloseWindow(WindowId::ItemLedger);
}

void DockspaceShell::RenderReportsWindow(int64_t now_unix) {
    if (reports_view_ && !reports_view_->Render(now_unix))
        app_ctrl_.CloseWindow(WindowId::ReportsStudio);
}

void DockspaceShell::RenderAdminPanelWindow(int64_t now_unix) {
    if (admin_panel_view_ && !admin_panel_view_->Render(now_unix))
        app_ctrl_.CloseWindow(WindowId::AdminPanel);
}

void DockspaceShell::RenderAuditLogWindow(int64_t now_unix) {
    if (audit_log_view_ && !audit_log_view_->Render(now_unix))
        app_ctrl_.CloseWindow(WindowId::AuditLog);
}

void DockspaceShell::RenderAlertsPanelWindow(int64_t now_unix) {
    if (alerts_panel_view_ && !alerts_panel_view_->Render(now_unix))
        app_ctrl_.CloseWindow(WindowId::AlertsPanel);
}

void DockspaceShell::RenderSchedulerPanelWindow(int64_t now_unix) {
    if (scheduler_panel_view_ && !scheduler_panel_view_->Render(now_unix))
        app_ctrl_.CloseWindow(WindowId::SchedulerPanel);
}

} // namespace shelterops::shell
#endif // _WIN32
