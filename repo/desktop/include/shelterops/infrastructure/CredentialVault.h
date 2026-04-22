#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <memory>
#include <unordered_map>

namespace shelterops::infrastructure {

// Key names used across the application.
static constexpr const char* kVaultKeyDataKey       = "ShelterOps/DataKey";
static constexpr const char* kVaultKeyAutomationKey = "ShelterOps/AutomationKey";
static constexpr const char* kVaultKeyLanSyncKey    = "ShelterOps/LanSyncKey";

struct VaultEntry {
    std::vector<uint8_t> data;
};

// Abstract interface so tests can inject an in-memory implementation.
class ICredentialVault {
public:
    virtual ~ICredentialVault() = default;

    // Store raw bytes under the given name (replaces if exists).
    virtual void Store(const std::string& name,
                       const std::vector<uint8_t>& data) = 0;

    // Returns the stored bytes, or std::nullopt if not found.
    virtual std::optional<VaultEntry> Load(const std::string& name) = 0;

    // Removes the entry. No-op if not found.
    virtual void Remove(const std::string& name) = 0;
};

// Platform implementation:
//   Windows: Windows Credential Manager (CredWriteW / CredReadW), key bytes
//            wrapped with DPAPI (CryptProtectData) before storage.
//   Non-Windows: file-based fallback in the user's config directory
//            (0600 permissions). FOR CI ONLY — not suitable for production.
class CredentialVault : public ICredentialVault {
public:
    // base_path: used only on non-Windows; ignored on Windows.
    explicit CredentialVault(const std::string& base_path = "");
    ~CredentialVault() override = default;

    void                   Store(const std::string& name,
                                 const std::vector<uint8_t>& data) override;
    std::optional<VaultEntry> Load(const std::string& name) override;
    void                   Remove(const std::string& name) override;

private:
    std::string base_path_;
};

// In-memory implementation for unit tests.
class InMemoryCredentialVault : public ICredentialVault {
public:
    void Store(const std::string& name,
               const std::vector<uint8_t>& data) override;
    std::optional<VaultEntry> Load(const std::string& name) override;
    void Remove(const std::string& name) override;

private:
    std::unordered_map<std::string, VaultEntry> store_;
};

} // namespace shelterops::infrastructure
