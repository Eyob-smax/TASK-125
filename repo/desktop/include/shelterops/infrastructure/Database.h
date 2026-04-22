#pragma once
#include <sqlite3.h>
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

namespace shelterops::infrastructure {

// Row callback: receives column names and values for a single result row.
using RowCallback = std::function<void(
    const std::vector<std::string>& cols,
    const std::vector<std::string>& vals)>;

// RAII wrapper for a single SQLite connection with WAL mode and standard
// pragmas configured at open time. All statement execution goes through
// Exec or Query to enforce parameterized binding and prevent SQL injection.
class DbConnection {
public:
    explicit DbConnection(const std::string& db_path);
    ~DbConnection();

    DbConnection(const DbConnection&)            = delete;
    DbConnection& operator=(const DbConnection&) = delete;
    DbConnection(DbConnection&&)                 = delete;
    DbConnection& operator=(DbConnection&&)      = delete;

    // Execute a write statement with positional ? parameters.
    // Throws std::runtime_error on failure.
    void Exec(const std::string& sql,
              const std::vector<std::string>& params = {});

    // Execute a SELECT with positional ? parameters; invokes callback per row.
    void Query(const std::string& sql,
               const std::vector<std::string>& params,
               RowCallback callback);

    // Returns the rowid of the last successful INSERT.
    int64_t LastInsertRowId() const noexcept;

    // Returns the number of rows affected by the last DML statement.
    int ChangeCount() const noexcept;

    sqlite3* Handle() noexcept { return db_; }

private:
    void ApplyPragmas();
    sqlite3* db_ = nullptr;
};

// Bounded connection pool for background workers.
// Max 4 concurrent connections; callers block if all are in use.
class Database {
public:
    explicit Database(const std::string& db_path, int max_connections = 4);
    ~Database();

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    // Acquire a connection from the pool (blocks if all are in use).
    // Returns a RAII guard that releases the connection on destruction.
    struct ConnectionGuard {
        ConnectionGuard(Database& owner, std::shared_ptr<DbConnection> conn);
        ~ConnectionGuard();
        DbConnection* operator->() { return conn_.get(); }
        DbConnection& operator*()  { return *conn_; }
    private:
        Database& owner_;
        std::shared_ptr<DbConnection> conn_;
    };

    ConnectionGuard Acquire();

    const std::string& Path() const noexcept { return db_path_; }

private:
    void Release(std::shared_ptr<DbConnection> conn);

    std::string db_path_;
    int         max_connections_;
    std::mutex  mu_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<DbConnection>> pool_;
    int active_count_ = 0;
};

} // namespace shelterops::infrastructure
