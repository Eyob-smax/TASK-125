#include "shelterops/shell/ShellController.h"
#include <spdlog/spdlog.h>

namespace shelterops::shell {

ShellController::ShellController(services::AuthService& auth,
                                 repositories::UserRepository& users,
                                 SessionContext&        ctx)
    : auth_(auth), users_(users), ctx_(ctx) {}

void ShellController::OnBootstrapComplete() {
    if (users_.IsEmpty()) {
        state_ = ShellState::InitialAdminSetupRequired;
        spdlog::info("ShellController: bootstrap complete, initial administrator setup required");
        return;
    }
    state_ = ShellState::LoginRequired;
    spdlog::info("ShellController: bootstrap complete, awaiting login");
}

std::optional<common::ErrorEnvelope> ShellController::CreateInitialAdmin(
    const std::string& username,
    const std::string& password,
    const std::string& display_name,
    int64_t now_unix) {
    if (!users_.IsEmpty()) {
        last_error_ = {common::ErrorCode::Forbidden,
                       "Initial administrator setup is already complete."};
        state_ = ShellState::LoginRequired;
        return last_error_;
    }

    if (username.empty() || display_name.empty()) {
        last_error_ = {common::ErrorCode::InvalidInput,
                       "Username and display name are required."};
        return last_error_;
    }

    if (!auth_.CreateInitialAdmin(username, password, display_name, now_unix)) {
        last_error_ = {common::ErrorCode::InvalidInput,
                       "Failed to create the initial administrator. Use a unique username and a password of at least 12 characters."};
        return last_error_;
    }

    last_error_.reset();
    state_ = ShellState::LoginRequired;
    spdlog::info("ShellController: initial administrator created");
    return std::nullopt;
}

std::optional<common::ErrorEnvelope> ShellController::OnLoginSubmitted(
        const std::string& username,
        const std::string& password,
        int64_t now_unix) {

    // Bind session to machine+operator so automation checks are user-scoped.
    const std::string machine_id = infrastructure::DeviceFingerprint::GetMachineId();
    std::string fingerprint = infrastructure::DeviceFingerprint::ComputeSessionBindingFingerprint(
        machine_id, username);
    auto result = auth_.Login(username, password, fingerprint, now_unix);

    if (auto* error = std::get_if<services::AuthError>(&result)) {
        common::ErrorCode code = common::ErrorCode::Unauthorized;
        if (error->code == "ACCOUNT_LOCKED" ||
            error->code == "ACCOUNT_INACTIVE") {
            code = common::ErrorCode::Forbidden;
        }
        last_error_ = common::ErrorEnvelope{code, error->message};
        spdlog::warn("ShellController: login failed [{}]: {}",
                     error->code, error->message);
        return last_error_;
    }

    const auto& handle = std::get<services::SessionHandle>(result);

    // Build user context from session handle.
    services::UserContext uctx;
    uctx.user_id    = handle.user_id;
    uctx.role_string = handle.role;
    uctx.session_id  = handle.session_id;

    // Parse role enum for gate checks.
    if (handle.role == "administrator")      uctx.role = domain::UserRole::Administrator;
    else if (handle.role == "operations_manager") uctx.role = domain::UserRole::OperationsManager;
    else if (handle.role == "inventory_clerk")    uctx.role = domain::UserRole::InventoryClerk;
    else                                          uctx.role = domain::UserRole::Auditor;

    ctx_.Set(uctx);
    last_error_.reset();
    state_ = ShellState::ShellReady;
    spdlog::info("ShellController: login success user_id={} role={}",
                 handle.user_id, handle.role);
    return std::nullopt;
}

void ShellController::OnLogout(int64_t now_unix) {
    if (ctx_.IsAuthenticated()) {
        auto uctx = ctx_.Get();
        auth_.Logout(uctx.session_id, now_unix);
    }
    ctx_.Clear();
    state_ = ShellState::LoginRequired;
    spdlog::info("ShellController: logout");
}

std::string ShellController::RoleBadge() const {
    if (!ctx_.IsAuthenticated()) return "";
    switch (ctx_.CurrentRole()) {
    case domain::UserRole::Administrator:    return "Administrator";
    case domain::UserRole::OperationsManager:return "Operations Manager";
    case domain::UserRole::InventoryClerk:   return "Inventory Clerk";
    case domain::UserRole::Auditor:          return "Auditor";
    }
    return "";
}

} // namespace shelterops::shell
