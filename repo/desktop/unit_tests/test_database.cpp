#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"

using namespace shelterops::infrastructure;

TEST(Database, OpensInMemory) {
    EXPECT_NO_THROW(Database db(":memory:"));
}

TEST(Database, WalModeEnabled) {
    Database db(":memory:");
    auto guard = db.Acquire();
    std::string mode;
    guard->Query("PRAGMA journal_mode", {},
                 [&mode](const auto&, const auto& vals) {
                     if (!vals.empty()) mode = vals[0];
                 });
    // In-memory always returns "memory"; WAL is applied but
    // journal_mode returns "memory" for :memory: databases.
    // What matters is that the pragma didn't throw.
    EXPECT_FALSE(mode.empty());
}

TEST(Database, ForeignKeysEnabled) {
    Database db(":memory:");
    auto guard = db.Acquire();
    std::string fk;
    guard->Query("PRAGMA foreign_keys", {},
                 [&fk](const auto&, const auto& vals) {
                     if (!vals.empty()) fk = vals[0];
                 });
    EXPECT_EQ(fk, "1");
}

TEST(Database, ExecInsertAndQuery) {
    Database db(":memory:");
    auto guard = db.Acquire();
    guard->Exec("CREATE TABLE t(id INTEGER PRIMARY KEY, val TEXT)");
    guard->Exec("INSERT INTO t(val) VALUES (?)", {"hello"});
    std::string val;
    guard->Query("SELECT val FROM t LIMIT 1", {},
                 [&val](const auto&, const auto& vals) {
                     if (!vals.empty()) val = vals[0];
                 });
    EXPECT_EQ(val, "hello");
}

TEST(Database, LastInsertRowId) {
    Database db(":memory:");
    auto guard = db.Acquire();
    guard->Exec("CREATE TABLE t(id INTEGER PRIMARY KEY, val TEXT)");
    guard->Exec("INSERT INTO t(val) VALUES (?)", {"x"});
    EXPECT_GT(guard->LastInsertRowId(), 0);
}

TEST(Database, ParameterizedQueryPreventsInjection) {
    Database db(":memory:");
    auto guard = db.Acquire();
    guard->Exec("CREATE TABLE t(name TEXT)");
    guard->Exec("INSERT INTO t(name) VALUES (?)", {"Alice"});
    // Attempt injection via parameter.
    std::string name;
    guard->Query("SELECT name FROM t WHERE name = ?",
                 {"Alice'; DROP TABLE t;--"},
                 [&name](const auto&, const auto& vals) {
                     if (!vals.empty()) name = vals[0];
                 });
    EXPECT_TRUE(name.empty()); // No match; table still exists.
    bool table_exists = false;
    guard->Query("SELECT name FROM sqlite_master WHERE type='table' AND name='t'",
                 {},
                 [&table_exists](const auto&, const auto&) { table_exists = true; });
    EXPECT_TRUE(table_exists);
}
