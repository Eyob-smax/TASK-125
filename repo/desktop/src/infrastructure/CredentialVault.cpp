#include "shelterops/infrastructure/CredentialVault.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include <unordered_map>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincred.h>
#include <dpapi.h>
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#endif

namespace fs = std::filesystem;

namespace shelterops::infrastructure {

// ---------------------------------------------------------------------------
// CredentialVault — Windows DPAPI + Credential Manager path
// ---------------------------------------------------------------------------

CredentialVault::CredentialVault(const std::string& base_path)
    : base_path_(base_path) {
#if !defined(_WIN32)
    if (base_path_.empty()) {
        base_path_ = ".shelterops_vault";
    }
    spdlog::warn("CredentialVault: using file-based fallback (CI only). "
                 "Do not use in production.");
#endif
}

#if defined(_WIN32)

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
    return ws;
}

void CredentialVault::Store(const std::string& name,
                             const std::vector<uint8_t>& data) {
    // DPAPI-protect the data before writing to Credential Manager.
    DATA_BLOB in_blob{};
    in_blob.cbData = static_cast<DWORD>(data.size());
    in_blob.pbData = const_cast<BYTE*>(data.data());
    DATA_BLOB out_blob{};
    // Use user-scoped DPAPI (flag 0) so only the current Windows user account
    // can decrypt the key. CRYPTPROTECT_LOCAL_MACHINE would allow any local
    // account to decrypt, undermining per-user PII key isolation.
    if (!CryptProtectData(&in_blob, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
        throw std::runtime_error(
            "CredentialVault::Store: CryptProtectData failed for " + name);
    }

    std::vector<BYTE> protected_data(out_blob.pbData,
                                     out_blob.pbData + out_blob.cbData);
    LocalFree(out_blob.pbData);

    std::wstring wname = ToWide(name);
    CREDENTIALW cred{};
    cred.Type               = CRED_TYPE_GENERIC;
    cred.TargetName         = const_cast<LPWSTR>(wname.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(protected_data.size());
    cred.CredentialBlob     = protected_data.data();
    cred.Persist            = CRED_PERSIST_LOCAL_MACHINE;

    if (!CredWriteW(&cred, 0)) {
        throw std::runtime_error(
            "CredentialVault::Store: CredWriteW failed for " + name);
    }
}

std::optional<VaultEntry> CredentialVault::Load(const std::string& name) {
    std::wstring wname = ToWide(name);
    PCREDENTIALW pcred = nullptr;
    if (!CredReadW(wname.c_str(), CRED_TYPE_GENERIC, 0, &pcred)) {
        DWORD err = GetLastError();
        if (err == ERROR_NOT_FOUND) return std::nullopt;
        throw std::runtime_error(
            "CredentialVault::Load: CredReadW failed for " + name);
    }

    DATA_BLOB in_blob{};
    in_blob.cbData = pcred->CredentialBlobSize;
    in_blob.pbData = pcred->CredentialBlob;
    DATA_BLOB out_blob{};
    BOOL ok = CryptUnprotectData(&in_blob, nullptr, nullptr, nullptr, nullptr,
                                 CRYPTPROTECT_UI_FORBIDDEN, &out_blob);
    CredFree(pcred);

    if (!ok) {
        throw std::runtime_error(
            "CredentialVault::Load: CryptUnprotectData failed for " + name);
    }

    VaultEntry entry;
    entry.data.assign(out_blob.pbData, out_blob.pbData + out_blob.cbData);
    LocalFree(out_blob.pbData);
    return entry;
}

void CredentialVault::Remove(const std::string& name) {
    std::wstring wname = ToWide(name);
    CredDeleteW(wname.c_str(), CRED_TYPE_GENERIC, 0);
}

#else // Non-Windows file-based fallback (CI only)

static std::string SafeFileName(const std::string& name) {
    std::string out;
    for (char c : name) {
        out += (std::isalnum(c) || c == '-' || c == '_') ? c : '_';
    }
    return out;
}

void CredentialVault::Store(const std::string& name,
                             const std::vector<uint8_t>& data) {
    fs::create_directories(base_path_);
    std::string path = base_path_ + "/" + SafeFileName(name) + ".key";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw std::runtime_error(
            "CredentialVault::Store: cannot write " + path);
    }
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    fs::permissions(path,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
}

std::optional<VaultEntry> CredentialVault::Load(const std::string& name) {
    std::string path = base_path_ + "/" + SafeFileName(name) + ".key";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return std::nullopt;
    VaultEntry entry;
    entry.data.assign(std::istreambuf_iterator<char>(f),
                      std::istreambuf_iterator<char>());
    return entry;
}

void CredentialVault::Remove(const std::string& name) {
    std::string path = base_path_ + "/" + SafeFileName(name) + ".key";
    fs::remove(path);
}

#endif

// ---------------------------------------------------------------------------
// InMemoryCredentialVault
// ---------------------------------------------------------------------------

void InMemoryCredentialVault::Store(const std::string& name,
                                     const std::vector<uint8_t>& data) {
    store_[name] = VaultEntry{data};
}

std::optional<VaultEntry> InMemoryCredentialVault::Load(
        const std::string& name) {
    auto it = store_.find(name);
    if (it == store_.end()) return std::nullopt;
    return it->second;
}

void InMemoryCredentialVault::Remove(const std::string& name) {
    store_.erase(name);
}

} // namespace shelterops::infrastructure
