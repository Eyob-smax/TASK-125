#pragma once
#include <string>

namespace shelterops::common {

// Generates a version-4 UUID string (e.g. "550e8400-e29b-41d4-a716-446655440000")
// using libsodium's cryptographically random byte source.
std::string GenerateUuidV4();

} // namespace shelterops::common
