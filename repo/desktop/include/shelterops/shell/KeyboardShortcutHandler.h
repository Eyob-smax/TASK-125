#pragma once
#include "shelterops/domain/Types.h"

namespace shelterops::shell {

// Logical window identifiers used by the shell and shortcut handler.
enum class WindowId {
    None,
    KennelBoard,
    ItemLedger,
    ReportsStudio,
    SchedulerPanel,
    AdminPanel,
    AuditLog,
    AlertsPanel
};

enum class ShortcutAction {
    None,
    BeginGlobalSearch,   // Ctrl+F
    NewRecord,           // Ctrl+N
    EditRecord,          // F2
    ExportTable,         // Ctrl+Shift+E
    CloseActiveWindow,   // Ctrl+W
    BeginLogout,         // Ctrl+Shift+L
};

// Maps key events to ShortcutActions, respecting role-based disabled states.
// Cross-platform: the view layer feeds raw ctrl/shift/vk values from ImGui.
class KeyboardShortcutHandler {
public:
    // Update the context before each frame.
    void SetContext(domain::UserRole role, WindowId active, bool in_edit_mode) noexcept;

    // Evaluate one key event. Returns None if no shortcut matches or the
    // matching action is disabled for the current context.
    // vk values match Win32 VK_* constants (0x46='F', 0x4E='N', etc.).
    ShortcutAction Evaluate(bool ctrl, bool shift, bool alt, int vk) const noexcept;

    // Whether a given action is currently enabled.
    bool IsEnabled(ShortcutAction action) const noexcept;

private:
    domain::UserRole active_role_   = domain::UserRole::Auditor;
    WindowId         active_window_ = WindowId::None;
    bool             in_edit_mode_  = false;
};

} // namespace shelterops::shell
