#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/ReportRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE report_definitions(report_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, report_type TEXT NOT NULL, description TEXT, "
            "filter_json TEXT NOT NULL DEFAULT '{}', schedule_cron TEXT, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE report_runs(run_id INTEGER PRIMARY KEY, "
            "report_id INTEGER NOT NULL, version_label TEXT NOT NULL, triggered_by INTEGER, "
            "trigger_type TEXT NOT NULL, started_at INTEGER NOT NULL, completed_at INTEGER, "
            "status TEXT NOT NULL DEFAULT 'running', filter_json TEXT NOT NULL DEFAULT '{}', "
            "output_path TEXT, error_message TEXT, anomaly_flags_json TEXT, row_count INTEGER)");
    g->Exec("CREATE TABLE report_snapshots(snapshot_id INTEGER PRIMARY KEY, "
            "run_id INTEGER NOT NULL, metric_name TEXT NOT NULL, "
            "metric_value REAL NOT NULL, dimension_json TEXT NOT NULL DEFAULT '{}', "
            "captured_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE export_jobs(job_id INTEGER PRIMARY KEY, "
            "report_run_id INTEGER NOT NULL, format TEXT NOT NULL, "
            "requested_by INTEGER NOT NULL, queued_at INTEGER NOT NULL, "
            "started_at INTEGER, completed_at INTEGER, output_path TEXT, "
            "status TEXT NOT NULL DEFAULT 'queued', max_concurrency INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE watermark_rules(rule_id INTEGER PRIMARY KEY, "
            "report_type TEXT NOT NULL, role TEXT NOT NULL, export_format TEXT NOT NULL, "
            "requires_watermark INTEGER NOT NULL DEFAULT 0, restrictions_json TEXT NOT NULL DEFAULT '{}', "
            "UNIQUE(report_type,role,export_format))");
}

class ReportRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_ = std::make_unique<ReportRepository>(*db_);
    }
    std::unique_ptr<Database>          db_;
    std::unique_ptr<ReportRepository>  repo_;

    int64_t MakeDefinition(const std::string& name, const std::string& type) {
        ReportDefinitionRecord rec;
        rec.name = name; rec.report_type = type;
        rec.filter_json = "{}"; rec.created_by = 1; rec.created_at = 1000;
        return repo_->InsertDefinition(rec);
    }
};

TEST_F(ReportRepoTest, InsertAndFindDefinition) {
    int64_t id = MakeDefinition("Occ Report", "occupancy");
    EXPECT_GT(id, 0);
    auto d = repo_->FindDefinition(id);
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ("Occ Report", d->name);
    EXPECT_EQ("occupancy", d->report_type);
}

TEST_F(ReportRepoTest, InsertRunAndUpdateStatus) {
    int64_t did = MakeDefinition("Test", "occupancy");
    int64_t rid = repo_->InsertRun(did, "occupancy-20260420-001", "manual", 1, "{}", 1000);
    EXPECT_GT(rid, 0);

    auto runs = repo_->ListRunsForReport(did);
    ASSERT_EQ(1u, runs.size());
    EXPECT_EQ("running", runs[0].status);

    repo_->UpdateRunStatus(rid, "completed", "", "[]");
    runs = repo_->ListRunsForReport(did);
    EXPECT_EQ("completed", runs[0].status);
}

TEST_F(ReportRepoTest, InsertSnapshotAndListByRun) {
    int64_t did = MakeDefinition("Test", "occupancy");
    int64_t rid = repo_->InsertRun(did, "occupancy-20260420-001", "manual", 1, "{}", 1000);

    repo_->InsertSnapshot(rid, "occupancy_rate", 0.75, "{}", 1000);
    repo_->InsertSnapshot(rid, "vacant_kennels", 3.0, "{}", 1000);

    auto snaps = repo_->ListSnapshotsForRun(rid);
    EXPECT_EQ(2u, snaps.size());
}

TEST_F(ReportRepoTest, ExportJobLifecycle) {
    int64_t did = MakeDefinition("Test", "occupancy");
    int64_t rid = repo_->InsertRun(did, "occupancy-20260420-001", "manual", 1, "{}", 1000);
    int64_t jid = repo_->InsertExportJob(rid, "csv", 1, 1, 1000);
    EXPECT_GT(jid, 0);

    auto pending = repo_->ListPendingExportJobs();
    ASSERT_EQ(1u, pending.size());

    repo_->UpdateExportJobStatus(jid, "completed", "exports/out.csv", 2000);
    auto job = repo_->FindExportJob(jid);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ("completed", job->status);
    EXPECT_EQ("exports/out.csv", job->output_path);
}

TEST_F(ReportRepoTest, CountRunsForReportOnDay) {
    int64_t did = MakeDefinition("Test", "occupancy");
    int64_t day = 1745712000; // 2026-04-27 00:00:00 UTC (any fixed unix day)
    repo_->InsertRun(did, "lbl-1", "manual", 1, "{}", day);
    repo_->InsertRun(did, "lbl-2", "manual", 1, "{}", day + 3600);
    int count = repo_->CountRunsForReportOnDay(did, day);
    EXPECT_EQ(2, count);
}
