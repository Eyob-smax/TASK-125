#include "shelterops/common/Uuid.h"
#include <sodium.h>
#include <sstream>
#include <iomanip>
#include <array>

namespace shelterops::common {

std::string GenerateUuidV4() {
    std::array<uint8_t, 16> bytes{};
    randombytes_buf(bytes.data(), bytes.size());

    // Set version (4) and variant (RFC 4122).
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            ss << '-';
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return ss.str();
}

} // namespace shelterops::common
