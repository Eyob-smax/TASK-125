#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/infrastructure/CryptoHelper.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;

static void CreateUsersTable(Database& db) {
    auto g = db.Acquire();
    g->Exec(
        "CREATE TABLE IF NOT EXISTS users("
        "  user_id INTEGER PRIMARY KEY,"
        "  username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "  display_name TEXT NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  role TEXT NOT NULL,"
        "  is_active INTEGER NOT NULL DEFAULT 1,"
        "  created_at INTEGER NOT NULL,"
        "  last_login_at INTEGER,"
        "  consent_given INTEGER NOT NULL DEFAULT 0,"
        "  anonymized_at INTEGER,"
        "  failed_login_attempts INTEGER NOT NULL DEFAULT 0,"
        "  locked_until INTEGER)");
}

class UserRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        CryptoHelper::Init();
        db_ = std::make_unique<Database>(":memory:");
        CreateUsersTable(*db_);
        repo_ = std::make_unique<UserRepository>(*db_);
    }
    std::unique_ptr<Database>        db_;
    std::unique_ptr<UserRepository>  repo_;
};

TEST_F(UserRepoTest, InsertAndFind) {
    NewUserParams p{"admin1", "Alice Admin", "hash123", "administrator"};
    int64_t id = repo_->Insert(p, 1000);
    EXPECT_GT(id, 0);
    auto r = repo_->FindByUsername("admin1");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->username, "admin1");
    EXPECT_EQ(r->role, "administrator");
}

TEST_F(UserRepoTest, UsernameIsCaseInsensitive) {
    repo_->Insert({"bob", "Bob", "h", "auditor"}, 1000);
    auto r = repo_->FindByUsername("BOB");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->username, "bob");
}

TEST_F(UserRepoTest, FindByIdRoundTrip) {
    int64_t id = repo_->Insert({"carol", "Carol", "h", "inventory_clerk"}, 1000);
    auto r = repo_->FindById(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->user_id, id);
}

TEST_F(UserRepoTest, MissingUsernameReturnsNullopt) {
    EXPECT_FALSE(repo_->FindByUsername("nobody").has_value());
}

TEST_F(UserRepoTest, FailedLoginCounterIncrements) {
    int64_t id = repo_->Insert({"dave", "Dave", "h", "auditor"}, 1000);
    repo_->RecordFailedLogin(id, 0);
    repo_->RecordFailedLogin(id, 0);
    auto r = repo_->FindById(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->failed_login_attempts, 2);
}

TEST_F(UserRepoTest, ResetClearsCounter) {
    int64_t id = repo_->Insert({"eve", "Eve", "h", "auditor"}, 1000);
    repo_->RecordFailedLogin(id, 0);
    repo_->ResetFailedLoginCount(id);
    auto r = repo_->FindById(id);
    EXPECT_EQ(r->failed_login_attempts, 0);
    EXPECT_EQ(r->locked_until, 0);
}

TEST_F(UserRepoTest, AnonymizeNullsPii) {
    int64_t id = repo_->Insert({"frank", "Frank User", "hash", "auditor"}, 1000);
    repo_->Anonymize(id, 9999);
    auto r = repo_->FindById(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->display_name, "[anonymized]");
    EXPECT_TRUE(r->password_hash.empty());
    EXPECT_NE(r->anonymized_at, 0);
    EXPECT_FALSE(r->is_active);
}

TEST_F(UserRepoTest, IsEmptyTrueInitially) {
    EXPECT_TRUE(repo_->IsEmpty());
    repo_->Insert({"u", "U", "h", "auditor"}, 1);
    EXPECT_FALSE(repo_->IsEmpty());
}
