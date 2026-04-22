#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/MigrationRunner.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace shelterops::infrastructure;

class MigrationRunnerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mig_dir_ = fs::temp_directory_path() / "shelterops_mig_test";
        fs::create_directories(mig_dir_);
        db_ = std::make_unique<Database>(":memory:");
        // The bootstrap migration must create db_migrations itself.
        db_->Acquire()->Exec(
            "CREATE TABLE IF NOT EXISTS db_migrations("
            "  migration_id INTEGER PRIMARY KEY,"
            "  script_name TEXT NOT NULL UNIQUE,"
            "  applied_at INTEGER NOT NULL)");
    }
    void TearDown() override {
        fs::remove_all(mig_dir_);
    }
    void WriteScript(const std::string& name, const std::string& sql) {
        std::ofstream f((mig_dir_ / name).string());
        f << sql;
    }
    fs::path mig_dir_;
    std::unique_ptr<Database> db_;
};

TEST_F(MigrationRunnerTest, AppliesScriptsInOrder) {
    WriteScript("001_a.sql", "CREATE TABLE a(x INTEGER);");
    WriteScript("002_b.sql", "CREATE TABLE b(y TEXT);");
    MigrationRunner runner(*db_);
    auto result = runner.Run(mig_dir_.string());
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.applied, 2);
    EXPECT_EQ(result.skipped, 0);
}

TEST_F(MigrationRunnerTest, IdempotentOnSecondRun) {
    WriteScript("001_x.sql", "CREATE TABLE x(id INTEGER);");
    MigrationRunner runner(*db_);
    runner.Run(mig_dir_.string());
    auto result = runner.Run(mig_dir_.string());
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.applied, 0);
    EXPECT_EQ(result.skipped, 1);
}

TEST_F(MigrationRunnerTest, RecordsInDbMigrations) {
    WriteScript("001_init.sql", "CREATE TABLE init_table(id INTEGER);");
    MigrationRunner runner(*db_);
    runner.Run(mig_dir_.string());
    bool found = false;
    db_->Acquire()->Query(
        "SELECT 1 FROM db_migrations WHERE script_name='001_init.sql'",
        {},
        [&found](const auto&, const auto&) { found = true; });
    EXPECT_TRUE(found);
}

TEST_F(MigrationRunnerTest, HaltsOnFailingScript) {
    WriteScript("001_ok.sql",  "CREATE TABLE ok_table(id INTEGER);");
    WriteScript("002_bad.sql", "THIS IS NOT VALID SQL;");
    MigrationRunner runner(*db_);
    auto result = runner.Run(mig_dir_.string());
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_script, "002_bad.sql");
    // First script should still be applied.
    EXPECT_EQ(result.applied, 1);
}

TEST_F(MigrationRunnerTest, MissingDirFails) {
    MigrationRunner runner(*db_);
    auto result = runner.Run("/nonexistent/path/that/does/not/exist");
    EXPECT_FALSE(result.success);
}
