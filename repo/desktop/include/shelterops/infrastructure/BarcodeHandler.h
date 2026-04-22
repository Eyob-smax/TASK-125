#pragma once
#include <string>

namespace shelterops::infrastructure {

struct BarcodeToken {
    std::string value;
    bool        is_printable = false;   // true when value contains only printable ASCII
};

// Pure barcode processing — no Win32 keyboard hook.
// Strips trailing CR and LF characters (USB-wedge scanner output).
// Validates the cleaned value via InventoryRules::ValidateBarcode.
// The UI layer feeds accumulated keystrokes to this handler.
struct BarcodeHandler {
    static BarcodeToken ProcessScan(const std::string& raw_input);
};

} // namespace shelterops::infrastructure
