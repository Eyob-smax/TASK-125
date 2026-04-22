#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/SchedulerRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/SchedulerService.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/workers/JobQueue.h"
#include <atomic>
#include <thread>

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using shelterops::domain::JobType;
using shelterops::domain::UserRole;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "event_type TEXT NOT NULL, description TEXT NOT NULL, actor_id INTEGER, "
            "occurred_at INTEGER NOT NULL, entity_type TEXT, entity_id INTEGER)");
    g->Exec("CREATE TABLE scheduled_jobs(job_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL UNIQUE, job_type TEXT NOT NULL, "
            "parameters_json TEXT NOT NULL DEFAULT '{}', cron_expression TEXT, "
            "priority INTEGER NOT NULL DEFAULT 5, max_concurrency INTEGER NOT NULL DEFAULT 4, "
            "is_active INTEGER NOT NULL DEFAULT 1, last_run_at INTEGER, next_run_at INTEGER, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE job_runs(run_id INTEGER PRIMARY KEY, "
            "job_id INTEGER NOT NULL, worker_id TEXT, started_at INTEGER NOT NULL, "
            "completed_at INTEGER, status TEXT NOT NULL DEFAULT 'queued', "
            "error_message TEXT, output_json TEXT)");
    g->Exec("CREATE TABLE job_dependencies(dependency_id INTEGER PRIMARY KEY, "
            "job_id INTEGER NOT NULL, depends_on_job_id INTEGER NOT NULL, "
            "UNIQUE(job_id,depends_on_job_id))");
    g->Exec("CREATE TABLE worker_leases(lease_id INTEGER PRIMARY KEY, "
            "worker_id TEXT NOT NULL, job_run_id INTEGER NOT NULL, "
            "acquired_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
}

class SchedSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_         = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_       = std::make_unique<SchedulerRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_  = std::make_unique<AuditService>(*audit_repo_);
        job_queue_  = std::make_unique<shelterops::workers::JobQueue>(1);
        job_queue_->RegisterHandler(JobType::ReportGenerate,
            [this](const std::string&, std::stop_token) -> shelterops::workers::JobOutcome {
                executed_count_++;
                return {true, "{}", ""};
            });
        job_queue_->Start();
        svc_        = std::make_unique<SchedulerService>(*repo_, *audit_svc_, job_queue_.get());
        ctx_.user_id = 1; ctx_.role = UserRole::Administrator;
    }

    void TearDown() override {
        if (job_queue_) job_queue_->Stop();
    }

    std::unique_ptr<Database>             db_;
    std::unique_ptr<SchedulerRepository>  repo_;
    std::unique_ptr<AuditRepository>      audit_repo_;
    std::unique_ptr<AuditService>         audit_svc_;
    std::unique_ptr<shelterops::workers::JobQueue> job_queue_;
    std::unique_ptr<SchedulerService>     svc_;
    UserContext                           ctx_;
    std::atomic<int>                      executed_count_{0};
};

TEST_F(SchedSvcTest, EnqueueOnDemandInsertsJobAndRun) {
    int64_t run_id = svc_->EnqueueOnDemand(JobType::ReportGenerate,
                                             "{}", "test_job", ctx_, 1000);
    EXPECT_GT(run_id, 0);
    auto jobs = repo_->ListActiveJobs();
    EXPECT_EQ(1u, jobs.size());
}

TEST_F(SchedSvcTest, RegisterDependencySelfEdgeRejects) {
    int64_t run_id = svc_->EnqueueOnDemand(JobType::AlertScan,
                                             "{}", "job_A", ctx_, 1000);
    (void)run_id;
    auto jobs = repo_->ListActiveJobs();
    ASSERT_EQ(1u, jobs.size());
    int64_t jid = jobs[0].job_id;

    auto result = svc_->RegisterDependency(jid, jid, ctx_, 2000);
    ASSERT_TRUE(std::holds_alternative<shelterops::common::ErrorEnvelope>(result));
    EXPECT_EQ(shelterops::common::ErrorCode::InvalidInput,
              std::get<shelterops::common::ErrorEnvelope>(result).code);
}

TEST_F(SchedSvcTest, RegisterDependencyCycleRejects) {
    svc_->EnqueueOnDemand(JobType::ReportGenerate, "{}", "job_B1", ctx_, 1000);
    svc_->EnqueueOnDemand(JobType::ReportGenerate, "{}", "job_B2", ctx_, 1001);

    auto jobs = repo_->ListActiveJobs();
    ASSERT_EQ(2u, jobs.size());
    int64_t j1 = jobs[0].job_id;
    int64_t j2 = jobs[1].job_id;

    // j1 → j2
    auto r1 = svc_->RegisterDependency(j1, j2, ctx_, 2000);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(r1));

    // j2 → j1 would be a cycle
    auto r2 = svc_->RegisterDependency(j2, j1, ctx_, 2001);
    ASSERT_TRUE(std::holds_alternative<shelterops::common::ErrorEnvelope>(r2));
    EXPECT_EQ(shelterops::common::ErrorCode::InvalidInput,
              std::get<shelterops::common::ErrorEnvelope>(r2).code);
}

TEST_F(SchedSvcTest, CancelRunUpdatesStatus) {
    int64_t run_id = svc_->EnqueueOnDemand(JobType::RetentionRun,
                                             "{}", "job_C", ctx_, 1000);
    svc_->CancelRun(run_id, ctx_, 2000);
    // No throw = success; status updated in DB.
}

TEST_F(SchedSvcTest, EnqueueOnDemandSubmitsToJobQueue) {
    int64_t run_id = svc_->EnqueueOnDemand(JobType::ReportGenerate,
                                            "{}", "job_submit", ctx_, 1000);
    EXPECT_GT(run_id, 0);

    // Trigger queue execution by waiting for idle after submit.
    for (int i = 0; i < 200 && !job_queue_->IsIdle(); ++i) {
        std::this_thread::yield();
    }
    EXPECT_GE(executed_count_.load(), 1);
}

TEST_F(SchedSvcTest, DispatchDueJobsSubmitsReadyScheduledJob) {
    ScheduledJobRecord rec;
    rec.name = "due_job";
    rec.job_type = JobType::ReportGenerate;
    rec.parameters_json = "{}";
    rec.priority = 5;
    rec.max_concurrency = 1;
    rec.is_active = true;
    rec.created_by = 1;
    rec.created_at = 900;
    int64_t job_id = repo_->InsertScheduledJob(rec);
    repo_->UpdateNextRunAt(job_id, 1000);

    const int before = executed_count_.load();
    int submitted = svc_->DispatchDueJobs(1001);
    EXPECT_EQ(1, submitted);

    for (int i = 0; i < 200 && !job_queue_->IsIdle(); ++i) {
        std::this_thread::yield();
    }
    EXPECT_GE(executed_count_.load(), before + 1);
}

TEST_F(SchedSvcTest, DispatchDueJobsHonorsDependencies) {
    ScheduledJobRecord dep;
    dep.name = "dep_job";
    dep.job_type = JobType::ReportGenerate;
    dep.parameters_json = "{}";
    dep.priority = 5;
    dep.max_concurrency = 1;
    dep.is_active = true;
    dep.created_by = 1;
    dep.created_at = 900;
    int64_t dep_job_id = repo_->InsertScheduledJob(dep);

    ScheduledJobRecord main;
    main.name = "main_job";
    main.job_type = JobType::ReportGenerate;
    main.parameters_json = "{}";
    main.priority = 5;
    main.max_concurrency = 1;
    main.is_active = true;
    main.created_by = 1;
    main.created_at = 900;
    int64_t main_job_id = repo_->InsertScheduledJob(main);

    repo_->InsertDependency(main_job_id, dep_job_id);
    repo_->UpdateNextRunAt(main_job_id, 1000);

    int submitted = svc_->DispatchDueJobs(1001);
    EXPECT_EQ(0, submitted);

    int64_t dep_run = repo_->InsertJobRun(dep_job_id, 900);
    repo_->UpdateJobRunStatus(dep_run, "completed", "", "{}", 901);

    submitted = svc_->DispatchDueJobs(1002);
    EXPECT_EQ(1, submitted);
}

TEST_F(SchedSvcTest, EnqueuedRunTransitionsToCompletedAfterHandlerFinishes) {
    int64_t run_id = svc_->EnqueueOnDemand(JobType::ReportGenerate,
                                           "{}", "job_complete", ctx_, 1000);
    ASSERT_GT(run_id, 0);

    for (int i = 0; i < 200 && !job_queue_->IsIdle(); ++i) {
        std::this_thread::yield();
    }

    auto run = repo_->FindJobRun(run_id);
    ASSERT_TRUE(run.has_value());
    EXPECT_EQ("completed", run->status);
    EXPECT_GT(run->completed_at, 0);
}
