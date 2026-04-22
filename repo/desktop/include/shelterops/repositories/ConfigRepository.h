#pragma once
#include "shelterops/infrastructure/Database.h"
#include <string>
#include <optional>

namespace shelterops::repositories {

// Thin accessor over the `system_policies` key-value table.
class ConfigRepository {
public:
    explicit ConfigRepository(infrastructure::Database& db);

    // Returns the value for `key`, or std::nullopt if not found.
    std::optional<std::string> GetPolicy(const std::string& key) const;

    // Returns the boolean interpretation of a policy value.
    // "true", "1", "yes" (case-insensitive) → true; anything else → false.
    bool GetPolicyBool(const std::string& key, bool default_value = false) const;

    // Returns the integer interpretation of a policy value.
    int  GetPolicyInt(const std::string& key, int default_value = 0) const;

    // Upsert a policy value. Only Administrator-level callers should invoke this.
    void SetPolicy(const std::string& key, const std::string& value);

private:
    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
