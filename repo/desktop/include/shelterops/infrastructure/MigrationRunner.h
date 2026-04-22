#pragma once
#include "shelterops/infrastructure/Database.h"
#include <string>
#include <vector>

namespace shelterops::infrastructure {

struct MigrationResult {
    bool        success  = true;
    int         applied  = 0;   // number of scripts newly applied
    int         skipped  = 0;   // scripts already present in db_migrations
    std::string error_script;   // non-empty on failure
    std::string error_message;
};

// Applies all *.sql files in `migrations_dir` that are not yet recorded
// in the `db_migrations` table, in ascending filename order.
// Each script runs in its own transaction; failure rolls back the script
// and halts processing without affecting already-applied scripts.
class MigrationRunner {
public:
    explicit MigrationRunner(Database& db);

    // migrations_dir: path to the folder containing *.sql migration files.
    MigrationResult Run(const std::string& migrations_dir);

private:
    std::vector<std::string> CollectScripts(const std::string& dir) const;
    bool IsApplied(DbConnection& conn, const std::string& script_name) const;
    void ApplyScript(DbConnection& conn,
                     const std::string& script_name,
                     const std::string& sql);
    void RecordApplied(DbConnection& conn, const std::string& script_name);

    Database& db_;
};

} // namespace shelterops::infrastructure
