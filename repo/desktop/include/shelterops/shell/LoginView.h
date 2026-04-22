#pragma once
// Win32 / ImGui header — only included on Windows.
// Render functions must not make SQLite calls.
#if defined(_WIN32)
#include "shelterops/shell/ShellController.h"
#include <string>

namespace shelterops::shell {

// Dear ImGui login window.
// Renders username/password fields; delegates submit to ShellController.
// Clears and zero-wipes the password buffer on success or close.
class LoginView {
public:
    explicit LoginView(ShellController& shell);

    // Call once per ImGui frame. Returns true while the view should remain open.
    bool Render(int64_t now_unix);

private:
    bool RenderInitialAdminSetup(int64_t now_unix);
    void ClearBuffers();

    ShellController& shell_;
    char display_name_buf_[128] = {};
    char confirm_password_buf_[128] = {};
    char username_buf_[128]  = {};
    char password_buf_[128]  = {};
    bool focus_username_     = true;
};

} // namespace shelterops::shell
#endif // _WIN32
