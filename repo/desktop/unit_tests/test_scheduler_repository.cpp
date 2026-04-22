#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/SchedulerRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
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

class SchedRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_ = std::make_unique<SchedulerRepository>(*db_);
    }
    std::unique_ptr<Database>             db_;
    std::unique_ptr<SchedulerRepository>  repo_;
};

TEST_F(SchedRepoTest, InsertAndListActiveJobs) {
    ScheduledJobRecord rec;
    rec.name = "daily_report"; rec.job_type = JobType::ReportGenerate;
    rec.parameters_json = "{}"; rec.priority = 5; rec.max_concurrency = 2;
    rec.is_active = true; rec.created_by = 1; rec.created_at = 1000;

    int64_t jid = repo_->InsertScheduledJob(rec);
    EXPECT_GT(jid, 0);

    auto jobs = repo_->ListActiveJobs();
    ASSERT_EQ(1u, jobs.size());
    EXPECT_EQ("daily_report", jobs[0].name);
}

TEST_F(SchedRepoTest, InsertJobRunLifecycle) {
    ScheduledJobRecord rec;
    rec.name = "j2"; rec.job_type = JobType::AlertScan;
    rec.parameters_json = "{}"; rec.priority = 3; rec.max_concurrency = 1;
    rec.is_active = true; rec.created_by = 1; rec.created_at = 1000;
    int64_t jid = repo_->InsertScheduledJob(rec);

    int64_t rid = repo_->InsertJobRun(jid, 2000);
    EXPECT_GT(rid, 0);

    repo_->UpdateJobRunStatus(rid, "completed", "", "{}", 3000);
    // No assertion on status here; just verify no throw.
}

TEST_F(SchedRepoTest, DependencyInsertAndList) {
    ScheduledJobRecord r1, r2;
    r1.name = "j3a"; r1.job_type = JobType::ReportGenerate;
    r1.parameters_json = "{}"; r1.priority = 5; r1.max_concurrency = 1;
    r1.is_active = true; r1.created_by = 1; r1.created_at = 1000;
    r2 = r1; r2.name = "j3b";

    int64_t a = repo_->InsertScheduledJob(r1);
    int64_t b = repo_->InsertScheduledJob(r2);
    repo_->InsertDependency(a, b);

    auto deps = repo_->ListAllDependencies();
    ASSERT_EQ(1u, deps.size());
    EXPECT_EQ(a, deps[0].job_id);
    EXPECT_EQ(b, deps[0].depends_on_job_id);
}

TEST_F(SchedRepoTest, WorkerLeaseAcquireAndRelease) {
    ScheduledJobRecord rec;
    rec.name = "j4"; rec.job_type = JobType::RetentionRun;
    rec.parameters_json = "{}"; rec.priority = 5; rec.max_concurrency = 1;
    rec.is_active = true; rec.created_by = 1; rec.created_at = 1000;
    int64_t jid = repo_->InsertScheduledJob(rec);
    int64_t rid = repo_->InsertJobRun(jid, 2000);

    int64_t lid = repo_->AcquireLease(rid, "worker-1", 3060, 3000);
    EXPECT_GT(lid, 0);
    repo_->ReleaseLease(lid);
    // No throw = success.
}
