#include "shelterops/repositories/ConfigRepository.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace shelterops::repositories {

ConfigRepository::ConfigRepository(infrastructure::Database& db) : db_(db) {}

std::optional<std::string> ConfigRepository::GetPolicy(
        const std::string& key) const {
    std::optional<std::string> result;
    auto guard = db_.Acquire();
    guard->Query(
        "SELECT value FROM system_policies WHERE key = ? LIMIT 1",
        {key},
        [&result](const auto&, const auto& vals) {
            if (!vals.empty()) result = vals[0];
        });
    return result;
}

bool ConfigRepository::GetPolicyBool(const std::string& key,
                                      bool default_value) const {
    auto val = GetPolicy(key);
    if (!val.has_value()) return default_value;
    std::string lower = *val;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return lower == "true" || lower == "1" || lower == "yes";
}

int ConfigRepository::GetPolicyInt(const std::string& key,
                                    int default_value) const {
    auto val = GetPolicy(key);
    if (!val.has_value()) return default_value;
    try { return std::stoi(*val); }
    catch (...) { return default_value; }
}

void ConfigRepository::SetPolicy(const std::string& key,
                                   const std::string& value) {
    auto guard = db_.Acquire();
    guard->Exec(
        "INSERT INTO system_policies(key, value) VALUES(?,?)"
        " ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        {key, value});
}

} // namespace shelterops::repositories
