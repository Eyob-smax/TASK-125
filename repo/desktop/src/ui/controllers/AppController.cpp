#include "shelterops/ui/controllers/AppController.h"
#include <spdlog/spdlog.h>

namespace shelterops::ui::controllers {

AppController::AppController(
    KennelBoardController&      kennel_board,
    ItemLedgerController&       item_ledger,
    ReportsController&          reports,
    GlobalSearchController&     global_search,
        AdminPanelController&       admin_panel,
        AuditLogController&         audit_log,
        AlertsPanelController&      alerts_panel,
        SchedulerPanelController&   scheduler_panel,
    services::CheckpointService& checkpoint,
    shell::SessionContext&       session)
    : kennel_board_(kennel_board), item_ledger_(item_ledger),
      reports_(reports), global_search_(global_search),
            admin_panel_(admin_panel), audit_log_(audit_log),
            alerts_panel_(alerts_panel), scheduler_panel_(scheduler_panel),
      checkpoint_(checkpoint), session_(session)
{}

void AppController::OpenWindow(shell::WindowId id) {
    open_windows_.insert(static_cast<int>(id));
    if (active_window_ == shell::WindowId::None)
        active_window_ = id;
    spdlog::debug("AppController: opened window {}", static_cast<int>(id));
}

void AppController::CloseWindow(shell::WindowId id) {
    open_windows_.erase(static_cast<int>(id));
    if (active_window_ == id)
        active_window_ = shell::WindowId::None;
}

bool AppController::IsWindowOpen(shell::WindowId id) const noexcept {
    return open_windows_.count(static_cast<int>(id)) > 0;
}

void AppController::SetActiveWindow(shell::WindowId id) noexcept {
    active_window_ = id;
    SyncShortcutContext();
}

void AppController::SyncShortcutContext() noexcept {
    bool in_edit = kennel_board_.State() == KennelBoardState::CreatingBooking
                || item_ledger_.State()  == ItemLedgerState::ReceivingStock
                || item_ledger_.State()  == ItemLedgerState::IssuingStock
                || item_ledger_.State()  == ItemLedgerState::AddingItem;

    shortcut_handler_.SetContext(session_.CurrentRole(), active_window_, in_edit);
}

shell::ShortcutAction AppController::ProcessKeyEvent(
    bool ctrl, bool shift, bool alt, int vk, int64_t now_unix)
{
    SyncShortcutContext();
    auto action = shortcut_handler_.Evaluate(ctrl, shift, alt, vk);

    switch (action) {
    case shell::ShortcutAction::ExportTable:
        // TSV is placed on clipboard by the render layer (Win32-only ClipboardHelper).
        break;
    case shell::ShortcutAction::CloseActiveWindow:
        CloseWindow(active_window_);
        break;
    case shell::ShortcutAction::NewRecord:
        if (active_window_ == shell::WindowId::KennelBoard)
            kennel_board_.BeginCreateBooking(kennel_board_.SelectedKennel());
        else if (active_window_ == shell::WindowId::ItemLedger)
            item_ledger_.BeginAddItem();
        break;
    default:
        break;
    }

    // Propagate dirty flags across windows.
    if (kennel_board_.IsDirty() || item_ledger_.IsDirty())
        cross_refresh_ = true;

    (void)now_unix;
    return action;
}

std::string AppController::GetActiveWindowTsv() const {
    switch (active_window_) {
    case shell::WindowId::KennelBoard: return kennel_board_.ClipboardTsv();
    case shell::WindowId::ItemLedger:  return item_ledger_.ClipboardTsv();
    default: return {};
    }
}

void AppController::CaptureCheckpoint(int64_t now_unix) {
    services::WindowInventory inv;
    for (int id : open_windows_)
        inv.open_window_ids.push_back(std::to_string(id));
    inv.active_window_id = std::to_string(static_cast<int>(active_window_));

    // Capture non-PII filter state from active controllers.
    std::vector<services::FormSnapshot> forms;

    if (IsWindowOpen(shell::WindowId::KennelBoard)) {
        services::FormSnapshot snap;
        snap.window_id = "kennel_board";
        snap.selected_row_key = std::to_string(kennel_board_.SelectedKennel());
        forms.push_back(std::move(snap));
    }
    if (IsWindowOpen(shell::WindowId::ItemLedger)) {
        services::FormSnapshot snap;
        snap.window_id = "item_ledger";
        snap.selected_row_key = std::to_string(item_ledger_.SelectedItem());
        forms.push_back(std::move(snap));
    }

    checkpoint_.CaptureState(inv, forms, now_unix);
}

bool AppController::RestoreCheckpoint(int64_t /*now_unix*/) {
    auto payload = checkpoint_.RestoreState();
    if (!payload) return false;

    open_windows_.clear();
    for (const auto& id_str : payload->windows.open_window_ids) {
        try {
            open_windows_.insert(std::stoi(id_str));
        } catch (...) {}
    }
    try {
        active_window_ = static_cast<shell::WindowId>(
            std::stoi(payload->windows.active_window_id));
    } catch (...) {
        active_window_ = shell::WindowId::None;
    }
    spdlog::info("AppController: checkpoint restored {} windows",
                 open_windows_.size());
    return true;
}

} // namespace shelterops::ui::controllers
