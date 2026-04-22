#include "shelterops/infrastructure/CrashCheckpoint.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <regex>
#include <nlohmann/json.hpp>

namespace shelterops::infrastructure {

CrashCheckpoint::CrashCheckpoint(Database& db) : db_(db) {}

bool CrashCheckpoint::ContainsPiiMarker(const std::string& json) {
    // Matches JSON keys containing PII field names.
    static const std::regex kPiiKeyPattern(
        R"("[^"]*(?:password|token|api_key|email|phone)[^"]*"\s*:)",
        std::regex::icase);
    if (std::regex_search(json, kPiiKeyPattern)) return true;

    // Matches "password=..." or "password:..." patterns in values.
    static const std::regex kPiiValuePattern(
        R"(password\s*[=:]\s*\S)",
        std::regex::icase);
    if (std::regex_search(json, kPiiValuePattern)) return true;

    // Matches email addresses (user@domain.tld) in values.
    static const std::regex kEmailPattern(
        R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})",
        std::regex::optimize);
    if (std::regex_search(json, kEmailPattern)) return true;

    return false;
}

bool CrashCheckpoint::SaveCheckpoint(const std::string& window_state_json,
                                      const std::string& sanitized_form_state_json) {
    if (ContainsPiiMarker(window_state_json) ||
        ContainsPiiMarker(sanitized_form_state_json)) {
        spdlog::error("CrashCheckpoint::SaveCheckpoint rejected: PII marker detected in payload");
        return false;
    }

    // Prefer saved_at embedded in form_state JSON; fall back to system clock.
    int64_t now = 0;
    try {
        auto fj = nlohmann::json::parse(sanitized_form_state_json);
        if (fj.contains("saved_at"))
            now = fj["saved_at"].get<int64_t>();
    } catch (...) {}
    if (now == 0) {
        now = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    auto guard = db_.Acquire();
    guard->Exec(
        "INSERT INTO crash_checkpoints(saved_at, window_state, form_state)"
        " VALUES (?, ?, ?)",
        {std::to_string(now), window_state_json, sanitized_form_state_json});
    return true;
}

std::optional<CheckpointData> CrashCheckpoint::LoadLatest() {
    std::optional<CheckpointData> result;
    auto guard = db_.Acquire();
    guard->Query(
        "SELECT saved_at, window_state, form_state"
        " FROM crash_checkpoints"
        " ORDER BY saved_at DESC LIMIT 1",
        {},
        [&result](const auto& /*cols*/, const auto& vals) {
            CheckpointData d;
            d.saved_at    = vals.size() > 0 ? std::stoll(vals[0]) : 0;
            d.window_state = vals.size() > 1 ? vals[1] : "";
            d.form_state   = vals.size() > 2 ? vals[2] : "";
            result = d;
        });
    return result;
}

void CrashCheckpoint::Trim(int keep_n) {
    if (keep_n < 1) keep_n = 1;
    auto guard = db_.Acquire();
    guard->Exec(
        "DELETE FROM crash_checkpoints"
        " WHERE checkpoint_id NOT IN ("
        "   SELECT checkpoint_id FROM crash_checkpoints"
        "   ORDER BY saved_at DESC LIMIT ?"
        ")",
        {std::to_string(keep_n)});
}

} // namespace shelterops::infrastructure
