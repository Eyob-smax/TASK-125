#pragma once
#include <string>

namespace shelterops::shell {

// Clipboard utilities. SetText is Win32-only; FormatTsv is cross-platform.
struct ClipboardHelper {
    // Copy text to the system clipboard (Win32). No-op on non-Windows builds.
    static bool SetText(const std::string& text);
};

} // namespace shelterops::shell
