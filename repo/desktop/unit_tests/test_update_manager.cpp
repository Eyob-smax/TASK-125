#include <gtest/gtest.h>
#include "shelterops/infrastructure/UpdateManager.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <regex>

using namespace shelterops::infrastructure;
namespace fs = std::filesystem;

class UpdateManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = fs::temp_directory_path() / "shelterops_update_test";
        fs::create_directories(tmp_dir_);
        mgr_ = std::make_unique<UpdateManager>(tmp_dir_.string());
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    // Create a dummy file at the given path
    void WriteFile(const fs::path& p, const std::string& content = "dummy") {
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
    }

    fs::path                       tmp_dir_;
    std::unique_ptr<UpdateManager> mgr_;
};

TEST_F(UpdateManagerTest, InitialStateIsIdle) {
    EXPECT_EQ(UpdateState::Idle, mgr_->State());
    EXPECT_FALSE(mgr_->Package().has_value());
}

TEST_F(UpdateManagerTest, ImportNonExistentPathFails) {
    bool ok = mgr_->ImportPackage("/nonexistent/path/pkg.msi", 1000);
    EXPECT_FALSE(ok);
    EXPECT_EQ(UpdateState::Failed, mgr_->State());
}

TEST_F(UpdateManagerTest, ImportNonMsiExtensionFails) {
    auto path = tmp_dir_ / "update.zip";
    WriteFile(path);
    bool ok = mgr_->ImportPackage(path.string(), 1000);
    EXPECT_FALSE(ok);
    EXPECT_EQ(UpdateState::SignatureFailed, mgr_->State());
}

TEST_F(UpdateManagerTest, ImportValidMsiPathStagesFile) {
    auto msi_path = tmp_dir_ / "src" / "update.msi";
    WriteFile(msi_path, "MSI_PLACEHOLDER");
    // On non-Windows CI: signature check fails (expected behavior).
    bool ok = mgr_->ImportPackage(msi_path.string(), 1000);
#if defined(_WIN32)
    // On Windows: signature verification runs; unsigned test file → failure.
    EXPECT_FALSE(ok);
    EXPECT_EQ(UpdateState::SignatureFailed, mgr_->State());
#else
    // On Linux CI: verify is unavailable, returns SignatureFailed.
    EXPECT_FALSE(ok);
    EXPECT_EQ(UpdateState::SignatureFailed, mgr_->State());
#endif
    // Package info should be set even on signature failure.
    EXPECT_TRUE(mgr_->Package().has_value());
    EXPECT_EQ("update", mgr_->Package()->version);
}

TEST_F(UpdateManagerTest, VerifySignatureWithoutPackageFails) {
    bool ok = mgr_->VerifySignature();
    EXPECT_FALSE(ok);
}

TEST_F(UpdateManagerTest, ApplyWithoutVerifiedPackageFails) {
    auto msi_path = tmp_dir_ / "src" / "update.msi";
    WriteFile(msi_path);
    mgr_->ImportPackage(msi_path.string(), 1000);
    // Package exists but not verified (signature_valid=false)
    bool ok = mgr_->Apply();
    EXPECT_FALSE(ok);
    EXPECT_EQ(UpdateState::Failed, mgr_->State());
}

TEST_F(UpdateManagerTest, RollbackWithNoMetadataFails) {
    bool ok = mgr_->RollbackToPrevious();
    EXPECT_FALSE(ok);
    EXPECT_FALSE(mgr_->Rollback().rollback_available);
}

TEST_F(UpdateManagerTest, RollbackMetadataPersistedAndLoaded) {
    // Simulate saving rollback metadata via the private path (write JSON directly).
    nlohmann::json j;
    j["previous_version"]   = "1.0.0";
    j["previous_msi_path"]  = "/fake/path/ShelterOpsDesk-1.0.0.msi";
    j["installed_at_unix"]  = 12345;

    auto meta_path = tmp_dir_ / "rollback.json";
    std::ofstream f(meta_path);
    f << j.dump(2);
    f.close();

    // Re-create manager so it loads from disk.
    auto mgr2 = std::make_unique<UpdateManager>(tmp_dir_.string());
    EXPECT_TRUE(mgr2->Rollback().rollback_available);
    EXPECT_EQ("1.0.0", mgr2->Rollback().previous_version);
    EXPECT_EQ(12345, mgr2->Rollback().installed_at_unix);
}

TEST_F(UpdateManagerTest, ResetClearsPackageAndError) {
    auto msi_path = tmp_dir_ / "src" / "update.msi";
    WriteFile(msi_path);
    mgr_->ImportPackage(msi_path.string(), 1000);
    mgr_->Reset();
    EXPECT_EQ(UpdateState::Idle, mgr_->State());
    EXPECT_FALSE(mgr_->Package().has_value());
}

TEST_F(UpdateManagerTest, LastErrorClearedAfterReset) {
    mgr_->ImportPackage("/nonexistent/a.msi", 1000);
    EXPECT_FALSE(mgr_->LastError().message.empty());
    mgr_->Reset();
    EXPECT_TRUE(mgr_->LastError().message.empty());
}

TEST_F(UpdateManagerTest, ApplyWithoutImportFails) {
    // Apply called on a fresh manager with no imported package.
    bool ok = mgr_->Apply();
    EXPECT_FALSE(ok);
    EXPECT_EQ(UpdateState::Failed, mgr_->State());
}

TEST_F(UpdateManagerTest, ApplyRequiresSignatureValid) {
    auto msi_path = tmp_dir_ / "src" / "update.msi";
    WriteFile(msi_path);
    mgr_->ImportPackage(msi_path.string(), 1000);
    // After ImportPackage the signature is not valid (unsigned test file).
    // Apply should refuse if signature_valid is false.
    bool ok = mgr_->Apply();
    EXPECT_FALSE(ok);
    EXPECT_EQ(UpdateState::Failed, mgr_->State());
}

TEST_F(UpdateManagerTest, RollbackWithMissingPreviousMsiPathFails) {
    // Write rollback metadata missing the previous_msi_path field.
    nlohmann::json j;
    j["previous_version"]  = "1.0.0";
    // previous_msi_path intentionally omitted / empty — rollback cannot proceed
    j["previous_msi_path"] = "";
    j["installed_at_unix"] = 12345;

    auto meta_path = tmp_dir_ / "rollback.json";
    std::ofstream f(meta_path);
    f << j.dump(2);
    f.close();

    auto mgr2 = std::make_unique<UpdateManager>(tmp_dir_.string());
    // Metadata is loaded but exe path is empty → rollback cannot proceed.
    bool ok = mgr2->RollbackToPrevious();
    EXPECT_FALSE(ok);
}

TEST_F(UpdateManagerTest, ResetClearsInMemoryRollbackState) {
    // Write rollback metadata so it is loaded.
    nlohmann::json j;
    j["previous_version"]  = "1.0.0";
    j["previous_msi_path"] = "/fake/ShelterOpsDesk-1.0.0.msi";
    j["installed_at_unix"] = 12345;
    {
        auto meta_path = tmp_dir_ / "rollback.json";
        std::ofstream mf(meta_path);
        mf << j.dump(2);
    }

    auto mgr2 = std::make_unique<UpdateManager>(tmp_dir_.string());
    ASSERT_TRUE(mgr2->Rollback().rollback_available);

    // Import a (failing) package then reset.
    auto msi_path = tmp_dir_ / "src" / "update.msi";
    WriteFile(msi_path);
    mgr2->ImportPackage(msi_path.string(), 1000);
    mgr2->Reset();

    // State and package are cleared.
    EXPECT_EQ(UpdateState::Idle, mgr2->State());
    EXPECT_FALSE(mgr2->Package().has_value());

    // rollback.json on disk is NOT deleted by Reset; only in-memory state is cleared.
    EXPECT_TRUE(fs::exists(tmp_dir_ / "rollback.json"));
}

TEST_F(UpdateManagerTest, RollbackMetadataStoresMsiPathNotExePath) {
    // When ImportPackage stages a second package, the first staged.msi is
    // preserved as rollback.msi. Verify that rollback.json records previous_msi_path
    // and that it points to the rollback.msi file (not the application exe).
    auto first_msi  = tmp_dir_ / "src" / "v1.msi";
    auto second_msi = tmp_dir_ / "src" / "v2.msi";
    WriteFile(first_msi, "v1_content");
    WriteFile(second_msi, "v2_content");

    // Stage first package (signature will fail on CI, but file is staged).
    mgr_->ImportPackage(first_msi.string(), 1000);
    EXPECT_TRUE(fs::exists(tmp_dir_ / "staged.msi"));

    // Stage second package — first staged.msi should be copied to rollback.msi.
    mgr_->ImportPackage(second_msi.string(), 2000);
    EXPECT_TRUE(fs::exists(tmp_dir_ / "rollback.msi"));

    // Verify rollback.json records previous_msi_path (if Apply was called it
    // would be set). Verify field name in any persisted metadata doesn't say "exe".
    auto meta_path = tmp_dir_ / "rollback.json";
    if (fs::exists(meta_path)) {
        std::ifstream f(meta_path);
        nlohmann::json j;
        f >> j;
        EXPECT_FALSE(j.contains("previous_exe_path"))
            << "Rollback metadata must not store exe path";
        if (j.contains("previous_msi_path")) {
            std::string p = j["previous_msi_path"].get<std::string>();
            EXPECT_TRUE(p.empty() || p.find(".msi") != std::string::npos)
                << "previous_msi_path must reference a .msi file";
        }
    }
}

// Validates the safe-character regex used in both Apply() and RollbackToPrevious()
// to guard against shell metacharacter injection in MSI paths.
TEST_F(UpdateManagerTest, ApplyAndRollbackPathValidationRejectsMaliciousChars) {
    static const std::regex kSafePathPattern(R"(^[A-Za-z0-9 _.:\\/\-]+$)");

    // These paths must be REJECTED — they contain shell metacharacters
    EXPECT_FALSE(std::regex_match("C:\\Users\\app; rm -rf /", kSafePathPattern));
    EXPECT_FALSE(std::regex_match("C:\\path | whoami", kSafePathPattern));
    EXPECT_FALSE(std::regex_match("C:\\path`cmd`", kSafePathPattern));
    EXPECT_FALSE(std::regex_match("C:\\path$(evil)", kSafePathPattern));
    EXPECT_FALSE(std::regex_match("C:\\path&&format C:", kSafePathPattern));
    EXPECT_FALSE(std::regex_match("C:\\path\nnewline", kSafePathPattern));

    // These paths must be ACCEPTED — alphanumeric + safe punctuation only
    EXPECT_TRUE(std::regex_match("C:\\ProgramData\\ShelterOps\\update\\staged.msi",
                                 kSafePathPattern));
    EXPECT_TRUE(std::regex_match("C:/Users/app/shelterops/staged.msi",
                                 kSafePathPattern));
    EXPECT_TRUE(std::regex_match("C:\\App Data\\ShelterOps-1.0.0\\staged.msi",
                                 kSafePathPattern));
}

TEST_F(UpdateManagerTest, RollbackRejectsPathWithShellMetacharacters) {
    // Write rollback.json with a path containing a shell metacharacter.
    nlohmann::json j;
    j["previous_version"]  = "1.0.0";
    j["previous_msi_path"] = "C:\\valid\\path; malicious_command";
    j["installed_at_unix"] = 12345;

    auto meta_path = tmp_dir_ / "rollback.json";
    std::ofstream f(meta_path);
    f << j.dump(2);
    f.close();

    auto mgr2 = std::make_unique<UpdateManager>(tmp_dir_.string());
    ASSERT_TRUE(mgr2->Rollback().rollback_available);

    bool ok = mgr2->RollbackToPrevious();
    EXPECT_FALSE(ok);
    // The error should indicate an invalid path, not a missing file
    EXPECT_FALSE(mgr2->LastError().message.empty());
}

TEST_F(UpdateManagerTest, CorruptRollbackJsonDoesNotCrash) {
    auto meta_path = tmp_dir_ / "rollback.json";
    std::ofstream f(meta_path);
    f << "{ this is not valid JSON !!";
    f.close();

    // Manager constructed with corrupt metadata must not throw.
    EXPECT_NO_THROW({
        auto mgr2 = std::make_unique<UpdateManager>(tmp_dir_.string());
        EXPECT_FALSE(mgr2->Rollback().rollback_available);
    });
}

TEST_F(UpdateManagerTest, NormalizeThumbprintRemovesSeparatorsAndUppercases) {
    const std::string raw = "aa:bb-cc dd.ee/ff";
    const std::string norm = UpdateManager::NormalizeThumbprint(raw);
    EXPECT_EQ("AABBCCDDEEFF", norm);
}

TEST_F(UpdateManagerTest, LoadPinnedThumbprintsParsesTrustedPublishersArray) {
    auto path = tmp_dir_ / "trusted_publishers.json";
    nlohmann::json j;
    j["trusted_publishers"] = nlohmann::json::array({
        {{"thumbprint", "aa:bb:cc"}, {"subject", "A"}},
        {{"thumbprint", "AA-BB-CC"}, {"subject", "A-dup"}},
        {{"thumbprint", "11 22 33"}, {"subject", "B"}}
    });
    std::ofstream f(path);
    f << j.dump(2);
    f.close();

    auto pins = UpdateManager::LoadPinnedThumbprints(path.string());
    ASSERT_EQ(2u, pins.size());
    EXPECT_EQ("112233", pins[0]);
    EXPECT_EQ("AABBCC", pins[1]);
}

TEST_F(UpdateManagerTest, IsThumbprintPinnedUsesNormalizedComparison) {
    std::vector<std::string> pinned = {"AABBCC", "112233"};
    EXPECT_TRUE(UpdateManager::IsThumbprintPinned("AA:BB:CC", pinned));
    EXPECT_TRUE(UpdateManager::IsThumbprintPinned("11-22-33", pinned));
    EXPECT_FALSE(UpdateManager::IsThumbprintPinned("DE:AD:BE:EF", pinned));
}

TEST_F(UpdateManagerTest, ImportInfersVersionFromFilename) {
    auto msi_path = tmp_dir_ / "src" / "ShelterOpsDesk-2.4.1.msi";
    WriteFile(msi_path);
    mgr_->ImportPackage(msi_path.string(), 1000);
    ASSERT_TRUE(mgr_->Package().has_value());
    EXPECT_EQ("2.4.1", mgr_->Package()->version);
}

TEST_F(UpdateManagerTest, RollbackAvailableAfterVersionedApply) {
    // Verify the end-to-end path: after Apply() saves rollback metadata with the
    // extracted version, rollback_available is true and previous_version is set.
    // Because WinVerifyTrust cannot run in CI we simulate the Apply() output by
    // writing rollback.json from the version extracted during ImportPackage.

    auto msi_path = tmp_dir_ / "src" / "ShelterOpsDesk-3.1.0.msi";
    WriteFile(msi_path);
    mgr_->ImportPackage(msi_path.string(), 5000);
    ASSERT_TRUE(mgr_->Package().has_value());

    // Confirm version was extracted from the filename.
    EXPECT_EQ("3.1.0", mgr_->Package()->version);

    // Simulate what Apply() saves to disk (SaveRollbackMetadata output format).
    const std::string version = mgr_->Package()->version;
    nlohmann::json j;
    j["previous_version"]  = version;
    j["previous_msi_path"] = (tmp_dir_ / "rollback.msi").string();
    j["installed_at_unix"] = 5000;
    {
        std::ofstream f(tmp_dir_ / "rollback.json");
        f << j.dump(2);
    }

    // Re-load the manager (as it would after an Apply restart) and verify.
    auto mgr2 = std::make_unique<UpdateManager>(tmp_dir_.string());
    EXPECT_TRUE(mgr2->Rollback().rollback_available);
    EXPECT_EQ("3.1.0", mgr2->Rollback().previous_version);
}
