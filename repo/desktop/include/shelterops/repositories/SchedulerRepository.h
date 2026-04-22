#pragma once
#include "shelterops/infrastructure/Database.h"
#include "shelterops/domain/Types.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::repositories {

struct ScheduledJobRecord {
    int64_t     job_id          = 0;
    std::string name;
    domain::JobType job_type    = domain::JobType::ReportGenerate;
    std::string parameters_json = "{}";
    std::string cron_expression;
    int         priority        = 5;
    int         max_concurrency = 4;
    bool        is_active       = true;
    int64_t     last_run_at     = 0;
    int64_t     next_run_at     = 0;
    int64_t     created_by      = 0;
    int64_t     created_at      = 0;    // IMMUTABLE
};

struct JobRunRecord {
    int64_t     run_id       = 0;
    int64_t     job_id       = 0;
    std::string worker_id;
    int64_t     started_at   = 0;
    int64_t     completed_at = 0;
    std::string status;
    std::string error_message;
    std::string output_json;
};

struct DependencyEdge {
    int64_t job_id          = 0;
    int64_t depends_on_job_id = 0;
};

struct WorkerLeaseRecord {
    int64_t     lease_id    = 0;
    std::string worker_id;
    int64_t     job_run_id  = 0;
    int64_t     acquired_at = 0;    // IMMUTABLE
    int64_t     expires_at  = 0;
    bool        is_active   = true;
};

class SchedulerRepository {
public:
    explicit SchedulerRepository(infrastructure::Database& db);

    // Insert a scheduled job record. Returns job_id.
    int64_t InsertScheduledJob(const ScheduledJobRecord& rec);

    std::optional<ScheduledJobRecord> FindJob(int64_t job_id) const;
    std::vector<ScheduledJobRecord> ListActiveJobs() const;

    // Insert a job_runs row in 'queued' state. Returns run_id.
    int64_t InsertJobRun(int64_t job_id, int64_t now_unix);

    void UpdateJobRunStatus(int64_t run_id, const std::string& status,
                            const std::string& error_message = "",
                            const std::string& output_json = "",
                            int64_t completed_at = 0);

    void SetJobRunWorker(int64_t run_id, const std::string& worker_id);

    // Insert a dependency edge (no cycle check — caller must check first).
    void InsertDependency(int64_t job_id, int64_t depends_on_job_id);

    // Returns all edges where job_id matches (direct dependencies).
    std::vector<DependencyEdge> ListDependenciesFor(int64_t job_id) const;

    // Returns all dependency edges (for SchedulerGraph cycle detection).
    std::vector<DependencyEdge> ListAllDependencies() const;

    // Returns job_ids that job_id depends on (prerequisites).
    std::vector<int64_t> ListPrerequisiteJobIds(int64_t job_id) const;

    // Worker lease management.
    int64_t AcquireLease(int64_t run_id, const std::string& worker_id,
                         int64_t expires_at, int64_t now_unix);
    void ReleaseLease(int64_t lease_id);
    bool HasActiveLease(int64_t run_id) const;

    void UpdateLastRunAt(int64_t job_id, int64_t now_unix);
    void UpdateNextRunAt(int64_t job_id, int64_t next_unix);

    std::optional<JobRunRecord> FindJobRun(int64_t run_id) const;

    // True when job has any queued/running run.
    bool HasActiveRunForJob(int64_t job_id) const;

    // True when job has at least one completed run.
    bool HasCompletedRunForJob(int64_t job_id) const;

private:
    static ScheduledJobRecord RowToJob(const std::vector<std::string>& vals);
    static JobRunRecord       RowToRun(const std::vector<std::string>& vals);
    static domain::JobType    ParseJobType(const std::string& s);
    static std::string        JobTypeToString(domain::JobType t);

    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
