#pragma once
#include "shelterops/infrastructure/Database.h"
#include <string>
#include <optional>
#include <cstdint>
#include <vector>

namespace shelterops::repositories {

struct UserRecord {
    int64_t     user_id                = 0;
    std::string username;
    std::string display_name;
    std::string password_hash;
    std::string role;              // "administrator" | "operations_manager" | "inventory_clerk" | "auditor"
    bool        is_active           = true;
    int64_t     created_at          = 0;
    int64_t     last_login_at       = 0;
    bool        consent_given       = false;
    int64_t     anonymized_at       = 0;   // 0 = not anonymized
    int         failed_login_attempts = 0;
    int64_t     locked_until        = 0;   // 0 = not locked
};

struct NewUserParams {
    std::string username;
    std::string display_name;
    std::string password_hash;  // pre-hashed by CryptoHelper
    std::string role;
};

// All queries use parameterized binding; no raw string concatenation.
// The Anonymize method NULLs PII fields (display_name, consent) and sets
// anonymized_at. The password_hash is also cleared to prevent login.
class UserRepository {
public:
    explicit UserRepository(infrastructure::Database& db);

    std::optional<UserRecord> FindByUsername(const std::string& username) const;
    std::optional<UserRecord> FindById(int64_t user_id) const;

    int64_t Insert(const NewUserParams& params, int64_t now_unix);

    void UpdateLastLogin(int64_t user_id, int64_t now_unix);
    void UpdateIsActive(int64_t user_id, bool active);
    void UpdatePasswordHash(int64_t user_id, const std::string& new_hash);

    void RecordFailedLogin(int64_t user_id, int64_t locked_until);
    void ResetFailedLoginCount(int64_t user_id);

    // Retention: replaces PII fields with placeholders, sets anonymized_at.
    void Anonymize(int64_t user_id, int64_t now_unix);

    struct RetentionCandidate {
        int64_t user_id;
        int64_t created_at;
        bool    already_anonymized;
    };

    // Returns users whose created_at is older than cutoff_unix.
    std::vector<RetentionCandidate> ListRetentionCandidates(int64_t cutoff_unix) const;

    // Hard delete user and related mutable records. Returns false when deletion
    // cannot be completed due to constraints.
    bool DeleteForRetention(int64_t user_id);

    bool Exists(int64_t user_id) const;
    bool IsEmpty() const;  // true when users table has no rows

private:
    static UserRecord RowToRecord(const std::vector<std::string>& vals);
    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
