#include "shelterops/infrastructure/UpdateManager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <regex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#endif

namespace shelterops::infrastructure {

namespace fs = std::filesystem;

namespace {

std::string InferVersionFromPath(const std::string& path) {
    if (path.empty()) return "";
    const std::string stem = fs::path(path).stem().string();
    const std::regex semver_pattern(R"((\d+\.\d+\.\d+(?:\.\d+)?))");
    std::smatch match;
    if (std::regex_search(stem, match, semver_pattern) && match.size() > 1) {
        return match[1].str();
    }
    return stem;
}

} // namespace

UpdateManager::UpdateManager(const std::string& metadata_dir,
                             const std::string& trusted_publishers_path)
    : metadata_dir_(metadata_dir)
    , trusted_publishers_path_(trusted_publishers_path)
{
    LoadRollbackMetadata();
}

std::string UpdateManager::NormalizeThumbprint(const std::string& thumbprint) {
    std::string out;
    out.reserve(thumbprint.size());
    for (unsigned char c : thumbprint) {
        if (std::isxdigit(c)) {
            out.push_back(static_cast<char>(std::toupper(c)));
        }
    }
    return out;
}

std::vector<std::string> UpdateManager::LoadPinnedThumbprints(const std::string& json_path) {
    std::vector<std::string> out;
    std::ifstream f(json_path);
    if (!f.is_open()) return out;

    try {
        nlohmann::json j;
        f >> j;

        auto append_norm = [&](const nlohmann::json& node) {
            if (!node.is_string()) return;
            std::string norm = NormalizeThumbprint(node.get<std::string>());
            if (!norm.empty()) out.push_back(norm);
        };

        if (j.is_object() && j.contains("trusted_publishers") && j["trusted_publishers"].is_array()) {
            for (const auto& entry : j["trusted_publishers"]) {
                if (entry.is_object() && entry.contains("thumbprint")) {
                    append_norm(entry["thumbprint"]);
                }
            }
        } else if (j.is_array()) {
            for (const auto& entry : j) {
                if (entry.is_object() && entry.contains("thumbprint")) {
                    append_norm(entry["thumbprint"]);
                } else {
                    append_norm(entry);
                }
            }
        }
    } catch (...) {
        return {};
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool UpdateManager::IsThumbprintPinned(
    const std::string& thumbprint,
    const std::vector<std::string>& pinned_thumbprints) {
    std::string norm = NormalizeThumbprint(thumbprint);
    if (norm.empty()) return false;
    return std::find(pinned_thumbprints.begin(), pinned_thumbprints.end(), norm)
        != pinned_thumbprints.end();
}

bool UpdateManager::ImportPackage(const std::string& msi_path, int64_t now_unix) {
    Reset();
    state_ = UpdateState::Verifying;

    if (!fs::exists(msi_path)) {
        last_error_ = { common::ErrorCode::NotFound,
                        "Package not found: " + msi_path };
        state_ = UpdateState::Failed;
        return false;
    }

    if (fs::path(msi_path).extension() != ".msi") {
        last_error_ = { common::ErrorCode::InvalidInput,
                        "Import requires a .msi package." };
        state_ = UpdateState::SignatureFailed;
        return false;
    }

    // Stage the file into metadata_dir_ / "staged.msi".
    // Before overwriting, preserve any existing staged.msi as rollback.msi so
    // RollbackToPrevious() can reinstall the previously-applied package.
    auto staged_path   = fs::path(metadata_dir_) / "staged.msi";
    auto rollback_path = fs::path(metadata_dir_) / "rollback.msi";
    try {
        fs::create_directories(metadata_dir_);
        if (fs::exists(staged_path)) {
            try {
                fs::copy_file(staged_path, rollback_path,
                              fs::copy_options::overwrite_existing);
            } catch (...) {
                spdlog::warn("UpdateManager: could not preserve rollback installer");
            }
        }
        fs::copy_file(msi_path, staged_path,
                      fs::copy_options::overwrite_existing);
    } catch (const std::exception& e) {
        last_error_ = { common::ErrorCode::Internal,
                        std::string("Failed to stage package: ") + e.what() };
        state_ = UpdateState::Failed;
        return false;
    }

    UpdatePackageInfo info;
    info.version = InferVersionFromPath(msi_path);
    info.package_path = staged_path.string();
    info.sha256_hex   = "(computed-on-verify)";
    info.signed_at_unix = now_unix;
    package_ = info;

    if (!DoVerify()) {
        state_ = UpdateState::SignatureFailed;
        return false;
    }

    state_ = UpdateState::Staged;
    spdlog::info("UpdateManager: package staged at {}", staged_path.string());
    return true;
}

bool UpdateManager::VerifySignature() {
    if (!package_) {
        last_error_ = { common::ErrorCode::Internal, "No package staged." };
        return false;
    }
    return DoVerify();
}

bool UpdateManager::DoVerify() {
#if defined(_WIN32)
    auto to_colon_hex = [](const BYTE* bytes, DWORD count) -> std::string {
        static const char* kHex = "0123456789ABCDEF";
        std::string out;
        if (!bytes || count == 0) return out;
        out.reserve(static_cast<size_t>(count) * 3U);
        for (DWORD i = 0; i < count; ++i) {
            if (i > 0) out.push_back(':');
            out.push_back(kHex[(bytes[i] >> 4) & 0xF]);
            out.push_back(kHex[bytes[i] & 0xF]);
        }
        return out;
    };

    auto cert_thumbprint = [&](PCCERT_CONTEXT cert) -> std::string {
        if (!cert) return "";
        DWORD cb = 0;
        if (!CertGetCertificateContextProperty(cert, CERT_SHA256_HASH_PROP_ID, nullptr, &cb) || cb == 0) {
            return "";
        }
        std::vector<BYTE> buf(cb);
        if (!CertGetCertificateContextProperty(cert, CERT_SHA256_HASH_PROP_ID, buf.data(), &cb)) {
            return "";
        }
        return to_colon_hex(buf.data(), cb);
    };

    auto cert_subject = [&](PCCERT_CONTEXT cert) -> std::string {
        if (!cert) return "";
        char name[512] = {0};
        DWORD len = CertGetNameStringA(cert,
            CERT_NAME_SIMPLE_DISPLAY_TYPE,
            0,
            nullptr,
            name,
            static_cast<DWORD>(sizeof(name)));
        if (len <= 1) return "";
        return std::string(name);
    };

    std::wstring wpath(package_->package_path.begin(), package_->package_path.end());

    WINTRUST_FILE_INFO file_info = {};
    file_info.cbStruct       = sizeof(file_info);
    file_info.pcwszFilePath  = wpath.c_str();

    GUID policy_guid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA trust_data = {};
    trust_data.cbStruct            = sizeof(trust_data);
    trust_data.dwUIChoice          = WTD_UI_NONE;
    trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust_data.dwUnionChoice       = WTD_CHOICE_FILE;
    trust_data.pFile               = &file_info;
    trust_data.dwStateAction       = WTD_STATEACTION_VERIFY;
    trust_data.dwProvFlags         = WTD_SAFER_FLAG;

    LONG result = WinVerifyTrust(NULL, &policy_guid, &trust_data);

    auto close_state = [&]() {
        trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(NULL, &policy_guid, &trust_data);
    };

    if (result != ERROR_SUCCESS) {
        last_error_ = { common::ErrorCode::InvalidInput,
                        "Signature verification failed (HRESULT=" +
                        std::to_string(result) + ")" };
        package_->signature_valid = false;
        spdlog::warn("UpdateManager: signature check failed, code={}", result);
        close_state();
        return false;
    }

    // Extract signer certificate from WinVerifyTrust state and pin-check thumbprint.
    CRYPT_PROVIDER_DATA* prov_data = WTHelperProvDataFromStateData(trust_data.hWVTStateData);
    if (!prov_data) {
        last_error_ = { common::ErrorCode::SignatureInvalid,
                        "Signature state data unavailable for signer extraction." };
        package_->signature_valid = false;
        close_state();
        return false;
    }

    CRYPT_PROVIDER_SGNR* signer = WTHelperGetProvSignerFromChain(prov_data, 0, FALSE, 0);
    if (!signer) {
        last_error_ = { common::ErrorCode::SignatureInvalid,
                        "Signer chain unavailable." };
        package_->signature_valid = false;
        close_state();
        return false;
    }

    CRYPT_PROVIDER_CERT* cert = WTHelperGetProvCertFromChain(signer, 0);
    if (!cert || !cert->pCert) {
        last_error_ = { common::ErrorCode::SignatureInvalid,
                        "Signer certificate unavailable." };
        package_->signature_valid = false;
        close_state();
        return false;
    }

    const std::string thumbprint = cert_thumbprint(cert->pCert);
    const std::string subject    = cert_subject(cert->pCert);
    package_->signer_thumbprint  = thumbprint;
    package_->signer_subject     = subject;

    auto pinned = LoadPinnedThumbprints(trusted_publishers_path_);
    if (pinned.empty()) {
        last_error_ = { common::ErrorCode::SignatureInvalid,
                        "No trusted publishers configured at: " + trusted_publishers_path_ };
        package_->signature_valid = false;
        close_state();
        return false;
    }

    if (!IsThumbprintPinned(thumbprint, pinned)) {
        last_error_ = { common::ErrorCode::SignatureInvalid,
                        "Signer thumbprint is not trusted." };
        package_->signature_valid = false;
        spdlog::warn("UpdateManager: signer thumbprint not pinned: {}", thumbprint);
        close_state();
        return false;
    }

    package_->signature_valid = true;
    spdlog::info("UpdateManager: signature verified for {} (subject='{}')",
                 package_->package_path, package_->signer_subject);
    close_state();
    return true;
#else
    // On non-Win32 CI builds: WinVerifyTrust is unavailable.
    // Deliberately return failure so tests can exercise the failed path.
    last_error_ = { common::ErrorCode::InvalidInput,
                    "Signature verification not available on this platform." };
    if (package_) package_->signature_valid = false;
    return false;
#endif
}

bool UpdateManager::Apply(ProgressCallback progress_cb) {
    if (!package_ || !package_->signature_valid) {
        last_error_ = { common::ErrorCode::InvalidInput,
                        "Cannot apply an unverified package." };
        state_ = UpdateState::Failed;
        return false;
    }

    // Save rollback metadata: record the rollback.msi path (the previously-applied
    // installer preserved during ImportPackage). This is what msiexec needs to
    // reinstall the old version, not the current exe path.
    auto rollback_msi = fs::path(metadata_dir_) / "rollback.msi";
    std::string rollback_msi_path = fs::exists(rollback_msi) ? rollback_msi.string() : "";
    std::string rollback_version = InferVersionFromPath(rollback_msi_path);
    if (rollback_version.empty()) {
        rollback_version = rollback_.previous_version;
    }
    SaveRollbackMetadata(rollback_version, rollback_msi_path, package_->signed_at_unix);

    state_ = UpdateState::Applying;
    if (progress_cb) progress_cb(0.0f);

#if defined(_WIN32)
    // Validate that the staged package path is within metadata_dir_ and contains
    // only safe characters — prevents shell injection if the staged path is tampered.
    {
        std::error_code ec;
        const fs::path expected_base = fs::canonical(metadata_dir_, ec);
        if (ec) {
            last_error_ = { common::ErrorCode::Internal,
                            "Cannot resolve metadata directory." };
            state_ = UpdateState::Failed;
            return false;
        }

        const fs::path msi_path(package_->package_path);
        const fs::path msi_canonical = fs::weakly_canonical(msi_path);
        const std::string msi_str  = msi_canonical.string();
        const std::string base_str = expected_base.string();
        const bool is_under_base =
            msi_str.size() >= base_str.size() &&
            msi_str.substr(0, base_str.size()) == base_str;

        static const std::regex kSafePathPattern(R"(^[A-Za-z0-9 _.:\\/\-]+$)");
        const bool has_safe_chars =
            std::regex_match(package_->package_path, kSafePathPattern);

        if (!is_under_base || !has_safe_chars) {
            last_error_ = { common::ErrorCode::InvalidInput,
                            "Staged MSI path is outside the expected directory or "
                            "contains disallowed characters." };
            state_ = UpdateState::Failed;
            spdlog::error("UpdateManager: apply path rejected: {}",
                          package_->package_path);
            return false;
        }

        if (!fs::exists(msi_path)) {
            last_error_ = { common::ErrorCode::NotFound,
                            "Staged installer not found at: " + package_->package_path };
            state_ = UpdateState::Failed;
            return false;
        }
    }

    // Use CreateProcessW instead of std::system to avoid shell injection.
    {
        std::wstring wmsi(package_->package_path.begin(),
                          package_->package_path.end());
        std::wstring cmdline = L"msiexec /quiet /norestart /i \"" + wmsi + L"\"";

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        BOOL created = CreateProcessW(
            nullptr,
            cmdline.data(),
            nullptr, nullptr,
            FALSE,
            0,
            nullptr, nullptr,
            &si, &pi);

        if (!created) {
            last_error_ = { common::ErrorCode::Internal,
                            "CreateProcessW failed (apply), error=" +
                            std::to_string(GetLastError()) };
            state_ = UpdateState::Failed;
            return false;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD rc = 0;
        GetExitCodeProcess(pi.hProcess, &rc);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (progress_cb) progress_cb(1.0f);

        if (rc != 0) {
            last_error_ = { common::ErrorCode::Internal,
                            "msiexec returned non-zero: " + std::to_string(rc) };
            state_ = UpdateState::Failed;
            spdlog::error("UpdateManager: msiexec failed rc={}", rc);
            return false;
        }
    }

    state_ = UpdateState::Applied;
    spdlog::info("UpdateManager: package applied successfully");
    return true;
#else
    last_error_ = { common::ErrorCode::Internal,
                    "Apply requires Windows and msiexec." };
    state_ = UpdateState::Failed;
    return false;
#endif
}

bool UpdateManager::RollbackToPrevious() {
    if (!rollback_.rollback_available) {
        last_error_ = { common::ErrorCode::NotFound,
                        "No rollback metadata available." };
        return false;
    }

    state_ = UpdateState::RollingBack;
    spdlog::info("UpdateManager: rolling back to version {}",
                 rollback_.previous_version);

#if defined(_WIN32)
    // Validate that the rollback path is within metadata_dir_ and contains no
    // shell metacharacters. This guards against tampered rollback.json injecting
    // arbitrary commands via path manipulation.
    {
        std::error_code ec;
        const fs::path expected_base = fs::canonical(metadata_dir_, ec);
        if (ec || rollback_.previous_msi_path.empty()) {
            last_error_ = { common::ErrorCode::NotFound,
                            "Invalid rollback metadata directory." };
            state_ = UpdateState::Failed;
            return false;
        }

        // Reject any path not rooted in metadata_dir_.
        const fs::path msi_path(rollback_.previous_msi_path);
        const fs::path msi_canonical = fs::weakly_canonical(msi_path);
        const std::string msi_str    = msi_canonical.string();
        const std::string base_str   = expected_base.string();
        const bool is_under_base =
            msi_str.size() >= base_str.size() &&
            msi_str.substr(0, base_str.size()) == base_str;

        // Also reject shell metacharacters that could escape the quoted arg.
        static const std::regex kSafePathPattern(R"(^[A-Za-z0-9 _.:\\/\-]+$)");
        const bool has_safe_chars =
            std::regex_match(rollback_.previous_msi_path, kSafePathPattern);

        if (!is_under_base || !has_safe_chars) {
            last_error_ = { common::ErrorCode::InvalidInput,
                            "Rollback MSI path is outside the expected directory or "
                            "contains disallowed characters." };
            state_ = UpdateState::Failed;
            spdlog::error("UpdateManager: rollback path rejected: {}",
                          rollback_.previous_msi_path);
            return false;
        }

        if (!fs::exists(msi_path)) {
            last_error_ = { common::ErrorCode::NotFound,
                            "Previous installer not found at: " +
                            rollback_.previous_msi_path };
            state_ = UpdateState::Failed;
            return false;
        }
    }

    // Use CreateProcessW instead of std::system to avoid shell injection.
    {
        std::wstring wmsi(rollback_.previous_msi_path.begin(),
                          rollback_.previous_msi_path.end());
        std::wstring cmdline = L"msiexec /quiet /norestart /i \"" + wmsi + L"\"";

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        BOOL created = CreateProcessW(
            nullptr,
            cmdline.data(),
            nullptr, nullptr,
            FALSE,
            0,
            nullptr, nullptr,
            &si, &pi);

        if (!created) {
            last_error_ = { common::ErrorCode::Internal,
                            "CreateProcessW failed (rollback), error=" +
                            std::to_string(GetLastError()) };
            state_ = UpdateState::Failed;
            return false;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD rc = 0;
        GetExitCodeProcess(pi.hProcess, &rc);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (rc != 0) {
            last_error_ = { common::ErrorCode::Internal,
                            "Rollback msiexec returned: " + std::to_string(rc) };
            state_ = UpdateState::Failed;
            return false;
        }
    }
#else
    last_error_ = { common::ErrorCode::Internal,
                    "Rollback requires Windows." };
    state_ = UpdateState::Failed;
    return false;
#endif

    state_ = UpdateState::RolledBack;
    return true;
}

void UpdateManager::LoadRollbackMetadata() {
    auto path = fs::path(metadata_dir_) / "rollback.json";
    if (!fs::exists(path)) return;

    try {
        std::ifstream f(path);
        nlohmann::json j;
        f >> j;
        rollback_.previous_version  = j.value("previous_version", "");
        rollback_.previous_msi_path = j.value("previous_msi_path", "");
        rollback_.installed_at_unix  = j.value("installed_at_unix", int64_t{0});
        rollback_.rollback_available =
            !rollback_.previous_version.empty() && !rollback_.previous_msi_path.empty();
    } catch (const std::exception& e) {
        spdlog::warn("UpdateManager: could not load rollback metadata: {}", e.what());
    }
}

void UpdateManager::SaveRollbackMetadata(
    const std::string& version,
    const std::string& msi_path,
    int64_t now_unix)
{
    try {
        fs::create_directories(metadata_dir_);
        nlohmann::json j;
        j["previous_version"]   = version;
        j["previous_msi_path"]  = msi_path;
        j["installed_at_unix"]  = now_unix;

        auto path = fs::path(metadata_dir_) / "rollback.json";
        std::ofstream f(path);
        f << j.dump(2);

        rollback_.previous_version   = version;
        rollback_.previous_msi_path  = msi_path;
        rollback_.installed_at_unix  = now_unix;
        rollback_.rollback_available = !version.empty() && !msi_path.empty();
    } catch (const std::exception& e) {
        spdlog::warn("UpdateManager: could not save rollback metadata: {}", e.what());
    }
}

void UpdateManager::Reset() noexcept {
    state_      = UpdateState::Idle;
    package_.reset();
    last_error_ = {};
}

} // namespace shelterops::infrastructure
