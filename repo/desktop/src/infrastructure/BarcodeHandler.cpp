#include "shelterops/infrastructure/BarcodeHandler.h"
#include "shelterops/domain/InventoryRules.h"

namespace shelterops::infrastructure {

BarcodeToken BarcodeHandler::ProcessScan(const std::string& raw_input) {
    // Strip trailing CR, LF, or CRLF as emitted by USB wedge scanners.
    std::string value = raw_input;
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n'))
        value.pop_back();

    // Reject if the stripped value contains non-printable ASCII characters.
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7F) {
            return BarcodeToken{value, false};
        }
    }

    // Validate barcode format via InventoryRules.
    if (!domain::ValidateBarcode(value).is_valid) {
        return BarcodeToken{value, false};
    }

    return BarcodeToken{value, true};
}

} // namespace shelterops::infrastructure
