#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/ui/controllers/AuditLogController.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::ui::controllers;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
}

static std::string RoleToString(UserRole role) {
    switch (role) {
    case UserRole::Administrator: return "administrator";
    case UserRole::OperationsManager: return "operations_manager";
    case UserRole::InventoryClerk: return "inventory_clerk";
    case UserRole::Auditor: return "auditor";
    }
    return "unknown";
}

static void SeedEvent(AuditRepository& repo,
                      const std::string& event_type,
                      const std::string& entity_type,
                      int64_t entity_id,
                      const std::string& description,
                      int64_t occurred_at,
                      const UserContext& actor) {
    AuditEvent e;
    e.occurred_at = occurred_at;
    e.actor_user_id = actor.user_id;
    e.actor_role = RoleToString(actor.role);
    e.event_type = event_type;
    e.entity_type = entity_type;
    e.entity_id = entity_id;
    e.description = description;
    repo.Append(e);
}

class AuditLogCtrlTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_         = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_  = std::make_unique<AuditService>(*audit_repo_);
        ctrl_       = std::make_unique<AuditLogController>(*audit_repo_);

        mgr_ctx_.user_id = 1;
        mgr_ctx_.role    = UserRole::OperationsManager;
        auditor_ctx_.user_id = 2;
        auditor_ctx_.role    = UserRole::Auditor;
    }

    std::unique_ptr<Database>         db_;
    std::unique_ptr<AuditRepository>  audit_repo_;
    std::unique_ptr<AuditService>     audit_svc_;
    std::unique_ptr<AuditLogController> ctrl_;
    UserContext                        mgr_ctx_;
    UserContext                        auditor_ctx_;
};

TEST_F(AuditLogCtrlTest, InitialStateIsIdle) {
    EXPECT_EQ(AuditLogState::Idle, ctrl_->State());
    EXPECT_TRUE(ctrl_->Events().empty());
}

TEST_F(AuditLogCtrlTest, ManagerCanRefreshAuditLog) {
    // Seed an event
    SeedEvent(*audit_repo_, "LOGIN", "user", 1, "User logged in", 1000, mgr_ctx_);
    ctrl_->Refresh(mgr_ctx_, 2000);
    EXPECT_EQ(AuditLogState::Loaded, ctrl_->State());
    EXPECT_GE(ctrl_->Events().size(), 1u);
}

TEST_F(AuditLogCtrlTest, AuditorCanRefreshAuditLog) {
    SeedEvent(*audit_repo_, "BOOKING_CREATED", "booking", 42, "Booking created", 1000, mgr_ctx_);
    ctrl_->Refresh(auditor_ctx_, 2000);
    // Auditors have audit log access (read-only)
    EXPECT_EQ(AuditLogState::Loaded, ctrl_->State());
}

TEST_F(AuditLogCtrlTest, FilterByEventTypeNarrowsResults) {
    SeedEvent(*audit_repo_, "LOGIN", "user", 1, "login", 1000, mgr_ctx_);
    SeedEvent(*audit_repo_, "BOOKING_CREATED", "booking", 1, "booking", 2000, mgr_ctx_);

    ctrl_->Filter().event_type = "LOGIN";
    ctrl_->Refresh(mgr_ctx_, 5000);
    EXPECT_EQ(AuditLogState::Loaded, ctrl_->State());
    for (const auto& e : ctrl_->Events())
        EXPECT_EQ("LOGIN", e.event_type);
}

TEST_F(AuditLogCtrlTest, EmptyFilterReturnsAllEvents) {
    SeedEvent(*audit_repo_, "EVT_A", "x", 1, "a", 1000, mgr_ctx_);
    SeedEvent(*audit_repo_, "EVT_B", "y", 2, "b", 2000, mgr_ctx_);
    ctrl_->Refresh(mgr_ctx_, 5000);
    EXPECT_GE(ctrl_->Events().size(), 2u);
}

TEST_F(AuditLogCtrlTest, ManagerExportCsvReturnsCsvContent) {
    SeedEvent(*audit_repo_, "LOGIN", "user", 1, "login", 1000, mgr_ctx_);
    ctrl_->Refresh(mgr_ctx_, 2000);
    std::string csv = ctrl_->ExportCsv(mgr_ctx_);
    EXPECT_FALSE(csv.empty());
    EXPECT_NE(std::string::npos, csv.find("occurred_at")); // header present
}

TEST_F(AuditLogCtrlTest, AuditorExportCsvIsMasked) {
    SeedEvent(*audit_repo_, "LOGIN", "user", 1, "Sensitive description", 1000, mgr_ctx_);
    ctrl_->Refresh(auditor_ctx_, 2000);
    std::string csv = ctrl_->ExportCsv(auditor_ctx_);
    EXPECT_FALSE(csv.empty());
    // Actor user IDs and descriptions must be masked for Auditor
    EXPECT_NE(std::string::npos, csv.find("[masked]"));
}

TEST_F(AuditLogCtrlTest, InventoryClerkCannotExportAuditLog) {
    SeedEvent(*audit_repo_, "EVT", "x", 1, "desc", 1000, mgr_ctx_);
    ctrl_->Refresh(mgr_ctx_, 2000);
    UserContext clerk; clerk.user_id = 3; clerk.role = UserRole::InventoryClerk;
    std::string csv = ctrl_->ExportCsv(clerk);
    EXPECT_TRUE(csv.empty()) << "InventoryClerk must not export audit log";
}

TEST_F(AuditLogCtrlTest, RefreshClearsDirtyFlag) {
    SeedEvent(*audit_repo_, "EVT", "x", 1, "d", 1000, mgr_ctx_);
    ctrl_->Refresh(mgr_ctx_, 2000);
    EXPECT_FALSE(ctrl_->IsDirty());
}

TEST_F(AuditLogCtrlTest, LimitRespected) {
    for (int i = 0; i < 10; ++i)
        SeedEvent(*audit_repo_, "EVT", "x", i, "d", 1000 + i, mgr_ctx_);
    ctrl_->Filter().limit = 3;
    ctrl_->Refresh(mgr_ctx_, 5000);
    EXPECT_LE(ctrl_->Events().size(), 3u);
}
