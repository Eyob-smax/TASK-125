#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/CrashCheckpoint.h"

using namespace shelterops::infrastructure;

static void CreateCheckpointTable(Database& db) {
    auto g = db.Acquire();
    g->Exec(
        "CREATE TABLE IF NOT EXISTS crash_checkpoints("
        "  checkpoint_id INTEGER PRIMARY KEY,"
        "  saved_at INTEGER NOT NULL,"
        "  window_state TEXT NOT NULL,"
        "  form_state TEXT)");
}

class CrashCheckpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateCheckpointTable(*db_);
        cp_   = std::make_unique<CrashCheckpoint>(*db_);
    }
    std::unique_ptr<Database>         db_;
    std::unique_ptr<CrashCheckpoint>  cp_;
};

TEST_F(CrashCheckpointTest, SaveAndLoadRoundTrip) {
    bool ok = cp_->SaveCheckpoint(
        R"({"windows":["KennelBoard"]})",
        R"({"kennel_filter":"Zone A"})");
    EXPECT_TRUE(ok);
    auto latest = cp_->LoadLatest();
    ASSERT_TRUE(latest.has_value());
    EXPECT_NE(latest->window_state.find("KennelBoard"), std::string::npos);
    EXPECT_NE(latest->form_state.find("Zone A"), std::string::npos);
}

TEST_F(CrashCheckpointTest, RejectsPayloadWithPasswordKey) {
    bool ok = cp_->SaveCheckpoint(
        R"({"password":"hunter2","windows":[]})",
        "{}");
    EXPECT_FALSE(ok);
    EXPECT_FALSE(cp_->LoadLatest().has_value());
}

TEST_F(CrashCheckpointTest, RejectsPayloadWithTokenKey) {
    bool ok = cp_->SaveCheckpoint(
        "{}",
        R"({"token":"abc123","form":"x"})");
    EXPECT_FALSE(ok);
}

TEST_F(CrashCheckpointTest, TrimKeepsOnlyMostRecent) {
    cp_->SaveCheckpoint(R"({"windows":["A"]})", "{}");
    cp_->SaveCheckpoint(R"({"windows":["B"]})", "{}");
    cp_->SaveCheckpoint(R"({"windows":["C"]})", "{}");
    cp_->Trim(1);
    // Count remaining rows
    int count = 0;
    db_->Acquire()->Query("SELECT COUNT(*) FROM crash_checkpoints", {},
                           [&count](const auto&, const auto& vals) {
                               if (!vals.empty()) count = std::stoi(vals[0]);
                           });
    EXPECT_EQ(count, 1);
}

TEST_F(CrashCheckpointTest, LoadLatestNulloptWhenEmpty) {
    EXPECT_FALSE(cp_->LoadLatest().has_value());
}
