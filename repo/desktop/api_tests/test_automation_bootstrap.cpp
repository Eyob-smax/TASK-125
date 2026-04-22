#include <gtest/gtest.h>
#include "shelterops/AppConfig.h"

// Verifies that the automation endpoint configuration defaults are safe
// (disabled, valid port range, positive rate limit) before the
// AutomationEndpoint class is wired in later scaffolding.

TEST(AutomationEndpointConfig, DisabledByDefault) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_FALSE(cfg.automation_endpoint_enabled)
        << "Automation endpoint must be off by default (security requirement)";
}

TEST(AutomationEndpointConfig, DefaultPortIsUnprivileged) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_GT(cfg.automation_endpoint_port, static_cast<uint16_t>(1024))
        << "Port must be > 1024 (unprivileged)";
    EXPECT_LT(cfg.automation_endpoint_port, static_cast<uint16_t>(65535))
        << "Port must be < 65535";
}

TEST(AutomationEndpointConfig, RateLimitIsPositive) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_GT(cfg.automation_rate_limit_rpm, 0)
        << "Rate limit must be a positive integer";
}

TEST(AutomationEndpointConfig, LanSyncDisabledByDefault) {
    auto cfg = shelterops::AppConfig::LoadOrDefault("__no_such_file__.json");
    EXPECT_FALSE(cfg.lan_sync_enabled)
        << "LAN sync must be off by default (explicit opt-in required)";
}
