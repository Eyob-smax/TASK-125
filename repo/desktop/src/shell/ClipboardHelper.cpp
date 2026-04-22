#include "shelterops/shell/ClipboardHelper.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <spdlog/spdlog.h>
#endif

namespace shelterops::shell {

bool ClipboardHelper::SetText(const std::string& text) {
#if defined(_WIN32)
    if (!OpenClipboard(nullptr)) {
        spdlog::warn("ClipboardHelper: OpenClipboard failed");
        return false;
    }
    EmptyClipboard();

    std::size_t size = text.size() + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    void* ptr = GlobalLock(hMem);
    if (ptr) {
        std::memcpy(ptr, text.c_str(), size);
        GlobalUnlock(hMem);
    }
    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
    return true;
#else
    (void)text;
    return false;
#endif
}

} // namespace shelterops::shell
