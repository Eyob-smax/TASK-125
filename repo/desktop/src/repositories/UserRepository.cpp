#include "shelterops/repositories/UserRepository.h"
#include <stdexcept>

namespace shelterops::repositories {

UserRepository::UserRepository(infrastructure::Database& db) : db_(db) {
    auto guard = db_.Acquire();
    auto ensure_column = [&](const char* sql) {
        try {
            guard->Exec(sql, {});
        } catch (...) {
            // Legacy test schemas may already contain these columns.
        }
    };
    ensure_column("ALTER TABLE users ADD COLUMN last_login_at INTEGER");
    ensure_column("ALTER TABLE users ADD COLUMN consent_given INTEGER NOT NULL DEFAULT 0");
    ensure_column("ALTER TABLE users ADD COLUMN anonymized_at INTEGER");
    ensure_column("ALTER TABLE users ADD COLUMN failed_login_attempts INTEGER NOT NULL DEFAULT 0");
    ensure_column("ALTER TABLE users ADD COLUMN locked_until INTEGER");
}

UserRecord UserRepository::RowToRecord(const std::vector<std::string>& vals) {
    // Column order mirrors the SELECT queries below:
    // 0:user_id 1:username 2:display_name 3:password_hash 4:role 5:is_active
    // 6:created_at 7:last_login_at 8:consent_given 9:anonymized_at
    // 10:failed_login_attempts 11:locked_until
    UserRecord r;
    auto safeInt = [&](size_t i, int64_t def = 0) -> int64_t {
        return (i < vals.size() && !vals[i].empty()) ? std::stoll(vals[i]) : def;
    };
    r.user_id               = safeInt(0);
    r.username              = vals.size() > 1 ? vals[1] : "";
    r.display_name          = vals.size() > 2 ? vals[2] : "";
    r.password_hash         = vals.size() > 3 ? vals[3] : "";
    r.role                  = vals.size() > 4 ? vals[4] : "";
    r.is_active             = safeInt(5, 1) != 0;
    r.created_at            = safeInt(6);
    r.last_login_at         = safeInt(7);
    r.consent_given         = safeInt(8) != 0;
    r.anonymized_at         = safeInt(9);
    r.failed_login_attempts = static_cast<int>(safeInt(10));
    r.locked_until          = safeInt(11);
    return r;
}

static const char* kSelectCols =
    "SELECT user_id, username, display_name, password_hash, role, is_active,"
    "       created_at, COALESCE(last_login_at,0),"
    "       consent_given, COALESCE(anonymized_at,0),"
    "       failed_login_attempts, COALESCE(locked_until,0)"
    " FROM users";

std::optional<UserRecord> UserRepository::FindByUsername(
        const std::string& username) const {
    std::optional<UserRecord> result;
    auto guard = db_.Acquire();
    guard->Query(
        std::string(kSelectCols) + " WHERE username = ? COLLATE NOCASE LIMIT 1",
        {username},
        [&result](const auto&, const auto& vals) {
            result = RowToRecord(vals);
        });
    return result;
}

std::optional<UserRecord> UserRepository::FindById(int64_t user_id) const {
    std::optional<UserRecord> result;
    auto guard = db_.Acquire();
    guard->Query(
        std::string(kSelectCols) + " WHERE user_id = ? LIMIT 1",
        {std::to_string(user_id)},
        [&result](const auto&, const auto& vals) {
            result = RowToRecord(vals);
        });
    return result;
}

int64_t UserRepository::Insert(const NewUserParams& params, int64_t now_unix) {
    auto guard = db_.Acquire();
    guard->Exec(
        "INSERT INTO users(username, display_name, password_hash, role,"
        "                  is_active, created_at, consent_given,"
        "                  failed_login_attempts)"
        " VALUES (?, ?, ?, ?, 1, ?, 0, 0)",
        {params.username, params.display_name, params.password_hash,
         params.role, std::to_string(now_unix)});
    return guard->LastInsertRowId();
}

void UserRepository::UpdateLastLogin(int64_t user_id, int64_t now_unix) {
    auto guard = db_.Acquire();
    guard->Exec("UPDATE users SET last_login_at = ? WHERE user_id = ?",
                {std::to_string(now_unix), std::to_string(user_id)});
}

void UserRepository::UpdateIsActive(int64_t user_id, bool active) {
    auto guard = db_.Acquire();
    guard->Exec("UPDATE users SET is_active = ? WHERE user_id = ?",
                {active ? "1" : "0", std::to_string(user_id)});
}

void UserRepository::RecordFailedLogin(int64_t user_id, int64_t locked_until) {
    auto guard = db_.Acquire();
    if (locked_until > 0) {
        guard->Exec(
            "UPDATE users SET"
            "  failed_login_attempts = failed_login_attempts + 1,"
            "  locked_until = ?"
            " WHERE user_id = ?",
            {std::to_string(locked_until), std::to_string(user_id)});
    } else {
        guard->Exec(
            "UPDATE users SET failed_login_attempts = failed_login_attempts + 1"
            " WHERE user_id = ?",
            {std::to_string(user_id)});
    }
}

void UserRepository::ResetFailedLoginCount(int64_t user_id) {
    auto guard = db_.Acquire();
    guard->Exec(
        "UPDATE users SET failed_login_attempts = 0, locked_until = NULL"
        " WHERE user_id = ?",
        {std::to_string(user_id)});
}

void UserRepository::UpdatePasswordHash(int64_t user_id,
                                          const std::string& new_hash) {
    auto guard = db_.Acquire();
    guard->Exec("UPDATE users SET password_hash = ? WHERE user_id = ?",
                {new_hash, std::to_string(user_id)});
}

void UserRepository::Anonymize(int64_t user_id, int64_t now_unix) {
    auto guard = db_.Acquire();
    guard->Exec(
        "UPDATE users SET"
        "  display_name   = '[anonymized]',"
        "  password_hash  = '',"
        "  consent_given  = 0,"
        "  anonymized_at  = ?,"
        "  is_active      = 0"
        " WHERE user_id = ?",
        {std::to_string(now_unix), std::to_string(user_id)});
}

std::vector<UserRepository::RetentionCandidate>
UserRepository::ListRetentionCandidates(int64_t cutoff_unix) const {
    static const std::string sql =
        "SELECT user_id, created_at, CASE WHEN anonymized_at IS NOT NULL THEN 1 ELSE 0 END "
        "FROM users WHERE created_at < ?";

    std::vector<RetentionCandidate> result;
    auto guard = db_.Acquire();
    guard->Query(sql, {std::to_string(cutoff_unix)},
        [&](const auto&, const auto& vals) {
            RetentionCandidate c;
            c.user_id            = vals[0].empty() ? 0 : std::stoll(vals[0]);
            c.created_at         = vals[1].empty() ? 0 : std::stoll(vals[1]);
            c.already_anonymized = vals[2] == "1";
            result.push_back(c);
        });
    return result;
}

bool UserRepository::DeleteForRetention(int64_t user_id) {
    auto guard = db_.Acquire();
    try {
        // Clear known foreign-key references first.
        guard->Exec("UPDATE bookings SET created_by = NULL WHERE created_by = ?",
                    {std::to_string(user_id)});
        guard->Exec("UPDATE bookings SET approved_by = NULL WHERE approved_by = ?",
                    {std::to_string(user_id)});
        guard->Exec("UPDATE maintenance_tickets SET created_by = NULL WHERE created_by = ?",
                    {std::to_string(user_id)});
        guard->Exec("UPDATE maintenance_tickets SET assigned_to = NULL WHERE assigned_to = ?",
                    {std::to_string(user_id)});

        // Remove session/consent rows owned by the user.
        guard->Exec("DELETE FROM user_sessions WHERE user_id = ?",
                    {std::to_string(user_id)});
        guard->Exec("DELETE FROM consent_records WHERE entity_type = 'users' AND entity_id = ?",
                    {std::to_string(user_id)});

        // Finally delete user row.
        guard->Exec("DELETE FROM users WHERE user_id = ?", {std::to_string(user_id)});
        return true;
    } catch (...) {
        return false;
    }
}

bool UserRepository::Exists(int64_t user_id) const {
    bool found = false;
    auto guard = db_.Acquire();
    guard->Query("SELECT 1 FROM users WHERE user_id = ? LIMIT 1",
                 {std::to_string(user_id)},
                 [&found](const auto&, const auto&) { found = true; });
    return found;
}

bool UserRepository::IsEmpty() const {
    bool has_row = false;
    auto guard = db_.Acquire();
    guard->Query("SELECT 1 FROM users LIMIT 1", {},
                 [&has_row](const auto&, const auto&) { has_row = true; });
    return !has_row;
}

} // namespace shelterops::repositories
