#pragma once
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>

namespace shelterops::shell {

// Win32 system-tray integration.
// Installs a tray icon on construction; removes it on destruction.
// Forwards WM_APP+1 tray notification messages via HandleMessage().
class TrayManager {
public:
    static constexpr UINT WM_TRAY = WM_APP + 1;
    static constexpr UINT WM_TRAY_EXIT = WM_APP + 2;

    using ActionCallback = std::function<void()>;

    explicit TrayManager(HWND hwnd);
    ~TrayManager();

    TrayManager(const TrayManager&) = delete;
    TrayManager& operator=(const TrayManager&) = delete;

    void Install();
    void Uninstall();

    // Update the badge tooltip/icon. count=0 removes the alert icon variant.
    void UpdateBadge(int count);

    // Show a balloon notification (informational; used for new-alert events).
    void ShowBalloon(const std::string& title, const std::string& text);

    // Handle WM_TRAY messages routed from WndProc. Returns true if consumed.
    bool HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool IsInstalled()       const noexcept { return installed_; }
    bool IsMinimizedToTray() const noexcept { return minimized_; }

    void MinimizeToTray();
    void RestoreFromTray();

    // Right-click menu callbacks for alert-panel quick-open actions.
    void SetOpenAlertsCallback(ActionCallback cb)       { on_open_alerts_      = std::move(cb); }
    void SetOpenLowStockCallback(ActionCallback cb)     { on_open_low_stock_   = std::move(cb); }
    void SetOpenExpirationCallback(ActionCallback cb)   { on_open_expiration_  = std::move(cb); }

private:
    HWND        hwnd_        = nullptr;
    NOTIFYICONDATAW nid_     = {};
    bool        installed_   = false;
    bool        minimized_   = false;
    int         last_badge_  = 0;
    ActionCallback on_open_alerts_;
    ActionCallback on_open_low_stock_;
    ActionCallback on_open_expiration_;
};

} // namespace shelterops::shell
#endif // _WIN32
