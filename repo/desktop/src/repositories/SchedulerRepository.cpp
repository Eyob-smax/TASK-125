#include "shelterops/repositories/SchedulerRepository.h"

namespace shelterops::repositories {

SchedulerRepository::SchedulerRepository(infrastructure::Database& db) : db_(db) {}

domain::JobType SchedulerRepository::ParseJobType(const std::string& s) {
    if (s == "report_generate") return domain::JobType::ReportGenerate;
    if (s == "export_csv")      return domain::JobType::ExportCsv;
    if (s == "export_pdf")      return domain::JobType::ExportPdf;
    if (s == "retention_run")   return domain::JobType::RetentionRun;
    if (s == "alert_scan")      return domain::JobType::AlertScan;
    if (s == "lan_sync")        return domain::JobType::LanSync;
    return domain::JobType::Backup;
}

std::string SchedulerRepository::JobTypeToString(domain::JobType t) {
    switch (t) {
    case domain::JobType::ReportGenerate: return "report_generate";
    case domain::JobType::ExportCsv:      return "export_csv";
    case domain::JobType::ExportPdf:      return "export_pdf";
    case domain::JobType::RetentionRun:   return "retention_run";
    case domain::JobType::AlertScan:      return "alert_scan";
    case domain::JobType::LanSync:        return "lan_sync";
    default:                               return "backup";
    }
}

ScheduledJobRecord SchedulerRepository::RowToJob(const std::vector<std::string>& vals) {
    ScheduledJobRecord r;
    r.job_id          = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.name            = vals[1];
    r.job_type        = ParseJobType(vals[2]);
    r.parameters_json = vals[3].empty() ? "{}" : vals[3];
    r.cron_expression = vals[4];
    r.priority        = vals[5].empty() ? 5 : std::stoi(vals[5]);
    r.max_concurrency = vals[6].empty() ? 4 : std::stoi(vals[6]);
    r.is_active       = vals[7].empty() ? true : (vals[7] == "1");
    r.last_run_at     = vals[8].empty() ? 0 : std::stoll(vals[8]);
    r.next_run_at     = vals[9].empty() ? 0 : std::stoll(vals[9]);
    r.created_by      = vals[10].empty() ? 0 : std::stoll(vals[10]);
    r.created_at      = vals[11].empty() ? 0 : std::stoll(vals[11]);
    return r;
}

JobRunRecord SchedulerRepository::RowToRun(const std::vector<std::string>& vals) {
    JobRunRecord r;
    r.run_id       = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.job_id       = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.worker_id    = vals[2];
    r.started_at   = vals[3].empty() ? 0 : std::stoll(vals[3]);
    r.completed_at = vals[4].empty() ? 0 : std::stoll(vals[4]);
    r.status       = vals[5];
    r.error_message = vals[6];
    r.output_json  = vals[7];
    return r;
}

int64_t SchedulerRepository::InsertScheduledJob(const ScheduledJobRecord& rec) {
    static const std::string sql =
        "INSERT INTO scheduled_jobs "
        "(name, job_type, parameters_json, cron_expression, priority, max_concurrency, "
        " is_active, created_by, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {rec.name,
                     JobTypeToString(rec.job_type),
                     rec.parameters_json,
                     rec.cron_expression,
                     std::to_string(rec.priority),
                     std::to_string(rec.max_concurrency),
                     rec.is_active ? "1" : "0",
                     std::to_string(rec.created_by),
                     std::to_string(rec.created_at)});
    return conn->LastInsertRowId();
}

std::optional<ScheduledJobRecord> SchedulerRepository::FindJob(int64_t job_id) const {
    static const std::string sql =
        "SELECT job_id, name, job_type, COALESCE(parameters_json,'{}'), "
        "       COALESCE(cron_expression,''), priority, max_concurrency, is_active, "
        "       COALESCE(last_run_at,0), COALESCE(next_run_at,0), "
        "       COALESCE(created_by,0), created_at "
        "FROM scheduled_jobs WHERE job_id = ?";
    std::optional<ScheduledJobRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(job_id)},
        [&](const auto&, const auto& vals) { result = RowToJob(vals); });
    return result;
}

std::vector<ScheduledJobRecord> SchedulerRepository::ListActiveJobs() const {
    static const std::string sql =
        "SELECT job_id, name, job_type, COALESCE(parameters_json,'{}'), "
        "       COALESCE(cron_expression,''), priority, max_concurrency, is_active, "
        "       COALESCE(last_run_at,0), COALESCE(next_run_at,0), "
        "       COALESCE(created_by,0), created_at "
        "FROM scheduled_jobs WHERE is_active = 1 ORDER BY priority";
    std::vector<ScheduledJobRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        result.push_back(RowToJob(vals));
    });
    return result;
}

int64_t SchedulerRepository::InsertJobRun(int64_t job_id, int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO job_runs (job_id, started_at, status) VALUES (?, ?, 'queued')";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(job_id), std::to_string(now_unix)});
    return conn->LastInsertRowId();
}

void SchedulerRepository::UpdateJobRunStatus(int64_t run_id,
                                               const std::string& status,
                                               const std::string& error_message,
                                               const std::string& output_json,
                                               int64_t completed_at) {
    static const std::string sql =
        "UPDATE job_runs SET status=?, error_message=?, output_json=?, completed_at=? "
        "WHERE run_id=?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {status,
                     error_message,
                     output_json,
                     completed_at > 0 ? std::to_string(completed_at) : "",
                     std::to_string(run_id)});
}

void SchedulerRepository::SetJobRunWorker(int64_t run_id, const std::string& worker_id) {
    static const std::string sql =
        "UPDATE job_runs SET worker_id=?, status='running' WHERE run_id=?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {worker_id, std::to_string(run_id)});
}

void SchedulerRepository::InsertDependency(int64_t job_id, int64_t depends_on_job_id) {
    static const std::string sql =
        "INSERT OR IGNORE INTO job_dependencies (job_id, depends_on_job_id) VALUES (?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(job_id), std::to_string(depends_on_job_id)});
}

std::vector<DependencyEdge> SchedulerRepository::ListDependenciesFor(
    int64_t job_id) const {
    static const std::string sql =
        "SELECT job_id, depends_on_job_id FROM job_dependencies WHERE job_id = ?";
    std::vector<DependencyEdge> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(job_id)},
        [&](const auto&, const auto& vals) {
            DependencyEdge e;
            e.job_id           = vals[0].empty() ? 0 : std::stoll(vals[0]);
            e.depends_on_job_id = vals[1].empty() ? 0 : std::stoll(vals[1]);
            result.push_back(e);
        });
    return result;
}

std::vector<DependencyEdge> SchedulerRepository::ListAllDependencies() const {
    static const std::string sql =
        "SELECT job_id, depends_on_job_id FROM job_dependencies";
    std::vector<DependencyEdge> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        DependencyEdge e;
        e.job_id           = vals[0].empty() ? 0 : std::stoll(vals[0]);
        e.depends_on_job_id = vals[1].empty() ? 0 : std::stoll(vals[1]);
        result.push_back(e);
    });
    return result;
}

std::vector<int64_t> SchedulerRepository::ListPrerequisiteJobIds(
    int64_t job_id) const {
    static const std::string sql =
        "SELECT depends_on_job_id FROM job_dependencies WHERE job_id = ?";
    std::vector<int64_t> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(job_id)},
        [&](const auto&, const auto& vals) {
            if (!vals[0].empty()) result.push_back(std::stoll(vals[0]));
        });
    return result;
}

int64_t SchedulerRepository::AcquireLease(int64_t run_id,
                                            const std::string& worker_id,
                                            int64_t expires_at,
                                            int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO worker_leases (worker_id, job_run_id, acquired_at, expires_at, is_active) "
        "VALUES (?, ?, ?, ?, 1)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {worker_id, std::to_string(run_id),
                     std::to_string(now_unix), std::to_string(expires_at)});
    return conn->LastInsertRowId();
}

void SchedulerRepository::ReleaseLease(int64_t lease_id) {
    static const std::string sql =
        "UPDATE worker_leases SET is_active = 0 WHERE lease_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(lease_id)});
}

bool SchedulerRepository::HasActiveLease(int64_t run_id) const {
    static const std::string sql =
        "SELECT COUNT(*) FROM worker_leases "
        "WHERE job_run_id = ? AND is_active = 1";
    int count = 0;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(run_id)},
        [&](const auto&, const auto& vals) {
            count = vals[0].empty() ? 0 : std::stoi(vals[0]);
        });
    return count > 0;
}

void SchedulerRepository::UpdateLastRunAt(int64_t job_id, int64_t now_unix) {
    static const std::string sql =
        "UPDATE scheduled_jobs SET last_run_at = ? WHERE job_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(now_unix), std::to_string(job_id)});
}

void SchedulerRepository::UpdateNextRunAt(int64_t job_id, int64_t next_unix) {
    static const std::string sql =
        "UPDATE scheduled_jobs SET next_run_at = ? WHERE job_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(next_unix), std::to_string(job_id)});
}

std::optional<JobRunRecord> SchedulerRepository::FindJobRun(int64_t run_id) const {
    static const std::string sql =
        "SELECT run_id, job_id, COALESCE(worker_id,''), started_at, "
        "       COALESCE(completed_at,0), status, "
        "       COALESCE(error_message,''), COALESCE(output_json,'') "
        "FROM job_runs WHERE run_id = ?";
    std::optional<JobRunRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(run_id)},
        [&](const auto&, const auto& vals) { result = RowToRun(vals); });
    return result;
}

bool SchedulerRepository::HasActiveRunForJob(int64_t job_id) const {
    static const std::string sql =
        "SELECT COUNT(*) FROM job_runs "
        "WHERE job_id = ? AND status IN ('queued','running')";
    int count = 0;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(job_id)},
        [&](const auto&, const auto& vals) {
            count = vals[0].empty() ? 0 : std::stoi(vals[0]);
        });
    return count > 0;
}

bool SchedulerRepository::HasCompletedRunForJob(int64_t job_id) const {
    static const std::string sql =
        "SELECT COUNT(*) FROM job_runs "
        "WHERE job_id = ? AND status = 'completed'";
    int count = 0;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(job_id)},
        [&](const auto&, const auto& vals) {
            count = vals[0].empty() ? 0 : std::stoi(vals[0]);
        });
    return count > 0;
}

} // namespace shelterops::repositories
