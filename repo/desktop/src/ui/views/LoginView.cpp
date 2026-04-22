#if defined(_WIN32)
#include "shelterops/shell/LoginView.h"
#include "shelterops/shell/ErrorDisplay.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include <imgui.h>
#include <chrono>
#include <cstring>

namespace shelterops::shell {

LoginView::LoginView(ShellController& shell) : shell_(shell) {}

bool LoginView::Render(int64_t now_unix) {
    if (shell_.CurrentState() == ShellState::InitialAdminSetupRequired) {
        return RenderInitialAdminSetup(now_unix);
    }

    // Center the login dialog.
    ImGuiIO& io = ImGui::GetIO();
    float cx = io.DisplaySize.x * 0.5f;
    float cy = io.DisplaySize.y * 0.5f;
    ImGui::SetNextWindowPos({cx, cy}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380.0f, 0.0f}, ImGuiCond_Always);

    bool open = true;
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("##Login", &open, flags)) {
        ImGui::End();
        return true; // keep open
    }

    ImGui::TextUnformatted("ShelterOps Desk Console");
    ImGui::Separator();
    ImGui::Spacing();

    if (focus_username_) {
        ImGui::SetKeyboardFocusHere();
        focus_username_ = false;
    }

    bool submit = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##username", username_buf_, sizeof(username_buf_),
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::TextUnformatted("Username");

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##password", password_buf_, sizeof(password_buf_),
                          ImGuiInputTextFlags_Password |
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        submit = true;
    }
    ImGui::TextUnformatted("Password");

    ImGui::Spacing();
    if (ImGui::Button("Sign In", {-1.0f, 0.0f})) submit = true;

    // Error banner
    const auto& err = shell_.LastError();
    if (err.has_value()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", ErrorDisplay::UserMessage(*err).c_str());
        ImGui::PopStyleColor();
    }

    if (submit && username_buf_[0] != '\0') {
        auto error = shell_.OnLoginSubmitted(username_buf_, password_buf_,
                                              now_unix);
        // Zero-wipe password buffer regardless of outcome.
        infrastructure::CryptoHelper::ZeroBuffer(password_buf_,
                                                   sizeof(password_buf_));
        if (!error.has_value()) {
            // Login succeeded — zero username too, close the login window.
            ClearBuffers();
            ImGui::End();
            return false; // signal caller: close login view
        }
        // On failure keep the form open; error shows via LastError().
    }

    ImGui::End();
    return shell_.CurrentState() != ShellState::ShellReady;
}

bool LoginView::RenderInitialAdminSetup(int64_t now_unix) {
    ImGuiIO& io = ImGui::GetIO();
    float cx = io.DisplaySize.x * 0.5f;
    float cy = io.DisplaySize.y * 0.5f;
    ImGui::SetNextWindowPos({cx, cy}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({420.0f, 0.0f}, ImGuiCond_Always);

    bool open = true;
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("##InitialAdmin", &open, flags)) {
        ImGui::End();
        return true;
    }

    ImGui::TextUnformatted("Create Initial Administrator");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##setup_username", username_buf_, sizeof(username_buf_));
    ImGui::TextUnformatted("Username");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##setup_display_name", display_name_buf_, sizeof(display_name_buf_));
    ImGui::TextUnformatted("Display Name");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##setup_password", password_buf_, sizeof(password_buf_),
                     ImGuiInputTextFlags_Password);
    ImGui::TextUnformatted("Password");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##setup_confirm", confirm_password_buf_, sizeof(confirm_password_buf_),
                     ImGuiInputTextFlags_Password);
    ImGui::TextUnformatted("Confirm Password");

    bool submit = ImGui::Button("Create Administrator", {-1.0f, 0.0f});
    const auto& err = shell_.LastError();
    if (err.has_value()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", ErrorDisplay::UserMessage(*err).c_str());
        ImGui::PopStyleColor();
    }

    if (submit) {
        if (std::strcmp(password_buf_, confirm_password_buf_) != 0) {
            shell_.ClearLastError();
        } else {
            auto create_err = shell_.CreateInitialAdmin(
                username_buf_, password_buf_, display_name_buf_, now_unix);
            if (!create_err.has_value()) {
                ClearBuffers();
            }
        }
    }

    ImGui::End();
    return true;
}

void LoginView::ClearBuffers() {
    infrastructure::CryptoHelper::ZeroBuffer(username_buf_, sizeof(username_buf_));
    infrastructure::CryptoHelper::ZeroBuffer(display_name_buf_, sizeof(display_name_buf_));
    infrastructure::CryptoHelper::ZeroBuffer(password_buf_, sizeof(password_buf_));
    infrastructure::CryptoHelper::ZeroBuffer(confirm_password_buf_, sizeof(confirm_password_buf_));
}

} // namespace shelterops::shell
#endif // _WIN32
