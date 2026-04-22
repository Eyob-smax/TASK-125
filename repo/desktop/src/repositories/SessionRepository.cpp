#include "shelterops/repositories/SessionRepository.h"

namespace shelterops::repositories {

SessionRepository::SessionRepository(infrastructure::Database& db) : db_(db) {}

SessionRecord SessionRepository::RowToRecord(
        const std::vector<std::string>& vals) {
    // 0:session_id 1:user_id 2:created_at 3:expires_at
    // 4:device_fingerprint 5:is_active 6:absolute_expires_at
    auto safeInt = [&](size_t i, int64_t def = 0) -> int64_t {
        return (i < vals.size() && !vals[i].empty()) ? std::stoll(vals[i]) : def;
    };
    SessionRecord r;
    r.session_id             = vals.size() > 0 ? vals[0] : "";
    r.user_id                = safeInt(1);
    r.created_at             = safeInt(2);
    r.expires_at             = safeInt(3);
    r.device_fingerprint     = vals.size() > 4 ? vals[4] : "";
    r.is_active              = safeInt(5, 1) != 0;
    r.absolute_expires_at    = safeInt(6);
    return r;
}

void SessionRepository::Insert(const SessionRecord& rec) {
    auto guard = db_.Acquire();
    guard->Exec(
        "INSERT INTO user_sessions"
        "(session_id, user_id, created_at, expires_at, device_fingerprint, is_active, absolute_expires_at)"
        " VALUES (?, ?, ?, ?, ?, 1, ?)",
        {rec.session_id, std::to_string(rec.user_id),
         std::to_string(rec.created_at), std::to_string(rec.expires_at),
         rec.device_fingerprint, std::to_string(rec.absolute_expires_at)});
}

std::optional<SessionRecord> SessionRepository::FindById(
        const std::string& session_id) const {
    std::optional<SessionRecord> result;
    auto guard = db_.Acquire();
    guard->Query(
        "SELECT session_id, user_id, created_at, expires_at,"
        "       COALESCE(device_fingerprint,''), is_active,"
        "       COALESCE(absolute_expires_at,0)"
        " FROM user_sessions WHERE session_id = ? LIMIT 1",
        {session_id},
        [&result](const auto&, const auto& vals) {
            result = RowToRecord(vals);
        });
    return result;
}

void SessionRepository::MarkInactive(const std::string& session_id) {
    auto guard = db_.Acquire();
    guard->Exec("UPDATE user_sessions SET is_active = 0 WHERE session_id = ?",
                {session_id});
}

void SessionRepository::ExpireAllForUser(int64_t user_id) {
    auto guard = db_.Acquire();
    guard->Exec("UPDATE user_sessions SET is_active = 0 WHERE user_id = ?",
                {std::to_string(user_id)});
}

void SessionRepository::PurgeExpired(int64_t now_unix) {
    auto guard = db_.Acquire();
    guard->Exec("DELETE FROM user_sessions WHERE expires_at < ?",
                {std::to_string(now_unix)});
}

void SessionRepository::UpdateActivity(const std::string& session_id,
                                        int64_t new_expires_at) {
    auto guard = db_.Acquire();
    guard->Exec("UPDATE user_sessions SET expires_at = ? WHERE session_id = ? AND is_active = 1",
                {std::to_string(new_expires_at), session_id});
}

} // namespace shelterops::repositories
