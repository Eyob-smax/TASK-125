#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/SessionRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;

static void CreateSessionsTable(Database& db) {
    auto g = db.Acquire();
    g->Exec(
        "CREATE TABLE IF NOT EXISTS user_sessions("
        "  session_id TEXT PRIMARY KEY,"
        "  user_id INTEGER NOT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  expires_at INTEGER NOT NULL,"
        "  device_fingerprint TEXT,"
        "  is_active INTEGER NOT NULL DEFAULT 1,"
        "  absolute_expires_at INTEGER)");
}

class SessionRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSessionsTable(*db_);
        repo_ = std::make_unique<SessionRepository>(*db_);
    }
    std::unique_ptr<Database>           db_;
    std::unique_ptr<SessionRepository>  repo_;
};

TEST_F(SessionRepoTest, InsertAndFind) {
    SessionRecord r{"sess-1", 42, 1000, 5000, "fp-abc", true};
    repo_->Insert(r);
    auto found = repo_->FindById("sess-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->user_id, 42);
    EXPECT_EQ(found->expires_at, 5000);
    EXPECT_TRUE(found->is_active);
}

TEST_F(SessionRepoTest, MissingSessionReturnsNullopt) {
    EXPECT_FALSE(repo_->FindById("nonexistent").has_value());
}

TEST_F(SessionRepoTest, MarkInactive) {
    repo_->Insert({"sess-x", 1, 100, 9999, "", true});
    repo_->MarkInactive("sess-x");
    auto found = repo_->FindById("sess-x");
    ASSERT_TRUE(found.has_value());
    EXPECT_FALSE(found->is_active);
}

TEST_F(SessionRepoTest, ExpireAllForUser) {
    repo_->Insert({"s1", 7, 100, 9999, "", true});
    repo_->Insert({"s2", 7, 100, 9999, "", true});
    repo_->Insert({"s3", 8, 100, 9999, "", true}); // different user
    repo_->ExpireAllForUser(7);
    EXPECT_FALSE(repo_->FindById("s1")->is_active);
    EXPECT_FALSE(repo_->FindById("s2")->is_active);
    EXPECT_TRUE(repo_->FindById("s3")->is_active); // unchanged
}

TEST_F(SessionRepoTest, PurgeExpiredRemovesOnlyExpired) {
    repo_->Insert({"old", 1, 100, 500,  "", true});
    repo_->Insert({"new", 1, 100, 9999, "", true});
    repo_->PurgeExpired(1000);
    EXPECT_FALSE(repo_->FindById("old").has_value());
    EXPECT_TRUE(repo_->FindById("new").has_value());
}
