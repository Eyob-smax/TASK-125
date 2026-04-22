#pragma once
#include "shelterops/common/ErrorEnvelope.h"
#include <string>
#include <optional>
#include <cstdint>
#include <functional>
#include <vector>

namespace shelterops::infrastructure {

enum class UpdateState {
    Idle,
    Verifying,
    SignatureOk,
    SignatureFailed,
    Staged,
    Applying,
    Applied,
    Failed,
    RollingBack,
    RolledBack
};

struct UpdatePackageInfo {
    std::string version;          // semantic version from MSI property table
    std::string package_path;     // absolute path to the staged .msi
    std::string sha256_hex;       // declared hash from signature block
    std::string signer_subject;   // CN of the signing certificate
    std::string signer_thumbprint; // SHA-256 thumbprint (hex, colon-separated)
    int64_t     signed_at_unix    = 0;
    bool        signature_valid   = false;
};

struct RollbackMetadata {
    std::string previous_version;
    std::string previous_msi_path;  // path to the previous installer .msi (not exe)
    int64_t     installed_at_unix  = 0;
    bool        rollback_available = false;
};

// UpdateManager drives the signed .msi update lifecycle:
//   ImportPackage → Verify → Stage → Apply → (Rollback if needed).
//
// Platform note:
//   - Signature verification is implemented on Win32 via WinVerifyTrust.
//   - On non-Win32 platforms (Linux CI), verification is a no-op that returns
//     SignatureFailed so CI builds can assert the failure path.
//   - Apply launches msiexec /quiet /norestart and waits for its exit code.
//   - Rollback reverts to the previous version recorded in rollback metadata.
class UpdateManager {
public:
    // progress_cb is called periodically with 0.0→1.0 during Apply.
    using ProgressCallback = std::function<void(float progress)>;

    explicit UpdateManager(const std::string& metadata_dir,
                           const std::string& trusted_publishers_path = "trusted_publishers.json");

    UpdateState                       State()     const noexcept { return state_; }
    const std::optional<UpdatePackageInfo>& Package() const noexcept { return package_; }
    const RollbackMetadata&           Rollback()  const noexcept { return rollback_; }
    const common::ErrorEnvelope&      LastError() const noexcept { return last_error_; }

    // Import a .msi from the given path into the staging area.
    // Sets state=Verifying, then Staged on success or SignatureFailed on failure.
    bool ImportPackage(const std::string& msi_path, int64_t now_unix);

    // Explicit re-verify of the staged package (e.g., after user reviews info).
    bool VerifySignature();

    // Persist rollback metadata for the currently installed version, then
    // launch the installer. Sets state=Applying; on msiexec success → Applied,
    // on failure → Failed.
    bool Apply(ProgressCallback progress_cb = nullptr);

    // Restore the previous version recorded in rollback metadata.
    // Sets state=RollingBack; on success → RolledBack.
    bool RollbackToPrevious();

    // Load persisted rollback metadata from the metadata directory.
    void LoadRollbackMetadata();

    void Reset() noexcept;

    // Utility helpers used by signature pinning and unit tests.
    // Normalization removes non-hex chars and uppercases.
    static std::string NormalizeThumbprint(const std::string& thumbprint);
    static std::vector<std::string> LoadPinnedThumbprints(const std::string& json_path);
    static bool IsThumbprintPinned(const std::string& thumbprint,
                                   const std::vector<std::string>& pinned_thumbprints);

private:
    bool DoVerify();
    void SaveRollbackMetadata(const std::string& version,
                               const std::string& msi_path,
                               int64_t now_unix);

    std::string metadata_dir_;
    std::string trusted_publishers_path_;

    UpdateState                      state_    = UpdateState::Idle;
    std::optional<UpdatePackageInfo> package_;
    RollbackMetadata                 rollback_;
    common::ErrorEnvelope            last_error_;
};

} // namespace shelterops::infrastructure
