#pragma once
#include "shelterops/shell/SessionContext.h"
#include "shelterops/services/AuthService.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/infrastructure/DeviceFingerprint.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <string>
#include <optional>

namespace shelterops::shell {

// Application-level state machine controlling the session lifecycle.
// Cross-platform logic lives here; Win32/ImGui rendering is in LoginView.cpp.
//
// States:
//   Uninitialized  — before bootstrap completes
//   LoginRequired  — no active session; LoginView should be shown
//   Authenticated  — user is authenticated; business shell is available
//   ShellReady     — all subsystems bootstrapped; ready for window opens
enum class ShellState {
    Uninitialized,
    InitialAdminSetupRequired,
    LoginRequired,
    Authenticated,
    ShellReady
};

class ShellController {
public:
    explicit ShellController(services::AuthService& auth,
                             repositories::UserRepository& users,
                             SessionContext&        ctx);

    ShellState CurrentState() const noexcept { return state_; }

    // Called by the bootstrap path after DB init + migration.
    void OnBootstrapComplete();
    std::optional<common::ErrorEnvelope> CreateInitialAdmin(
        const std::string& username,
        const std::string& password,
        const std::string& display_name,
        int64_t now_unix);

    // Called by LoginView when the operator submits credentials.
    // Returns nullopt on success; ErrorEnvelope on failure.
    std::optional<common::ErrorEnvelope> OnLoginSubmitted(
        const std::string& username,
        const std::string& password,
        int64_t now_unix);

    // Called on logout (menu item or timeout).
    void OnLogout(int64_t now_unix);

    // Returns the last error (if any) to display in the login view.
    const std::optional<common::ErrorEnvelope>& LastError() const noexcept {
        return last_error_;
    }

    void ClearLastError() { last_error_.reset(); }

    // Role badge string for the shell status bar (e.g. "Administrator").
    std::string RoleBadge() const;

private:
    services::AuthService& auth_;
    repositories::UserRepository& users_;
    SessionContext&         ctx_;
    ShellState              state_   = ShellState::Uninitialized;
    std::optional<common::ErrorEnvelope> last_error_;
};

} // namespace shelterops::shell
