#include "shelterops/repositories/ReportRepository.h"

namespace shelterops::repositories {

ReportRepository::ReportRepository(infrastructure::Database& db) : db_(db) {}

ReportDefinitionRecord ReportRepository::RowToDefinition(
    const std::vector<std::string>& vals) {
    ReportDefinitionRecord r;
    r.report_id     = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.name          = vals[1];
    r.report_type   = vals[2];
    r.description   = vals[3];
    r.filter_json   = vals[4].empty() ? "{}" : vals[4];
    r.schedule_cron = vals[5];
    r.is_active     = vals[6].empty() ? true : (vals[6] == "1");
    r.created_by    = vals[7].empty() ? 0 : std::stoll(vals[7]);
    r.created_at    = vals[8].empty() ? 0 : std::stoll(vals[8]);
    return r;
}

ReportRunRecord ReportRepository::RowToRun(const std::vector<std::string>& vals) {
    // Columns: run_id, report_id, version_label, triggered_by, trigger_type,
    //          started_at, completed_at, status, filter_json, output_path,
    //          error_message, anomaly_flags_json, row_count
    ReportRunRecord r;
    r.run_id             = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.report_id          = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.version_label      = vals[2];
    r.triggered_by       = vals[3].empty() ? 0 : std::stoll(vals[3]);
    r.trigger_type       = vals[4];
    r.started_at         = vals[5].empty() ? 0 : std::stoll(vals[5]);
    r.completed_at       = vals[6].empty() ? 0 : std::stoll(vals[6]);
    r.status             = vals[7];
    r.filter_json        = vals[8].empty() ? "{}" : vals[8];
    r.output_path        = vals[9];
    r.error_message      = vals[10];
    r.anomaly_flags_json = vals[11];
    r.row_count          = vals[12].empty() ? 0 : std::stoi(vals[12]);
    return r;
}

ReportSnapshotRecord ReportRepository::RowToSnapshot(
    const std::vector<std::string>& vals) {
    ReportSnapshotRecord r;
    r.snapshot_id   = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.run_id        = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.metric_name   = vals[2];
    r.metric_value  = vals[3].empty() ? 0.0 : std::stod(vals[3]);
    r.dimension_json = vals[4].empty() ? "{}" : vals[4];
    r.captured_at   = vals[5].empty() ? 0 : std::stoll(vals[5]);
    return r;
}

ExportJobRecord ReportRepository::RowToExportJob(const std::vector<std::string>& vals) {
    ExportJobRecord r;
    r.job_id          = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.report_run_id   = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.format          = vals[2];
    r.requested_by    = vals[3].empty() ? 0 : std::stoll(vals[3]);
    r.queued_at       = vals[4].empty() ? 0 : std::stoll(vals[4]);
    r.started_at      = vals[5].empty() ? 0 : std::stoll(vals[5]);
    r.completed_at    = vals[6].empty() ? 0 : std::stoll(vals[6]);
    r.output_path     = vals[7];
    r.status          = vals[8];
    r.max_concurrency = vals[9].empty() ? 1 : std::stoi(vals[9]);
    return r;
}

int64_t ReportRepository::InsertDefinition(const ReportDefinitionRecord& rec) {
    static const std::string sql =
        "INSERT INTO report_definitions "
        "(name, report_type, description, filter_json, schedule_cron, is_active, "
        " created_by, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {rec.name, rec.report_type, rec.description,
                     rec.filter_json, rec.schedule_cron,
                     rec.is_active ? "1" : "0",
                     std::to_string(rec.created_by),
                     std::to_string(rec.created_at)});
    return conn->LastInsertRowId();
}

std::optional<ReportDefinitionRecord> ReportRepository::FindDefinition(
    int64_t report_id) const {
    static const std::string sql =
        "SELECT report_id, name, report_type, COALESCE(description,''), "
        "       COALESCE(filter_json,'{}'), COALESCE(schedule_cron,''), is_active, "
        "       COALESCE(created_by,0), created_at "
        "FROM report_definitions WHERE report_id = ?";
    std::optional<ReportDefinitionRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(report_id)},
        [&](const auto&, const auto& vals) { result = RowToDefinition(vals); });
    return result;
}

int64_t ReportRepository::InsertRun(int64_t report_id,
                                     const std::string& version_label,
                                     const std::string& trigger_type,
                                     int64_t triggered_by,
                                     const std::string& filter_json,
                                     int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO report_runs "
        "(report_id, version_label, triggered_by, trigger_type, started_at, "
        " status, filter_json) "
        "VALUES (?, ?, ?, ?, ?, 'running', ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(report_id),
                     version_label,
                     std::to_string(triggered_by),
                     trigger_type,
                     std::to_string(now_unix),
                     filter_json});
    return conn->LastInsertRowId();
}

void ReportRepository::UpdateRunStatus(int64_t run_id,
                                        const std::string& status,
                                        const std::string& error_message,
                                        const std::string& anomaly_flags_json) {
    static const std::string sql =
        "UPDATE report_runs SET status=?, error_message=?, anomaly_flags_json=? "
        "WHERE run_id=?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {status,
                     error_message.empty() ? "" : error_message,
                     anomaly_flags_json.empty() ? "" : anomaly_flags_json,
                     std::to_string(run_id)});
}

void ReportRepository::FinalizeRun(int64_t run_id, const std::string& output_path,
                                    int row_count, int64_t now_unix) {
    static const std::string sql =
        "UPDATE report_runs SET status='completed', output_path=?, row_count=?, "
        "  completed_at=? WHERE run_id=?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {output_path,
                     std::to_string(row_count),
                     std::to_string(now_unix),
                     std::to_string(run_id)});
}

void ReportRepository::InsertSnapshot(int64_t run_id,
                                       const std::string& metric_name,
                                       double value,
                                       const std::string& dimension_json,
                                       int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO report_snapshots (run_id, metric_name, metric_value, "
        "  dimension_json, captured_at) VALUES (?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(run_id),
                     metric_name,
                     std::to_string(value),
                     dimension_json.empty() ? "{}" : dimension_json,
                     std::to_string(now_unix)});
}

std::vector<ReportRunRecord> ReportRepository::ListRunsForReport(
    int64_t report_id) const {
    static const std::string sql =
        "SELECT run_id, report_id, version_label, COALESCE(triggered_by,0), "
        "       trigger_type, started_at, COALESCE(completed_at,0), status, "
        "       COALESCE(filter_json,'{}'), COALESCE(output_path,''), "
        "       COALESCE(error_message,''), COALESCE(anomaly_flags_json,''), "
        "       COALESCE(row_count,0) "
        "FROM report_runs WHERE report_id = ? ORDER BY started_at DESC";
    std::vector<ReportRunRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(report_id)},
        [&](const auto&, const auto& vals) { result.push_back(RowToRun(vals)); });
    return result;
}

std::optional<ReportRunRecord> ReportRepository::FindRun(int64_t run_id) const {
    static const std::string sql =
        "SELECT run_id, report_id, version_label, COALESCE(triggered_by,0), "
        "       trigger_type, started_at, COALESCE(completed_at,0), status, "
        "       COALESCE(filter_json,'{}'), COALESCE(output_path,''), "
        "       COALESCE(error_message,''), COALESCE(anomaly_flags_json,''), "
        "       COALESCE(row_count,0) "
        "FROM report_runs WHERE run_id = ?";
    std::optional<ReportRunRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(run_id)},
        [&](const auto&, const auto& vals) { result = RowToRun(vals); });
    return result;
}

std::vector<ReportSnapshotRecord> ReportRepository::ListSnapshotsForRun(
    int64_t run_id) const {
    static const std::string sql =
        "SELECT snapshot_id, run_id, metric_name, metric_value, "
        "       COALESCE(dimension_json,'{}'), captured_at "
        "FROM report_snapshots WHERE run_id = ? ORDER BY snapshot_id";
    std::vector<ReportSnapshotRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(run_id)},
        [&](const auto&, const auto& vals) { result.push_back(RowToSnapshot(vals)); });
    return result;
}

int ReportRepository::CountRunsForReportOnDay(int64_t report_id,
                                               int64_t day_unix) const {
    static const std::string sql =
        "SELECT COUNT(*) FROM report_runs "
        "WHERE report_id = ? AND DATE(started_at,'unixepoch') = DATE(?,'unixepoch')";
    int count = 0;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(report_id), std::to_string(day_unix)},
        [&](const auto&, const auto& vals) {
            count = vals[0].empty() ? 0 : std::stoi(vals[0]);
        });
    return count;
}

int64_t ReportRepository::InsertExportJob(int64_t report_run_id,
                                           const std::string& format,
                                           int64_t requested_by,
                                           int max_concurrency,
                                           int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO export_jobs (report_run_id, format, requested_by, queued_at, "
        "  status, max_concurrency) "
        "VALUES (?, ?, ?, ?, 'queued', ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(report_run_id),
                     format,
                     std::to_string(requested_by),
                     std::to_string(now_unix),
                     std::to_string(max_concurrency)});
    return conn->LastInsertRowId();
}

void ReportRepository::UpdateExportJobStatus(int64_t job_id,
                                              const std::string& status,
                                              const std::string& output_path,
                                              int64_t completed_at) {
    static const std::string sql =
        "UPDATE export_jobs SET status=?, output_path=?, completed_at=? WHERE job_id=?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {status,
                     output_path,
                     completed_at > 0 ? std::to_string(completed_at) : "",
                     std::to_string(job_id)});
}

std::optional<ExportJobRecord> ReportRepository::FindExportJob(int64_t job_id) const {
    static const std::string sql =
        "SELECT job_id, report_run_id, format, requested_by, queued_at, "
        "       COALESCE(started_at,0), COALESCE(completed_at,0), "
        "       COALESCE(output_path,''), status, max_concurrency "
        "FROM export_jobs WHERE job_id = ?";
    std::optional<ExportJobRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(job_id)},
        [&](const auto&, const auto& vals) { result = RowToExportJob(vals); });
    return result;
}

std::vector<ExportJobRecord> ReportRepository::ListPendingExportJobs() const {
    static const std::string sql =
        "SELECT job_id, report_run_id, format, requested_by, queued_at, "
        "       COALESCE(started_at,0), COALESCE(completed_at,0), "
        "       COALESCE(output_path,''), status, max_concurrency "
        "FROM export_jobs WHERE status = 'queued' ORDER BY queued_at";
    std::vector<ExportJobRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        result.push_back(RowToExportJob(vals));
    });
    return result;
}

} // namespace shelterops::repositories
