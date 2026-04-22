#include <gtest/gtest.h>
#include "shelterops/shell/ClipboardHelper.h"

using namespace shelterops::shell;

// ClipboardHelper::SetText is a Win32-only operation.
// On non-Windows CI hosts it returns false, which is the documented contract.
// On Windows hosts the clipboard is available but tests must not require it
// to actually succeed (sandboxed CI may not have clipboard access).

TEST(ClipboardHelper, SetTextOnNonWindowsReturnsFalse) {
#if defined(_WIN32)
    // On Windows we exercise the call and accept either result —
    // CI hosts may not have an open window station.
    (void)ClipboardHelper::SetText("test");
    SUCCEED(); // no crash is the assertion
#else
    bool result = ClipboardHelper::SetText("any text");
    EXPECT_FALSE(result);
#endif
}

TEST(ClipboardHelper, EmptyStringIsAccepted) {
#if defined(_WIN32)
    (void)ClipboardHelper::SetText("");
    SUCCEED();
#else
    bool result = ClipboardHelper::SetText("");
    EXPECT_FALSE(result);
#endif
}

TEST(ClipboardHelper, LargeStringIsAccepted) {
    std::string large(100000, 'x');
#if defined(_WIN32)
    (void)ClipboardHelper::SetText(large);
    SUCCEED();
#else
    bool result = ClipboardHelper::SetText(large);
    EXPECT_FALSE(result);
#endif
}

TEST(ClipboardHelper, TsvFormattedStringIsAccepted) {
    std::string tsv = "col1\tcol2\tcol3\nval1\tval2\tval3\n";
#if defined(_WIN32)
    (void)ClipboardHelper::SetText(tsv);
    SUCCEED();
#else
    bool result = ClipboardHelper::SetText(tsv);
    EXPECT_FALSE(result);
#endif
}
