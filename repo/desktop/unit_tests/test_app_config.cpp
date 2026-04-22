#include <gtest/gtest.h>
#include "shelterops/AppConfig.h"
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

class AppConfigTest : public ::testing::Test {
protected:
    std::string tmp = "test_config_scratch.json";
    void TearDown() override { fs::remove(tmp); }
};

TEST_F(AppConfigTest, DefaultsWhenFileAbsent) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_EQ(cfg.db_path,                 "shelterops.db");
    EXPECT_EQ(cfg.low_stock_days_threshold, 7);
    EXPECT_EQ(cfg.expiration_alert_days,    14);
    EXPECT_EQ(cfg.retention_years_default,  7);
    EXPECT_FALSE(cfg.automation_endpoint_enabled);
    EXPECT_FALSE(cfg.lan_sync_enabled);
    EXPECT_FALSE(cfg.start_in_tray);
    EXPECT_TRUE(cfg.retention_anonymize);
}

TEST_F(AppConfigTest, LoadsFieldsFromJson) {
    std::ofstream f(tmp);
    f << R"({
        "db_path": "custom.db",
        "log_level": "debug",
        "low_stock_days_threshold": 10,
        "expiration_alert_days": 21,
        "retention_years_default": 5,
        "retention_anonymize": false,
        "automation_endpoint_enabled": true,
        "automation_endpoint_port": 28000,
        "automation_rate_limit_rpm": 30,
        "lan_sync_enabled": false,
        "start_in_tray": true
    })";
    f.close();

    auto cfg = shelterops::AppConfig::LoadOrDefault(tmp);
    EXPECT_EQ(cfg.db_path,                    "custom.db");
    EXPECT_EQ(cfg.log_level,                  "debug");
    EXPECT_EQ(cfg.low_stock_days_threshold,    10);
    EXPECT_EQ(cfg.expiration_alert_days,       21);
    EXPECT_EQ(cfg.retention_years_default,     5);
    EXPECT_FALSE(cfg.retention_anonymize);
    EXPECT_TRUE(cfg.automation_endpoint_enabled);
    EXPECT_EQ(cfg.automation_endpoint_port,    28000);
    EXPECT_EQ(cfg.automation_rate_limit_rpm,   30);
    EXPECT_FALSE(cfg.lan_sync_enabled);
    EXPECT_TRUE(cfg.start_in_tray);
}

TEST_F(AppConfigTest, MalformedJsonFallsBackToDefaults) {
    std::ofstream f(tmp);
    f << "not {{{{ valid json";
    f.close();

    auto cfg = shelterops::AppConfig::LoadOrDefault(tmp);
    EXPECT_EQ(cfg.db_path, "shelterops.db");
    EXPECT_FALSE(cfg.automation_endpoint_enabled);
    EXPECT_EQ(cfg.low_stock_days_threshold, 7);
}

TEST_F(AppConfigTest, RoundTripSaveLoad) {
    shelterops::AppConfig src;
    src.db_path                    = "roundtrip_test.db";
    src.low_stock_days_threshold   = 5;
    src.expiration_alert_days      = 30;
    src.automation_endpoint_port   = 29000;
    src.automation_endpoint_enabled = true;
    src.retention_anonymize        = false;
    src.SaveToFile(tmp);

    auto dst = shelterops::AppConfig::LoadOrDefault(tmp);
    EXPECT_EQ(dst.db_path,                      src.db_path);
    EXPECT_EQ(dst.low_stock_days_threshold,      src.low_stock_days_threshold);
    EXPECT_EQ(dst.expiration_alert_days,         src.expiration_alert_days);
    EXPECT_EQ(dst.automation_endpoint_port,      src.automation_endpoint_port);
    EXPECT_EQ(dst.automation_endpoint_enabled,   src.automation_endpoint_enabled);
    EXPECT_EQ(dst.retention_anonymize,           src.retention_anonymize);
}

TEST_F(AppConfigTest, LanSyncDisabledByDefault) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_FALSE(cfg.lan_sync_enabled);
    EXPECT_TRUE(cfg.lan_sync_peer_host.empty());
    EXPECT_EQ(cfg.lan_sync_peer_port,          27316);
    EXPECT_EQ(cfg.lan_sync_pinned_certs_path,  "lan_sync_trusted_peers.json");
}

TEST_F(AppConfigTest, AlertThresholdsArePositive) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_GT(cfg.low_stock_days_threshold, 0);
    EXPECT_GT(cfg.expiration_alert_days,    0);
    EXPECT_GT(cfg.retention_years_default,  0);
}

TEST_F(AppConfigTest, StoragePathDefaultsAreRelative) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    // All storage paths must be relative (no leading slash / drive letter).
    EXPECT_FALSE(cfg.db_path.empty());
    EXPECT_FALSE(cfg.log_dir.empty());
    EXPECT_FALSE(cfg.exports_dir.empty());
    EXPECT_FALSE(cfg.update_metadata_dir.empty());
    EXPECT_FALSE(cfg.trusted_publishers_path.empty());
    // Relative: must not start with '/' or a Windows drive like "C:".
    EXPECT_NE(cfg.db_path[0],                 '/');
    EXPECT_NE(cfg.log_dir[0],                 '/');
    EXPECT_NE(cfg.exports_dir[0],             '/');
    EXPECT_NE(cfg.update_metadata_dir[0],     '/');
    EXPECT_NE(cfg.trusted_publishers_path[0], '/');
}

TEST_F(AppConfigTest, StoragePathDefaultValues) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_EQ(cfg.exports_dir,           "exports");
    EXPECT_EQ(cfg.update_metadata_dir,   "update");
    EXPECT_EQ(cfg.log_dir,               "logs");
    EXPECT_EQ(cfg.trusted_publishers_path, "trusted_publishers.json");
}

TEST_F(AppConfigTest, StoragePathsRoundTrip) {
    shelterops::AppConfig src;
    src.exports_dir         = "data/exports";
    src.update_metadata_dir = "data/update";
    src.SaveToFile(tmp);

    auto dst = shelterops::AppConfig::LoadOrDefault(tmp);
    EXPECT_EQ(dst.exports_dir,         src.exports_dir);
    EXPECT_EQ(dst.update_metadata_dir, src.update_metadata_dir);
}

TEST_F(AppConfigTest, AutomationPortDefault) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_EQ(cfg.automation_endpoint_port, 27315);
    EXPECT_EQ(cfg.automation_rate_limit_rpm, 60);
    EXPECT_FALSE(cfg.automation_endpoint_enabled);
}
