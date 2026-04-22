#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/CrashCheckpoint.h"
#include "shelterops/services/CheckpointService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::services;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE crash_checkpoints("
            "  checkpoint_id  INTEGER PRIMARY KEY,"
            "  window_state   TEXT    NOT NULL,"
            "  form_state     TEXT    NOT NULL,"
            "  saved_at       INTEGER NOT NULL"
            ")");
}

class CheckpointSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_          = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        crash_cp_    = std::make_unique<CrashCheckpoint>(*db_);
        svc_         = std::make_unique<CheckpointService>(*crash_cp_);
    }
    std::unique_ptr<Database>          db_;
    std::unique_ptr<CrashCheckpoint>   crash_cp_;
    std::unique_ptr<CheckpointService> svc_;
};

TEST_F(CheckpointSvcTest, CaptureAndRestoreRoundTrip) {
    WindowInventory wi;
    wi.open_window_ids = {"kennel_board", "item_ledger"};
    wi.active_window_id = "kennel_board";

    FormSnapshot fs;
    fs.window_id        = "item_ledger";
    fs.filter_json      = R"({"category":1})";
    fs.selected_row_key = "42";
    fs.draft_text       = "pending note";

    bool ok = svc_->CaptureState(wi, {fs}, 9000);
    EXPECT_TRUE(ok);

    auto restored = svc_->RestoreState();
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(9000, restored->saved_at);
    EXPECT_EQ(std::vector<std::string>({"kennel_board", "item_ledger"}),
              restored->windows.open_window_ids);
    EXPECT_EQ("kennel_board", restored->windows.active_window_id);

    ASSERT_EQ(1u, restored->forms.size());
    EXPECT_EQ("item_ledger", restored->forms[0].window_id);
    EXPECT_EQ("42", restored->forms[0].selected_row_key);
}

TEST_F(CheckpointSvcTest, PiiInDraftTextRejected) {
    WindowInventory wi;
    wi.active_window_id = "login";

    FormSnapshot fs;
    fs.window_id   = "login";
    fs.draft_text  = "password=secret123"; // contains PII marker

    bool ok = svc_->CaptureState(wi, {fs}, 1000);
    EXPECT_FALSE(ok);
}

TEST_F(CheckpointSvcTest, EmptyStateRestoresNullopt) {
    auto restored = svc_->RestoreState();
    EXPECT_FALSE(restored.has_value());
}

TEST_F(CheckpointSvcTest, TokenMarkerInFilterJsonRejected) {
    WindowInventory wi;
    wi.active_window_id = "item_ledger";

    FormSnapshot fs;
    fs.window_id    = "item_ledger";
    fs.filter_json  = R"({"token": "abc123"})"; // contains PII marker

    bool ok = svc_->CaptureState(wi, {fs}, 2000);
    EXPECT_FALSE(ok);
}

TEST_F(CheckpointSvcTest, EmailPatternInDraftTextRejected) {
    WindowInventory wi;
    wi.active_window_id = "login";

    FormSnapshot fs;
    fs.window_id  = "login";
    fs.draft_text = "user@example.com";

    bool ok = svc_->CaptureState(wi, {fs}, 3000);
    EXPECT_FALSE(ok);
}

TEST_F(CheckpointSvcTest, SafePayloadRoundTripsCompletely) {
    WindowInventory wi;
    wi.open_window_ids  = {"kennel_board", "item_ledger", "reports_studio"};
    wi.active_window_id = "reports_studio";

    FormSnapshot fs1;
    fs1.window_id        = "kennel_board";
    fs1.filter_json      = R"({"zone_id":2,"species":"dog"})";
    fs1.selected_row_key = "101";
    fs1.draft_text       = "";

    FormSnapshot fs2;
    fs2.window_id        = "item_ledger";
    fs2.filter_json      = R"({"category_id":1})";
    fs2.selected_row_key = "55";
    fs2.draft_text       = "pending receipt note";

    bool ok = svc_->CaptureState(wi, {fs1, fs2}, 8000);
    ASSERT_TRUE(ok);

    auto restored = svc_->RestoreState();
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(8000, restored->saved_at);
    EXPECT_EQ("reports_studio", restored->windows.active_window_id);
    EXPECT_EQ(3u, restored->windows.open_window_ids.size());
    ASSERT_EQ(2u, restored->forms.size());

    bool found_kb = false, found_il = false;
    for (const auto& f : restored->forms) {
        if (f.window_id == "kennel_board") { found_kb = true; EXPECT_EQ("101", f.selected_row_key); }
        if (f.window_id == "item_ledger")  { found_il = true; EXPECT_EQ("55",  f.selected_row_key); }
    }
    EXPECT_TRUE(found_kb);
    EXPECT_TRUE(found_il);
}

TEST_F(CheckpointSvcTest, OverwrittenCheckpointReturnsLatest) {
    WindowInventory wi1; wi1.active_window_id = "kennel_board";
    svc_->CaptureState(wi1, {}, 1000);

    WindowInventory wi2; wi2.active_window_id = "item_ledger";
    svc_->CaptureState(wi2, {}, 2000);

    auto restored = svc_->RestoreState();
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ("item_ledger", restored->windows.active_window_id);
    EXPECT_EQ(2000, restored->saved_at);
}
