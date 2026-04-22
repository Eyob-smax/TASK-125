#include "shelterops/AppConfig.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>

namespace shelterops {

AppConfig AppConfig::LoadOrDefault(const std::string& config_path) {
    AppConfig cfg;
    std::ifstream f(config_path);
    if (!f.is_open()) {
        spdlog::info("Config '{}' not found; using built-in defaults", config_path);
        return cfg;
    }
    try {
        auto j = nlohmann::json::parse(f);
        auto get = [&](const char* key, auto& field) {
            if (j.contains(key)) field = j[key].get<std::remove_reference_t<decltype(field)>>();
        };
        get("db_path",                     cfg.db_path);
        get("log_dir",                     cfg.log_dir);
        get("log_level",                   cfg.log_level);
        get("high_dpi_aware",              cfg.high_dpi_aware);
        get("default_window_width",        cfg.default_window_width);
        get("default_window_height",       cfg.default_window_height);
        get("start_in_tray",               cfg.start_in_tray);
        get("automation_endpoint_enabled", cfg.automation_endpoint_enabled);
        get("automation_endpoint_port",    cfg.automation_endpoint_port);
        get("automation_rate_limit_rpm",   cfg.automation_rate_limit_rpm);
        get("lan_sync_enabled",            cfg.lan_sync_enabled);
        get("lan_sync_peer_host",          cfg.lan_sync_peer_host);
        get("lan_sync_peer_port",          cfg.lan_sync_peer_port);
        get("lan_sync_pinned_certs_path",  cfg.lan_sync_pinned_certs_path);
        get("retention_years_default",     cfg.retention_years_default);
        get("retention_anonymize",         cfg.retention_anonymize);
        get("low_stock_days_threshold",    cfg.low_stock_days_threshold);
        get("expiration_alert_days",       cfg.expiration_alert_days);
        get("trusted_publishers_path",     cfg.trusted_publishers_path);
        get("exports_dir",                 cfg.exports_dir);
        get("update_metadata_dir",         cfg.update_metadata_dir);
    } catch (const nlohmann::json::exception& e) {
        spdlog::warn("Config parse error in '{}': {}. Falling back to defaults.", config_path, e.what());
    }
    return cfg;
}

void AppConfig::SaveToFile(const std::string& config_path) const {
    nlohmann::json j;
    j["db_path"]                     = db_path;
    j["log_dir"]                     = log_dir;
    j["log_level"]                   = log_level;
    j["high_dpi_aware"]              = high_dpi_aware;
    j["default_window_width"]        = default_window_width;
    j["default_window_height"]       = default_window_height;
    j["start_in_tray"]               = start_in_tray;
    j["automation_endpoint_enabled"] = automation_endpoint_enabled;
    j["automation_endpoint_port"]    = automation_endpoint_port;
    j["automation_rate_limit_rpm"]   = automation_rate_limit_rpm;
    j["lan_sync_enabled"]            = lan_sync_enabled;
    j["lan_sync_peer_host"]          = lan_sync_peer_host;
    j["lan_sync_peer_port"]          = lan_sync_peer_port;
    j["lan_sync_pinned_certs_path"]  = lan_sync_pinned_certs_path;
    j["retention_years_default"]     = retention_years_default;
    j["retention_anonymize"]         = retention_anonymize;
    j["low_stock_days_threshold"]    = low_stock_days_threshold;
    j["expiration_alert_days"]       = expiration_alert_days;
    j["trusted_publishers_path"]     = trusted_publishers_path;
    j["exports_dir"]                 = exports_dir;
    j["update_metadata_dir"]         = update_metadata_dir;
    std::ofstream f(config_path);
    if (f.is_open()) {
        f << j.dump(4);
    } else {
        spdlog::error("Cannot write config to '{}'", config_path);
    }
}

} // namespace shelterops
