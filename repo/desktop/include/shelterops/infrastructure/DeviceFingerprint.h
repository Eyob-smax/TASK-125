#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace shelterops::infrastructure {

// Derives a stable machine ID and computes an HMAC-SHA256 fingerprint
// used for automation endpoint authentication and session device binding.
//
//   Windows : machine_id from HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid
//   Fallback: SHA-256(hostname + first MAC address) encoded as hex
class DeviceFingerprint {
public:
    // Returns the machine GUID string (Windows) or derived hex hash (Linux).
    // Cached after first call; value is stable for the process lifetime.
    static std::string GetMachineId();

    // Computes HMAC-SHA256(machine_id || ":" || operator_username, key).
    // Returns lowercase hex string (64 chars).
    // key must be non-empty; throws std::invalid_argument otherwise.
    static std::string ComputeFingerprint(
        const std::string&         machine_id,
        const std::string&         operator_username,
        const std::vector<uint8_t>& key);

    // Computes a stable session binding fingerprint from machine id + operator.
    // Uses keyed Blake2b (domain separator as key) to derive a per-device HMAC
    // key, then applies HMAC-SHA256(machine_id:operator_username). The domain
    // separator prevents precomputation from machine_id alone.
    // Returns lowercase hex string (64 chars).
    static std::string ComputeSessionBindingFingerprint(
        const std::string& machine_id,
        const std::string& operator_username);
};

} // namespace shelterops::infrastructure
