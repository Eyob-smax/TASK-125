#include "shelterops/common/Validation.h"
#include <algorithm>
#include <cctype>

namespace shelterops::common {

ValidationResult IsNonEmpty(const std::string& value) {
    bool all_ws = std::all_of(value.begin(), value.end(),
                               [](unsigned char c){ return std::isspace(c); });
    if (value.empty() || all_ws)
        return {false, "FIELD_EMPTY"};
    return {true, {}};
}

ValidationResult IsEmailShape(const std::string& value) {
    auto at_pos = value.find('@');
    if (at_pos == std::string::npos || at_pos == 0)
        return {false, "INVALID_EMAIL"};
    auto second_at = value.find('@', at_pos + 1);
    if (second_at != std::string::npos)
        return {false, "INVALID_EMAIL"};
    if (at_pos + 1 >= value.size())
        return {false, "INVALID_EMAIL"};
    // Domain must contain at least one dot after '@'
    auto dot_pos = value.find('.', at_pos + 1);
    if (dot_pos == std::string::npos || dot_pos + 1 >= value.size())
        return {false, "INVALID_EMAIL"};
    return {true, {}};
}

ValidationResult IsE164PhoneShape(const std::string& value) {
    if (value.empty() || value[0] != '+')
        return {false, "INVALID_PHONE"};
    size_t digit_count = 0;
    for (size_t i = 1; i < value.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(value[i])))
            return {false, "INVALID_PHONE"};
        ++digit_count;
    }
    if (digit_count < 7 || digit_count > 15)
        return {false, "INVALID_PHONE"};
    return {true, {}};
}

ValidationResult IsPrintableAscii(const std::string& value) {
    for (unsigned char c : value) {
        if (c < 0x20 || c > 0x7E)
            return {false, "INVALID_CHARACTERS"};
    }
    return {true, {}};
}

ValidationResult IsAlphanumericHyphenUnderscore(const std::string& value) {
    for (unsigned char c : value) {
        if (!std::isalnum(c) && c != '-' && c != '_')
            return {false, "INVALID_CHARACTERS"};
    }
    return {true, {}};
}

ValidationResult IsWithinLength(const std::string& value,
                                  size_t min_len,
                                  size_t max_len) {
    if (value.size() < min_len)
        return {false, "TOO_SHORT"};
    if (value.size() > max_len)
        return {false, "TOO_LONG"};
    return {true, {}};
}

} // namespace shelterops::common
