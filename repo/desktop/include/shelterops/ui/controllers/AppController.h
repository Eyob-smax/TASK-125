#pragma once
#include "shelterops/ui/controllers/KennelBoardController.h"
#include "shelterops/ui/controllers/ItemLedgerController.h"
#include "shelterops/ui/controllers/ReportsController.h"
#include "shelterops/ui/controllers/GlobalSearchController.h"
#include "shelterops/ui/controllers/AdminPanelController.h"
#include "shelterops/ui/controllers/AuditLogController.h"
#include "shelterops/ui/controllers/AlertsPanelController.h"
#include "shelterops/ui/controllers/SchedulerPanelController.h"
#include "shelterops/services/CheckpointService.h"
#include "shelterops/shell/KeyboardShortcutHandler.h"
#include "shelterops/shell/SessionContext.h"
#include <unordered_set>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::ui::controllers {

// Top-level shell orchestrator.
// Owns window-open state, routes shortcuts, captures checkpoints.
// Cross-platform: no ImGui dependency.
class AppController {
public:
    AppController(
        KennelBoardController&     kennel_board,
        ItemLedgerController&      item_ledger,
        ReportsController&         reports,
        GlobalSearchController&    global_search,
        AdminPanelController&      admin_panel,
        AuditLogController&        audit_log,
        AlertsPanelController&     alerts_panel,
        SchedulerPanelController&  scheduler_panel,
        services::CheckpointService& checkpoint,
        shell::SessionContext&      session);

    // --- Window management ---
    void OpenWindow(shell::WindowId id);
    void CloseWindow(shell::WindowId id);
    bool IsWindowOpen(shell::WindowId id) const noexcept;
    void SetActiveWindow(shell::WindowId id) noexcept;
    shell::WindowId GetActiveWindow() const noexcept { return active_window_; }

    // --- Shortcut dispatch ---
    // Called by the render layer after detecting a key event.
    // Returns the resolved action (None if nothing to do).
    shell::ShortcutAction ProcessKeyEvent(bool ctrl, bool shift, bool alt,
                                           int vk, int64_t now_unix);

    // --- Cross-window refresh signal ---
    // Set when one controller marks data dirty; cleared by the render layer.
    bool HasCrossWindowRefresh() const noexcept { return cross_refresh_; }
    void SetCrossWindowRefresh()    noexcept { cross_refresh_ = true; }
    void ClearCrossWindowRefresh()  noexcept { cross_refresh_ = false; }

    // --- Clipboard TSV ---
    // Returns the TSV string for the currently active window's table.
    std::string GetActiveWindowTsv() const;

    // --- Checkpoint ---
    void CaptureCheckpoint(int64_t now_unix);
    bool RestoreCheckpoint(int64_t now_unix);

    // --- Child controller accessors ---
    KennelBoardController&  KennelBoard()   noexcept { return kennel_board_; }
    ItemLedgerController&   ItemLedger()    noexcept { return item_ledger_; }
    ReportsController&      Reports()       noexcept { return reports_; }
    GlobalSearchController& GlobalSearch()  noexcept { return global_search_; }
    AdminPanelController&   AdminPanel()    noexcept { return admin_panel_; }
    AuditLogController&     AuditLog()      noexcept { return audit_log_; }
    AlertsPanelController&  AlertsPanel()   noexcept { return alerts_panel_; }
    SchedulerPanelController& SchedulerPanel() noexcept { return scheduler_panel_; }

private:
    void SyncShortcutContext() noexcept;

    KennelBoardController&      kennel_board_;
    ItemLedgerController&       item_ledger_;
    ReportsController&          reports_;
    GlobalSearchController&     global_search_;
    AdminPanelController&       admin_panel_;
    AuditLogController&         audit_log_;
    AlertsPanelController&      alerts_panel_;
    SchedulerPanelController&   scheduler_panel_;
    services::CheckpointService& checkpoint_;
    shell::SessionContext&       session_;

    shell::KeyboardShortcutHandler shortcut_handler_;
    std::unordered_set<int>        open_windows_;   // cast WindowId to int
    shell::WindowId                active_window_   = shell::WindowId::None;
    bool                           cross_refresh_   = false;
};

} // namespace shelterops::ui::controllers
