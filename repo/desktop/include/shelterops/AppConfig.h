#pragma once
#include <string>
#include <cstdint>

namespace shelterops {

struct AppConfig {
    // Database
    std::string  db_path                    = "shelterops.db";

    // Logging
    std::string  log_dir                    = "logs";
    std::string  log_level                  = "info";       // trace|debug|info|warn|error|critical

    // UI / shell
    bool         high_dpi_aware             = true;
    int          default_window_width       = 1920;
    int          default_window_height      = 1080;
    bool         start_in_tray              = false;

    // Local automation endpoint (disabled by default)
    bool         automation_endpoint_enabled  = false;
    uint16_t     automation_endpoint_port     = 27315;
    int          automation_rate_limit_rpm    = 60;

    // Optional LAN sync (disabled by default — Schannel TLS + pinned cert required)
    bool         lan_sync_enabled           = false;
    std::string  lan_sync_peer_host;        // hostname or IP of the sync peer
    int          lan_sync_peer_port         = 27316;   // TLS port on the sync peer
    std::string  lan_sync_pinned_certs_path = "lan_sync_trusted_peers.json"; // thumbprint list

    // Retention
    int          retention_years_default    = 7;
    bool         retention_anonymize        = true;         // false = hard delete

    // Alert thresholds
    int          low_stock_days_threshold   = 7;
    int          expiration_alert_days      = 14;

    // Update trust
    std::string  trusted_publishers_path    = "trusted_publishers.json";

    // Storage paths (all relative to working directory)
    std::string  exports_dir               = "exports";
    std::string  update_metadata_dir       = "update";

    static AppConfig LoadOrDefault(const std::string& config_path);
    void             SaveToFile(const std::string& config_path) const;
};

} // namespace shelterops
