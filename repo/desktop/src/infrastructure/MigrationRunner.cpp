#include "shelterops/infrastructure/MigrationRunner.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

namespace shelterops::infrastructure {

MigrationRunner::MigrationRunner(Database& db) : db_(db) {}

MigrationResult MigrationRunner::Run(const std::string& migrations_dir) {
    MigrationResult result;

    std::vector<std::string> scripts;
    try {
        scripts = CollectScripts(migrations_dir);
    } catch (const std::exception& ex) {
        result.success       = false;
        result.error_message = ex.what();
        return result;
    }

    auto guard = db_.Acquire();

    guard->Exec(
        "CREATE TABLE IF NOT EXISTS db_migrations("
        "id INTEGER PRIMARY KEY, "
        "script_name TEXT NOT NULL UNIQUE, "
        "applied_at INTEGER NOT NULL)",
        {});

    for (const auto& path : scripts) {
        std::string script_name = fs::path(path).filename().string();

        if (IsApplied(*guard, script_name)) {
            ++result.skipped;
            continue;
        }

        std::ifstream f(path);
        if (!f.is_open()) {
            result.success       = false;
            result.error_script  = script_name;
            result.error_message = "Cannot open " + path;
            spdlog::error("MigrationRunner: {}", result.error_message);
            return result;
        }
        std::stringstream buf;
        buf << f.rdbuf();
        std::string sql = buf.str();

        try {
            ApplyScript(*guard, script_name, sql);
            RecordApplied(*guard, script_name);
            ++result.applied;
            spdlog::info("Migration applied: {}", script_name);
        } catch (const std::exception& ex) {
            result.success       = false;
            result.error_script  = script_name;
            result.error_message = ex.what();
            spdlog::error("Migration failed [{}]: {}", script_name, ex.what());
            return result;
        }
    }

    spdlog::info("MigrationRunner complete: applied={} skipped={}",
                 result.applied, result.skipped);
    return result;
}

std::vector<std::string> MigrationRunner::CollectScripts(
        const std::string& dir) const {
    if (!fs::exists(dir)) {
        throw std::runtime_error("Migrations directory not found: " + dir);
    }

    std::vector<std::string> paths;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".sql") {
            paths.push_back(entry.path().string());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

bool MigrationRunner::IsApplied(DbConnection& conn,
                                  const std::string& script_name) const {
    bool found = false;
    conn.Query(
        "SELECT 1 FROM db_migrations WHERE script_name = ? LIMIT 1",
        {script_name},
        [&found](const auto&, const auto&) { found = true; });
    return found;
}

void MigrationRunner::ApplyScript(DbConnection& conn,
                                   const std::string& /*script_name*/,
                                   const std::string& sql) {
    // Run the entire script as a single exec (supports multi-statement SQL).
    char* errmsg = nullptr;
    int rc = sqlite3_exec(conn.Handle(), sql.c_str(),
                          nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "sqlite3_exec error";
        sqlite3_free(errmsg);
        throw std::runtime_error("SQL execution error: " + msg);
    }
}

void MigrationRunner::RecordApplied(DbConnection& conn,
                                     const std::string& script_name) {
    int64_t now = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    conn.Exec(
        "INSERT INTO db_migrations(script_name, applied_at) VALUES (?, ?)",
        {script_name, std::to_string(now)});
}

} // namespace shelterops::infrastructure
