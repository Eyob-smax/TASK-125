#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/SchedulerRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/SchedulerService.h"
#include "shelterops/ui/controllers/SchedulerPanelController.h"

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
    g->Exec("CREATE TABLE scheduled_jobs(job_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, job_type TEXT NOT NULL, parameters_json TEXT DEFAULT '{}', "
            "cron_expression TEXT, priority INTEGER DEFAULT 5, "
            "max_concurrency INTEGER DEFAULT 4, is_active INTEGER DEFAULT 1, "
            "last_run_at INTEGER DEFAULT 0, next_run_at INTEGER DEFAULT 0, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE job_runs(run_id INTEGER PRIMARY KEY, "
            "job_id INTEGER NOT NULL, worker_id TEXT, "
            "started_at INTEGER NOT NULL, completed_at INTEGER DEFAULT 0, "
            "status TEXT NOT NULL DEFAULT 'queued', error_message TEXT, output_json TEXT)");
    g->Exec("CREATE TABLE job_dependencies(dependency_id INTEGER PRIMARY KEY, "
            "job_id INTEGER NOT NULL, depends_on_job_id INTEGER NOT NULL, "
            "UNIQUE(job_id, depends_on_job_id))");
    g->Exec("CREATE TABLE worker_leases(lease_id INTEGER PRIMARY KEY, "
            "worker_id TEXT NOT NULL, job_run_id INTEGER NOT NULL, "
            "acquired_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
}

class SchedulerPanelCtrlTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_          = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        sched_repo_  = std::make_unique<SchedulerRepository>(*db_);
        audit_repo_  = std::make_unique<AuditRepository>(*db_);
        audit_svc_   = std::make_unique<AuditService>(*audit_repo_);
        sched_svc_   = std::make_unique<SchedulerService>(*sched_repo_, *audit_svc_);
        ctrl_        = std::make_unique<SchedulerPanelController>(*sched_svc_, *sched_repo_);

        mgr_ctx_.user_id = 1;
        mgr_ctx_.role    = UserRole::OperationsManager;
        auditor_ctx_.user_id = 2;
        auditor_ctx_.role    = UserRole::Auditor;
    }

    int64_t InsertJob(const std::string& name, const std::string& job_type = "report_generate") {
        ScheduledJobRecord r;
        r.name        = name;
        r.job_type    = JobType::ReportGenerate;
        r.is_active   = true;
        r.created_at  = 1000;
        return sched_repo_->InsertScheduledJob(r);
    }

    std::unique_ptr<Database>              db_;
    std::unique_ptr<SchedulerRepository>   sched_repo_;
    std::unique_ptr<AuditRepository>       audit_repo_;
    std::unique_ptr<AuditService>          audit_svc_;
    std::unique_ptr<SchedulerService>      sched_svc_;
    std::unique_ptr<SchedulerPanelController> ctrl_;
    UserContext                            mgr_ctx_;
    UserContext                            auditor_ctx_;
};

TEST_F(SchedulerPanelCtrlTest, InitialStateIsIdle) {
    EXPECT_EQ(SchedulerPanelState::Idle, ctrl_->State());
    EXPECT_TRUE(ctrl_->Jobs().empty());
    EXPECT_FALSE(ctrl_->Detail().has_value());
}

TEST_F(SchedulerPanelCtrlTest, RefreshLoadsActiveJobs) {
    InsertJob("Daily Report");
    InsertJob("Weekly Retention");
    ctrl_->Refresh(mgr_ctx_, 5000);
    EXPECT_EQ(SchedulerPanelState::Loaded, ctrl_->State());
    EXPECT_EQ(2u, ctrl_->Jobs().size());
}

TEST_F(SchedulerPanelCtrlTest, RefreshEmptyDbStaysLoaded) {
    ctrl_->Refresh(mgr_ctx_, 5000);
    EXPECT_EQ(SchedulerPanelState::Loaded, ctrl_->State());
    EXPECT_TRUE(ctrl_->Jobs().empty());
}

TEST_F(SchedulerPanelCtrlTest, ViewJobDetailPopulatesDetail) {
    int64_t jid = InsertJob("Alert Scan");
    ctrl_->Refresh(mgr_ctx_, 5000);
    ctrl_->ViewJobDetail(jid);
    EXPECT_EQ(SchedulerPanelState::DetailView, ctrl_->State());
    ASSERT_TRUE(ctrl_->Detail().has_value());
    EXPECT_EQ(jid, ctrl_->Detail()->job.job_id);
}

TEST_F(SchedulerPanelCtrlTest, ViewJobDetailContainsPipelineStages) {
    int64_t jid = InsertJob("Report Job");
    ctrl_->ViewJobDetail(jid);
    ASSERT_TRUE(ctrl_->Detail().has_value());
    const auto& d = *ctrl_->Detail();
    EXPECT_NE(std::string::npos, d.stage_display.find("collect"))
        << "Stage display must include collect stage";
    EXPECT_NE(std::string::npos, d.stage_display.find("visualize"))
        << "Stage display must include visualize stage";
}

TEST_F(SchedulerPanelCtrlTest, ViewNonExistentJobSetsError) {
    ctrl_->ViewJobDetail(99999);
    EXPECT_EQ(SchedulerPanelState::Error, ctrl_->State());
}

TEST_F(SchedulerPanelCtrlTest, ManagerCanEnqueueJob) {
    int64_t jid = InsertJob("On-Demand Job");
    ctrl_->Refresh(mgr_ctx_, 5000);
    bool ok = ctrl_->EnqueueJob(jid, "{}", mgr_ctx_, 6000);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(ctrl_->IsDirty());
}

TEST_F(SchedulerPanelCtrlTest, AuditorCannotEnqueueJob) {
    int64_t jid = InsertJob("Restricted Job");
    ctrl_->Refresh(auditor_ctx_, 5000);
    bool ok = ctrl_->EnqueueJob(jid, "{}", auditor_ctx_, 6000);
    EXPECT_FALSE(ok) << "Auditor must not enqueue jobs";
}

TEST_F(SchedulerPanelCtrlTest, ClearDetailResetsToLoadedState) {
    int64_t jid = InsertJob("Job X");
    ctrl_->Refresh(mgr_ctx_, 5000);
    ctrl_->ViewJobDetail(jid);
    EXPECT_EQ(SchedulerPanelState::DetailView, ctrl_->State());
    ctrl_->ClearDetail();
    EXPECT_EQ(SchedulerPanelState::Loaded, ctrl_->State());
    EXPECT_FALSE(ctrl_->Detail().has_value());
}

TEST_F(SchedulerPanelCtrlTest, JobDetailIncludesDependencies) {
    int64_t job_a = InsertJob("Job A");
    int64_t job_b = InsertJob("Job B");
    sched_svc_.get()->RegisterDependency(job_b, job_a, mgr_ctx_, 1000);
    ctrl_->ViewJobDetail(job_b);
    ASSERT_TRUE(ctrl_->Detail().has_value());
    EXPECT_GE(ctrl_->Detail()->dependencies.size(), 1u);
}
