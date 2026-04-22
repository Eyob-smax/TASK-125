#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/BookingRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL, row_label TEXT, x_coord_ft REAL NOT NULL DEFAULT 0, "
            "y_coord_ft REAL NOT NULL DEFAULT 0, description TEXT, is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'boarding', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("INSERT INTO zones(zone_id,name,building,x_coord_ft,y_coord_ft) VALUES(1,'Z','B',0,0)");
    g->Exec("INSERT INTO kennels(kennel_id,zone_id,name) VALUES(1,1,'K1')");
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users(user_id,username,display_name,password_hash,role,created_at) "
            "VALUES(1,'admin','Admin','h','administrator',1)");
    g->Exec("CREATE TABLE animals(animal_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "species TEXT NOT NULL, intake_at INTEGER NOT NULL, intake_type TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'intake')");
    g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER NOT NULL, "
            "animal_id INTEGER, guest_name TEXT, guest_phone_enc TEXT, guest_email_enc TEXT, "
            "check_in_at INTEGER NOT NULL, check_out_at INTEGER NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'pending', nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "total_price_cents INTEGER NOT NULL DEFAULT 0, special_requirements TEXT, "
            "created_by INTEGER, created_at INTEGER NOT NULL, approved_by INTEGER, approved_at INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE booking_approvals(approval_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER NOT NULL, requested_by INTEGER NOT NULL, "
            "requested_at INTEGER NOT NULL, approver_id INTEGER, decision TEXT, decided_at INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE boarding_fees(fee_id INTEGER PRIMARY KEY, booking_id INTEGER NOT NULL, "
            "amount_cents INTEGER NOT NULL, due_at INTEGER NOT NULL, paid_at INTEGER, "
            "payment_method TEXT, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE recommendation_results(result_id INTEGER PRIMARY KEY, "
            "query_hash TEXT NOT NULL, kennel_id INTEGER NOT NULL, rank_position INTEGER NOT NULL, "
            "score REAL NOT NULL, reason_json TEXT NOT NULL, generated_at INTEGER NOT NULL)");
}

class BookingRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_ = std::make_unique<BookingRepository>(*db_);
    }
    std::unique_ptr<Database>          db_;
    std::unique_ptr<BookingRepository> repo_;
};

TEST_F(BookingRepoTest, InsertAndFindById) {
    NewBookingParams p;
    p.kennel_id           = 1;
    p.check_in_at         = 1000;
    p.check_out_at        = 2000;
    p.nightly_price_cents = 3000;
    p.total_price_cents   = 3000;
    p.created_by          = 1;

    int64_t id = repo_->Insert(p, 500);
    EXPECT_GT(id, 0);
    auto r = repo_->FindById(id);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(1, r->kennel_id);
    EXPECT_EQ(BookingStatus::Pending, r->status);
}

TEST_F(BookingRepoTest, ListOverlappingExcludesCancelledAndNoShow) {
    NewBookingParams p;
    p.kennel_id = 1; p.check_in_at = 1000; p.check_out_at = 2000;
    p.nightly_price_cents = 0; p.total_price_cents = 0;
    p.created_by = 1;

    int64_t b1 = repo_->Insert(p, 0);
    int64_t b2 = repo_->Insert(p, 0);
    repo_->UpdateStatus(b2, BookingStatus::Cancelled, 1, 100);

    DateRange win{1000, 2000};
    auto overlapping = repo_->ListOverlapping(1, win);
    ASSERT_EQ(1u, overlapping.size());
    EXPECT_EQ(b1, overlapping[0].booking_id);
}

TEST_F(BookingRepoTest, ListOverlappingOnlyIntersectingWindow) {
    NewBookingParams p;
    p.kennel_id = 1; p.created_by = 1;
    p.nightly_price_cents = 0; p.total_price_cents = 0;

    p.check_in_at = 1000; p.check_out_at = 1500;
    repo_->Insert(p, 0); // overlaps [1200,1800)

    p.check_in_at = 2000; p.check_out_at = 3000;
    repo_->Insert(p, 0); // does not overlap [1200,1800)

    DateRange win{1200, 1800};
    auto overlapping = repo_->ListOverlapping(1, win);
    ASSERT_EQ(1u, overlapping.size());
}

TEST_F(BookingRepoTest, ApprovalRoundTrip) {
    NewBookingParams p;
    p.kennel_id = 1; p.check_in_at = 500; p.check_out_at = 600;
    p.nightly_price_cents = 0; p.total_price_cents = 0;
    p.created_by = 1;
    int64_t bid = repo_->Insert(p, 0);

    int64_t appr_id = repo_->InsertApprovalRequest(bid, 1, 10);
    EXPECT_GT(appr_id, 0);

    auto appr = repo_->FindApprovalByBooking(bid);
    ASSERT_TRUE(appr.has_value());
    EXPECT_EQ(appr_id, appr->approval_id);
    EXPECT_TRUE(appr->decision.empty());

    repo_->DecideApproval(appr_id, "approved", 1, 20);
    appr = repo_->FindApprovalByBooking(bid);
    EXPECT_EQ("approved", appr->decision);
}

TEST_F(BookingRepoTest, RecommendationResultsRoundTrip) {
    repo_->InsertRecommendationResult("hash123", 1, 1, 0.95f, R"([{"code":"OK"}])", 1000);
    auto recs = repo_->ListRecommendationsFor("hash123");
    ASSERT_EQ(1u, recs.size());
    EXPECT_EQ(1, recs[0].kennel_id);
}
