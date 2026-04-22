#if defined(_WIN32)
#include "shelterops/shell/TrayManager.h"
#include <spdlog/spdlog.h>
#include <cstring>

namespace shelterops::shell {

TrayManager::TrayManager(HWND hwnd) : hwnd_(hwnd) {}

TrayManager::~TrayManager() {
    if (installed_) Uninstall();
}

void TrayManager::Install() {
    if (installed_) return;

    std::memset(&nid_, 0, sizeof(nid_));
    nid_.cbSize           = sizeof(nid_);
    nid_.hWnd             = hwnd_;
    nid_.uID              = 1;
    nid_.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid_.uCallbackMessage = WM_TRAY;
    nid_.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);

    wcsncpy_s(nid_.szTip, L"ShelterOps Desk Console", _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        spdlog::warn("TrayManager: Shell_NotifyIconW NIM_ADD failed");
        return;
    }
    installed_ = true;
    spdlog::info("TrayManager: tray icon installed");
}

void TrayManager::Uninstall() {
    if (!installed_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    installed_ = false;
    spdlog::info("TrayManager: tray icon removed");
}

void TrayManager::UpdateBadge(int count) {
    if (!installed_ || count == last_badge_) return;
    last_badge_ = count;

    wchar_t tip[128];
    if (count > 0)
        swprintf_s(tip, L"ShelterOps — %d unacknowledged alert%s",
                   count, count == 1 ? L"" : L"s");
    else
        wcsncpy_s(tip, L"ShelterOps Desk Console", _TRUNCATE);

    wcsncpy_s(nid_.szTip, tip, _TRUNCATE);
    nid_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayManager::ShowBalloon(const std::string& title, const std::string& text) {
    if (!installed_) return;

    nid_.uFlags |= NIF_INFO;
    nid_.dwInfoFlags = NIIF_INFO;
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nid_.szInfoTitle,
                        static_cast<int>(std::size(nid_.szInfoTitle)));
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nid_.szInfo,
                        static_cast<int>(std::size(nid_.szInfo)));
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    nid_.uFlags &= ~NIF_INFO;
}

bool TrayManager::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != WM_TRAY || wParam != 1) return false;

    if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
        RestoreFromTray();
        return true;
    }

    if (lParam == WM_RBUTTONUP) {
        POINT pt;
        GetCursorPos(&pt);
        HMENU menu = CreatePopupMenu();
        if (!menu) return true;

        AppendMenuW(menu, MF_STRING, 1, L"Open Alerts");
        AppendMenuW(menu, MF_STRING, 2, L"Low-Stock Alerts");
        AppendMenuW(menu, MF_STRING, 3, L"Expiration Alerts");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 4, L"Restore");
        AppendMenuW(menu, MF_STRING, 5, L"Exit");

        SetForegroundWindow(hwnd_);
        UINT cmd = TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
            pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);

        if (cmd == 1 && on_open_alerts_) {
            RestoreFromTray();
            on_open_alerts_();
        } else if (cmd == 2 && on_open_low_stock_) {
            RestoreFromTray();
            on_open_low_stock_();
        } else if (cmd == 3 && on_open_expiration_) {
            RestoreFromTray();
            on_open_expiration_();
        } else if (cmd == 4) {
            RestoreFromTray();
        } else if (cmd == 5) {
            PostMessageW(hwnd_, WM_TRAY_EXIT, 0, 0);
        }
        return true;
    }

    return false;
}

void TrayManager::MinimizeToTray() {
    ShowWindow(hwnd_, SW_HIDE);
    minimized_ = true;
}

void TrayManager::RestoreFromTray() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    minimized_ = false;
}

} // namespace shelterops::shell
#endif // _WIN32
