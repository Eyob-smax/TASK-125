#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/workers/JobQueue.h"
#include "shelterops/ui/controllers/DiagnosticsController.h"

using namespace shelterops::infrastructure;
using namespace shelterops::workers;
using namespace shelterops::ui::controllers;

class DiagnosticsCtrlTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        auto g = db_->Acquire();
        // Minimal DB setup so PRAGMA queries succeed
        g->Exec("CREATE TABLE dummy(x INTEGER)");
        queue_ = std::make_unique<JobQueue>(1);
        ctrl_  = std::make_unique<DiagnosticsController>(*queue_, *db_);
    }

    std::unique_ptr<Database>              db_;
    std::unique_ptr<JobQueue>              queue_;
    std::unique_ptr<DiagnosticsController> ctrl_;
};

TEST_F(DiagnosticsCtrlTest, WorkerStatusIdleWhenNotStarted) {
    auto ws = ctrl_->GetWorkerStatus();
    // Queue not started → IsIdle() should be true
    EXPECT_TRUE(ws.is_idle);
}

TEST_F(DiagnosticsCtrlTest, DatabaseStatsHasPositivePageSize) {
    auto stats = ctrl_->GetDatabaseStats();
    EXPECT_GT(stats.page_size_bytes, 0)
        << "SQLite page_size must be positive";
}

TEST_F(DiagnosticsCtrlTest, DatabaseStatsHasJournalMode) {
    auto stats = ctrl_->GetDatabaseStats();
    EXPECT_FALSE(stats.journal_mode.empty())
        << "journal_mode PRAGMA must return a value";
}

TEST_F(DiagnosticsCtrlTest, CacheStatsEmptyBeforeRegistration) {
    ctrl_->Refresh();
    EXPECT_TRUE(ctrl_->GetCacheStats().empty());
}

TEST_F(DiagnosticsCtrlTest, RegisteredCacheAppearsInStats) {
    ctrl_->RegisterCache("TestCache", []() -> std::pair<std::size_t, std::size_t> {
        return {3, 100};
    });
    ctrl_->Refresh();
    ASSERT_EQ(1u, ctrl_->GetCacheStats().size());
    EXPECT_EQ("TestCache", ctrl_->GetCacheStats()[0].name);
    EXPECT_EQ(3u, ctrl_->GetCacheStats()[0].current_size);
    EXPECT_EQ(100u, ctrl_->GetCacheStats()[0].max_size);
}

TEST_F(DiagnosticsCtrlTest, MultipleCachesTrackedIndependently) {
    ctrl_->RegisterCache("CacheA", []() -> std::pair<std::size_t, std::size_t> {
        return {5, 50};
    });
    ctrl_->RegisterCache("CacheB", []() -> std::pair<std::size_t, std::size_t> {
        return {10, 200};
    });
    ctrl_->Refresh();
    ASSERT_EQ(2u, ctrl_->GetCacheStats().size());
    bool found_a = false, found_b = false;
    for (const auto& cs : ctrl_->GetCacheStats()) {
        if (cs.name == "CacheA") { found_a = true; EXPECT_EQ(5u, cs.current_size); }
        if (cs.name == "CacheB") { found_b = true; EXPECT_EQ(10u, cs.current_size); }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TEST_F(DiagnosticsCtrlTest, RefreshUpdatesCacheStats) {
    std::size_t val = 1;
    ctrl_->RegisterCache("Dynamic", [&val]() -> std::pair<std::size_t, std::size_t> {
        return {val, 10};
    });
    ctrl_->Refresh();
    EXPECT_EQ(1u, ctrl_->GetCacheStats()[0].current_size);

    val = 7;
    ctrl_->Refresh();
    EXPECT_EQ(7u, ctrl_->GetCacheStats()[0].current_size);
}

TEST_F(DiagnosticsCtrlTest, DatabaseStatsPageCountNonNegative) {
    auto stats = ctrl_->GetDatabaseStats();
    EXPECT_GE(stats.page_count, 0);
}
