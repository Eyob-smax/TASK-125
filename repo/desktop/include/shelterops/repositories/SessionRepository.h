#pragma once
#include "shelterops/infrastructure/Database.h"
#include <string>
#include <optional>
#include <cstdint>

namespace shelterops::repositories {

struct SessionRecord {
    std::string session_id;
    int64_t     user_id                = 0;
    int64_t     created_at             = 0;
    int64_t     expires_at             = 0;        // sliding 1-hour inactivity window
    std::string device_fingerprint;
    bool        is_active              = true;
    int64_t     absolute_expires_at    = 0;        // hard 12-hour cap from login time
};

class SessionRepository {
public:
    explicit SessionRepository(infrastructure::Database& db);

    void Insert(const SessionRecord& rec);

    std::optional<SessionRecord> FindById(const std::string& session_id) const;

    // Mark a single session as inactive (logout).
    void MarkInactive(const std::string& session_id);

    // Invalidate all active sessions for a user (password change, admin lock).
    void ExpireAllForUser(int64_t user_id);

    // Remove all sessions whose expires_at < now_unix.
    void PurgeExpired(int64_t now_unix);

    // Refresh the expiry timestamp of an active session (last-activity update).
    void UpdateActivity(const std::string& session_id, int64_t new_expires_at);

private:
    static SessionRecord RowToRecord(const std::vector<std::string>& vals);
    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
