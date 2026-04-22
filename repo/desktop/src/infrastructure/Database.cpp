#include "shelterops/infrastructure/Database.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cassert>
#include <condition_variable>

namespace shelterops::infrastructure {

// ---------------------------------------------------------------------------
// DbConnection
// ---------------------------------------------------------------------------

DbConnection::DbConnection(const std::string& db_path) {
    int rc = sqlite3_open_v2(
        db_path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "sqlite3_open_v2 failed";
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Database::open failed: " + msg);
    }
    ApplyPragmas();
}

DbConnection::~DbConnection() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

void DbConnection::ApplyPragmas() {
    const char* pragmas =
        "PRAGMA journal_mode = WAL;"
        "PRAGMA synchronous  = NORMAL;"
        "PRAGMA foreign_keys = ON;"
        "PRAGMA busy_timeout = 5000;";
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, pragmas, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "pragma error";
        sqlite3_free(errmsg);
        throw std::runtime_error("Database pragma setup failed: " + msg);
    }
}

void DbConnection::Exec(const std::string& sql,
                         const std::vector<std::string>& params) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(
            std::string("Exec prepare failed: ") + sqlite3_errmsg(db_) +
            " SQL: " + sql.substr(0, 120));
    }

    for (int i = 0; i < static_cast<int>(params.size()); ++i) {
        rc = sqlite3_bind_text(stmt, i + 1,
                               params[i].c_str(), -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Exec bind failed at param " +
                                     std::to_string(i));
        }
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(
            std::string("Exec step failed: ") + sqlite3_errmsg(db_));
    }
}

void DbConnection::Query(const std::string& sql,
                          const std::vector<std::string>& params,
                          RowCallback callback) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(
            std::string("Query prepare failed: ") + sqlite3_errmsg(db_) +
            " SQL: " + sql.substr(0, 120));
    }

    for (int i = 0; i < static_cast<int>(params.size()); ++i) {
        rc = sqlite3_bind_text(stmt, i + 1,
                               params[i].c_str(), -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Query bind failed at param " +
                                     std::to_string(i));
        }
    }

    int ncols = sqlite3_column_count(stmt);
    std::vector<std::string> col_names;
    col_names.reserve(static_cast<size_t>(ncols));
    for (int c = 0; c < ncols; ++c) {
        const char* n = sqlite3_column_name(stmt, c);
        col_names.emplace_back(n ? n : "");
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        std::vector<std::string> vals;
        vals.reserve(static_cast<size_t>(ncols));
        for (int c = 0; c < ncols; ++c) {
            const unsigned char* v = sqlite3_column_text(stmt, c);
            vals.emplace_back(v ? reinterpret_cast<const char*>(v) : "");
        }
        callback(col_names, vals);
    }

    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(
            std::string("Query step failed: ") + sqlite3_errmsg(db_));
    }
}

int64_t DbConnection::LastInsertRowId() const noexcept {
    return sqlite3_last_insert_rowid(db_);
}

int DbConnection::ChangeCount() const noexcept {
    return sqlite3_changes(db_);
}

// ---------------------------------------------------------------------------
// Database (connection pool)
// ---------------------------------------------------------------------------

Database::Database(const std::string& db_path, int max_connections)
    : db_path_(db_path), max_connections_(max_connections) {
    if (db_path_ == ":memory:") {
        // SQLite in-memory databases are connection-local. Using one pooled
        // connection keeps schema/data consistent for test fixtures.
        max_connections_ = 1;
    }
    // Pre-create one connection to validate the path at construction time.
    auto conn = std::make_shared<DbConnection>(db_path_);
    pool_.push_back(std::move(conn));
    spdlog::info("Database opened: {} (max_connections={})",
                 db_path_, max_connections_);
}

Database::~Database() {
    std::unique_lock<std::mutex> lock(mu_);
    pool_.clear();
}

Database::ConnectionGuard::ConnectionGuard(Database& owner,
                                            std::shared_ptr<DbConnection> conn)
    : owner_(owner), conn_(std::move(conn)) {}

Database::ConnectionGuard::~ConnectionGuard() {
    owner_.Release(std::move(conn_));
}

Database::ConnectionGuard Database::Acquire() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] {
        return !pool_.empty() || active_count_ < max_connections_;
    });

    std::shared_ptr<DbConnection> conn;
    if (!pool_.empty()) {
        conn = std::move(pool_.back());
        pool_.pop_back();
    } else {
        conn = std::make_shared<DbConnection>(db_path_);
    }
    ++active_count_;
    return ConnectionGuard(*this, std::move(conn));
}

void Database::Release(std::shared_ptr<DbConnection> conn) {
    std::unique_lock<std::mutex> lock(mu_);
    pool_.push_back(std::move(conn));
    --active_count_;
    lock.unlock();
    cv_.notify_one();
}

} // namespace shelterops::infrastructure
