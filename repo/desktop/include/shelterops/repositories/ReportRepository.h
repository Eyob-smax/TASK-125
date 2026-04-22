#pragma once
#include "shelterops/infrastructure/Database.h"
#include "shelterops/domain/Types.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::repositories {

struct ReportDefinitionRecord {
    int64_t     report_id     = 0;
    std::string name;
    std::string report_type;
    std::string description;
    std::string filter_json   = "{}";
    std::string schedule_cron;
    bool        is_active     = true;
    int64_t     created_by    = 0;
    int64_t     created_at    = 0;    // IMMUTABLE
};

struct ReportRunRecord {
    int64_t     run_id             = 0;
    int64_t     report_id          = 0;
    std::string version_label;
    int64_t     triggered_by       = 0;
    std::string trigger_type;
    int64_t     started_at         = 0;
    int64_t     completed_at       = 0;
    std::string status;
    std::string filter_json        = "{}";
    std::string output_path;
    std::string error_message;
    std::string anomaly_flags_json;
    int         row_count          = 0;
};

struct ReportSnapshotRecord {
    int64_t     snapshot_id    = 0;
    int64_t     run_id         = 0;
    std::string metric_name;
    double      metric_value   = 0.0;
    std::string dimension_json = "{}";
    int64_t     captured_at    = 0;    // IMMUTABLE
};

struct ExportJobRecord {
    int64_t     job_id          = 0;
    int64_t     report_run_id   = 0;
    std::string format;
    int64_t     requested_by    = 0;
    int64_t     queued_at       = 0;
    int64_t     started_at      = 0;
    int64_t     completed_at    = 0;
    std::string output_path;
    std::string status;
    int         max_concurrency = 1;
};

class ReportRepository {
public:
    explicit ReportRepository(infrastructure::Database& db);

    int64_t InsertDefinition(const ReportDefinitionRecord& rec);
    std::optional<ReportDefinitionRecord> FindDefinition(int64_t report_id) const;

    // Insert a run record in 'queued' status. Returns run_id.
    int64_t InsertRun(int64_t report_id,
                      const std::string& version_label,
                      const std::string& trigger_type,
                      int64_t triggered_by,
                      const std::string& filter_json,
                      int64_t now_unix);

    void UpdateRunStatus(int64_t run_id, const std::string& status,
                         const std::string& error_message = "",
                         const std::string& anomaly_flags_json = "");

    void FinalizeRun(int64_t run_id, const std::string& output_path,
                     int row_count, int64_t now_unix);

    void InsertSnapshot(int64_t run_id, const std::string& metric_name,
                        double value, const std::string& dimension_json,
                        int64_t now_unix);

    std::vector<ReportRunRecord> ListRunsForReport(int64_t report_id) const;
    std::optional<ReportRunRecord> FindRun(int64_t run_id) const;

    std::vector<ReportSnapshotRecord> ListSnapshotsForRun(int64_t run_id) const;

    // Returns the count of prior runs for the same report_id on the same calendar day
    // (used to generate the version_label sequence number).
    int CountRunsForReportOnDay(int64_t report_id, int64_t day_unix) const;

    // Export job management.
    int64_t InsertExportJob(int64_t report_run_id, const std::string& format,
                             int64_t requested_by, int max_concurrency, int64_t now_unix);

    void UpdateExportJobStatus(int64_t job_id, const std::string& status,
                                const std::string& output_path = "",
                                int64_t completed_at = 0);

    std::optional<ExportJobRecord> FindExportJob(int64_t job_id) const;
    std::vector<ExportJobRecord> ListPendingExportJobs() const;

private:
    static ReportDefinitionRecord RowToDefinition(const std::vector<std::string>& vals);
    static ReportRunRecord        RowToRun(const std::vector<std::string>& vals);
    static ReportSnapshotRecord   RowToSnapshot(const std::vector<std::string>& vals);
    static ExportJobRecord        RowToExportJob(const std::vector<std::string>& vals);

    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
