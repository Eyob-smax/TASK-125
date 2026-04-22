#include "shelterops/shell/KeyboardShortcutHandler.h"

namespace shelterops::shell {

// Win32 VK_* values; same numeric values used in tests cross-platform.
static constexpr int kVK_F  = 0x46;
static constexpr int kVK_N  = 0x4E;
static constexpr int kVK_W  = 0x57;
static constexpr int kVK_E  = 0x45;
static constexpr int kVK_L  = 0x4C;
static constexpr int kVK_F2 = 0x71;

void KeyboardShortcutHandler::SetContext(
    domain::UserRole role, WindowId active, bool in_edit_mode) noexcept
{
    active_role_   = role;
    active_window_ = active;
    in_edit_mode_  = in_edit_mode;
}

ShortcutAction KeyboardShortcutHandler::Evaluate(
    bool ctrl, bool shift, bool /*alt*/, int vk) const noexcept
{
    if (ctrl && !shift && vk == kVK_F)
        return ShortcutAction::BeginGlobalSearch;
    if (ctrl && !shift && vk == kVK_N)
        return IsEnabled(ShortcutAction::NewRecord)
               ? ShortcutAction::NewRecord : ShortcutAction::None;
    if (!ctrl && !shift && vk == kVK_F2)
        return IsEnabled(ShortcutAction::EditRecord)
               ? ShortcutAction::EditRecord : ShortcutAction::None;
    if (ctrl && shift && vk == kVK_E)
        return IsEnabled(ShortcutAction::ExportTable)
               ? ShortcutAction::ExportTable : ShortcutAction::None;
    if (ctrl && !shift && vk == kVK_W)
        return IsEnabled(ShortcutAction::CloseActiveWindow)
               ? ShortcutAction::CloseActiveWindow : ShortcutAction::None;
    if (ctrl && shift && vk == kVK_L)
        return ShortcutAction::BeginLogout;
    return ShortcutAction::None;
}

bool KeyboardShortcutHandler::IsEnabled(ShortcutAction action) const noexcept {
    switch (action) {
    case ShortcutAction::NewRecord:
    case ShortcutAction::EditRecord:
        // Auditors are read-only; actions need an open window.
        return active_role_ != domain::UserRole::Auditor
            && active_window_ != WindowId::None
            && !in_edit_mode_;
    case ShortcutAction::ExportTable:
        // Auditors cannot trigger data exports.
        return active_role_ != domain::UserRole::Auditor
            && active_window_ != WindowId::None;
    case ShortcutAction::CloseActiveWindow:
        return active_window_ != WindowId::None;
    case ShortcutAction::BeginGlobalSearch:
    case ShortcutAction::BeginLogout:
        return true;
    case ShortcutAction::None:
        return false;
    }
    return false;
}

} // namespace shelterops::shell
