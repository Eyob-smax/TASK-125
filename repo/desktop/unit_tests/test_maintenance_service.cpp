#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/MaintenanceService.h"
#include "shelterops/services/AuditService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "event_type TEXT NOT NULL, description TEXT NOT NULL, actor_id INTEGER, "
            "occurred_at INTEGER NOT NULL, entity_type TEXT, entity_id INTEGER)");
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

class MaintSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_         = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_       = std::make_unique<MaintenanceRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_  = std::make_unique<AuditService>(*audit_repo_);
        svc_        = std::make_unique<MaintenanceService>(*repo_, *audit_svc_);
        ctx_.user_id = 1; ctx_.role = UserRole::Administrator;
    }

    std::unique_ptr<Database>               db_;
    std::unique_ptr<MaintenanceRepository>  repo_;
    std::unique_ptr<AuditRepository>        audit_repo_;
    std::unique_ptr<AuditService>           audit_svc_;
    std::unique_ptr<MaintenanceService>     svc_;
    UserContext                             ctx_;

    int64_t MakeTicket(const std::string& title, const std::string& priority,
                       int64_t now_unix) {
        NewTicketParams p;
        p.title = title; p.priority = priority; p.created_by = 1;
        return svc_->CreateTicket(p, ctx_, now_unix);
    }
};

TEST_F(MaintSvcTest, CreateTicketImmutableCreatedAt) {
    int64_t id = MakeTicket("Broken lock", "high", 5000);
    EXPECT_GT(id, 0);
    auto t = repo_->FindById(id);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(5000, t->created_at);
}

TEST_F(MaintSvcTest, RecordEventSetsFirstActionAt) {
    int64_t id = MakeTicket("Leaky pipe", "normal", 1000);
    svc_->RecordEvent(id, "status_changed", "in_progress", "", ctx_, 2000);

    auto t = repo_->FindById(id);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(2000, t->first_action_at);
}

TEST_F(MaintSvcTest, SecondEventDoesNotOverwriteFirstActionAt) {
    int64_t id = MakeTicket("Broken fan", "normal", 1000);
    svc_->RecordEvent(id, "status_changed", "in_progress", "", ctx_, 2000);
    svc_->RecordEvent(id, "status_changed", "resolved", "", ctx_, 3000);

    auto t = repo_->FindById(id);
    EXPECT_EQ(2000, t->first_action_at);
}

TEST_F(MaintSvcTest, ResolveTicketSetsResolvedAt) {
    int64_t id = MakeTicket("Cable", "low", 1000);
    svc_->Resolve(id, "", ctx_, 7000);
    auto t = repo_->FindById(id);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(7000, t->resolved_at);
}

TEST_F(MaintSvcTest, AssignTicket) {
    int64_t id = MakeTicket("Squeaky door", "normal", 1000);
    svc_->AssignTo(id, 1, ctx_, 1500);
    auto t = repo_->FindById(id);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(1, t->assigned_to);
}
