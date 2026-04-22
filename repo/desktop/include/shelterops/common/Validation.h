#pragma once
#include <string>

namespace shelterops::common {

struct ValidationResult {
    bool        ok           = true;
    std::string message_code;  // e.g. "FIELD_EMPTY", "TOO_LONG", "INVALID_EMAIL"
};

// Returns {false, "FIELD_EMPTY"} if value is empty or whitespace-only.
ValidationResult IsNonEmpty(const std::string& value);

// Minimal RFC-5321 shape check: contains exactly one '@', non-empty local and domain.
ValidationResult IsEmailShape(const std::string& value);

// E.164: starts with '+', 8–15 digits after the '+'.
ValidationResult IsE164PhoneShape(const std::string& value);

// All characters are printable ASCII (0x20–0x7E).
ValidationResult IsPrintableAscii(const std::string& value);

// All characters are alphanumeric, '-', or '_'.
ValidationResult IsAlphanumericHyphenUnderscore(const std::string& value);

// Length in bytes is in [min_len, max_len].
ValidationResult IsWithinLength(const std::string& value,
                                  size_t min_len,
                                  size_t max_len);

} // namespace shelterops::common
