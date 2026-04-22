#include "shelterops/infrastructure/DeviceFingerprint.h"
#include <sodium.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace shelterops::infrastructure {

namespace {

static std::string g_machine_id;
static std::mutex  g_machine_id_mutex;

static std::string BytesToHex(const uint8_t* data, size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        ss << std::setw(2) << static_cast<int>(data[i]);
    return ss.str();
}

#if defined(_WIN32)
static std::string ReadMachineGuid() {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ, &hkey) != ERROR_SUCCESS) {
        return "";
    }
    wchar_t buf[64] = {};
    DWORD   size    = sizeof(buf);
    DWORD   type    = REG_SZ;
    bool ok = RegQueryValueExW(hkey, L"MachineGuid", nullptr, &type,
                               reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS;
    RegCloseKey(hkey);
    if (!ok) return "";
    // Convert wide to narrow UTF-8
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &out[0], n, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}
#else
static std::string DeriveFromHostname() {
    char hostname[256] = {};
    gethostname(hostname, sizeof(hostname));
    std::string input(hostname);
    uint8_t hash[crypto_generichash_BYTES];
    crypto_generichash(hash, sizeof(hash),
                       reinterpret_cast<const uint8_t*>(input.data()),
                       input.size(), nullptr, 0);
    return BytesToHex(hash, sizeof(hash));
}
#endif

} // anonymous namespace

std::string DeviceFingerprint::GetMachineId() {
    std::lock_guard<std::mutex> lock(g_machine_id_mutex);
    if (!g_machine_id.empty()) return g_machine_id;

#if defined(_WIN32)
    g_machine_id = ReadMachineGuid();
    if (g_machine_id.empty()) {
        spdlog::warn("DeviceFingerprint: could not read MachineGuid; using fallback");
        g_machine_id = "unknown-machine";
    }
#else
    g_machine_id = DeriveFromHostname();
#endif
    return g_machine_id;
}

std::string DeviceFingerprint::ComputeFingerprint(
        const std::string&         machine_id,
        const std::string&         operator_username,
        const std::vector<uint8_t>& key) {
    if (key.empty()) {
        throw std::invalid_argument(
            "DeviceFingerprint::ComputeFingerprint: key must not be empty");
    }

    // Message: machine_id + ":" + operator_username
    std::string msg = machine_id + ":" + operator_username;

    uint8_t out[crypto_auth_hmacsha256_BYTES];
    // Use crypto_auth_hmacsha256 for HMAC-SHA256
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, key.data(), key.size());
    crypto_auth_hmacsha256_update(
        &state,
        reinterpret_cast<const uint8_t*>(msg.data()),
        msg.size());
    crypto_auth_hmacsha256_final(&state, out);

    return BytesToHex(out, sizeof(out));
}

std::string DeviceFingerprint::ComputeSessionBindingFingerprint(
        const std::string& machine_id,
        const std::string& operator_username) {
    // Derive a per-device binding key using keyed Blake2b (libsodium
    // crypto_generichash with a non-null key). The application-specific domain
    // separator acts as the key, ensuring an attacker who knows the machine_id
    // cannot reproduce the binding key without knowledge of this constant.
    static const uint8_t kDomainKey[] = "ShelterOps-SessionBinding-v1";
    static const size_t kDomainKeyLen = sizeof(kDomainKey) - 1; // exclude NUL

    uint8_t binding_key[crypto_auth_hmacsha256_KEYBYTES];
    crypto_generichash(binding_key, sizeof(binding_key),
                       reinterpret_cast<const uint8_t*>(machine_id.data()),
                       machine_id.size(),
                       kDomainKey, kDomainKeyLen);

    const std::string msg = machine_id + ":" + operator_username;
    uint8_t out[crypto_auth_hmacsha256_BYTES];
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, binding_key, sizeof(binding_key));
    crypto_auth_hmacsha256_update(
        &state,
        reinterpret_cast<const uint8_t*>(msg.data()),
        msg.size());
    crypto_auth_hmacsha256_final(&state, out);
    return BytesToHex(out, sizeof(out));
}

} // namespace shelterops::infrastructure
