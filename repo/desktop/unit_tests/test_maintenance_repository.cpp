#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/MaintenanceRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL, row_label TEXT, x_coord_ft REAL DEFAULT 0, "
            "y_coord_ft REAL DEFAULT 0, description TEXT, is_active INTEGER DEFAULT 1)");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER, "
            "name TEXT NOT NULL, capacity INTEGER DEFAULT 1, current_purpose TEXT DEFAULT 'empty', "
            "nightly_price_cents INTEGER DEFAULT 0, rating REAL, is_active INTEGER DEFAULT 1, notes TEXT)");
    g->Exec("CREATE TABLE maintenance_tickets(ticket_id INTEGER PRIMARY KEY, "
            "zone_id INTEGER, kennel_id INTEGER, title TEXT NOT NULL, description TEXT, "
            "priority TEXT NOT NULL DEFAULT 'normal', status TEXT NOT NULL DEFAULT 'open', "
            "created_at INTEGER NOT NULL, created_by INTEGER, assigned_to INTEGER, "
            "first_action_at INTEGER, resolved_at INTEGER)");
    g->Exec("CREATE TABLE maintenance_events(event_id INTEGER PRIMARY KEY, "
            "ticket_id INTEGER NOT NULL, actor_id INTEGER NOT NULL, event_type TEXT NOT NULL, "
            "old_status TEXT, new_status TEXT, notes TEXT, occurred_at INTEGER NOT NULL)");
}

class MaintRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_ = std::make_unique<MaintenanceRepository>(*db_);
    }
    std::unique_ptr<Database>               db_;
    std::unique_ptr<MaintenanceRepository>  repo_;

    int64_t MakeTicket(const std::string& title, const std::string& priority,
                       int64_t now_unix) {
        NewTicketParams p;
        p.title = title; p.priority = priority; p.created_by = 1;
        return repo_->InsertTicket(p, now_unix);
    }
};

TEST_F(MaintRepoTest, InsertTicketAndImmutableCreatedAt) {
    int64_t id = MakeTicket("Leaky faucet", "normal", 5000);
    EXPECT_GT(id, 0);
    auto t = repo_->FindById(id);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(5000, t->created_at);
}

TEST_F(MaintRepoTest, SetFirstActionAtOnlyWhenNull) {
    int64_t id = MakeTicket("Broken door", "high", 1000);
    repo_->SetFirstActionAt(id, 1500);
    auto t = repo_->FindById(id);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(1500, t->first_action_at);

    repo_->SetFirstActionAt(id, 9999);
    t = repo_->FindById(id);
    EXPECT_EQ(1500, t->first_action_at);
}

TEST_F(MaintRepoTest, InsertEventAndListByTicket) {
    int64_t id = MakeTicket("Ticket", "low", 100);
    repo_->InsertEvent(id, 1, "status_changed", "open", "in_progress", "", 200);
    repo_->InsertEvent(id, 1, "note_added",     "", "", "Note here", 300);

    auto events = repo_->ListEventsFor(id);
    EXPECT_EQ(2u, events.size());
}

TEST_F(MaintRepoTest, ResolveTicketSetsResolvedAt) {
    int64_t id = MakeTicket("Cable loose", "normal", 100);
    repo_->SetResolvedAt(id, 5000);
    auto t = repo_->FindById(id);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(5000, t->resolved_at);
}

TEST_F(MaintRepoTest, GetResponsePointsReturnsFirstActionAndResolved) {
    int64_t id = MakeTicket("Noisy fan", "normal", 1000);
    repo_->SetFirstActionAt(id, 2000);
    repo_->SetResolvedAt(id, 5000);

    auto pts = repo_->GetResponsePoints(0, 99999);
    ASSERT_EQ(1u, pts.size());
    EXPECT_EQ(1000, pts[0].created_at);
    EXPECT_EQ(2000, pts[0].first_action_at);
    EXPECT_EQ(5000, pts[0].resolved_at);
}
