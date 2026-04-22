#include "shelterops/services/SchedulerService.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace shelterops::services {

SchedulerService::SchedulerService(repositories::SchedulerRepository& scheduler,
                                    AuditService&                      audit,
                                    workers::JobQueue*                 job_queue)
    : scheduler_(scheduler), audit_(audit), job_queue_(job_queue) {
    if (job_queue_ != nullptr) {
        job_queue_->SetLifecycleCallbacks(
            [this](const workers::JobDescriptor& desc,
                   const std::string& worker_id,
                   int64_t /*started_at_unix*/) {
                scheduler_.SetJobRunWorker(desc.run_id, worker_id);
            },
            [this](const workers::JobDescriptor& desc,
                   const workers::JobOutcome& outcome,
                   int64_t completed_at_unix) {
                scheduler_.UpdateJobRunStatus(
                    desc.run_id,
                    outcome.success ? "completed" : "failed",
                    outcome.error_message,
                    outcome.output_json,
                    completed_at_unix);
            });
    }
}

std::variant<std::monostate, common::ErrorEnvelope>
SchedulerService::RegisterDependency(int64_t job_id, int64_t depends_on_id,
                                      const UserContext& user_ctx,
                                      int64_t now_unix) {
    (void)user_ctx;
    // Load all existing edges.
    auto edges_raw = scheduler_.ListAllDependencies();
    std::vector<domain::SchedulerEdge> edges;
    for (const auto& e : edges_raw) {
        edges.push_back({e.job_id, e.depends_on_job_id});
    }

    // Check for cycle before inserting.
    if (domain::HasCircularDependency(edges, job_id, depends_on_id)) {
        audit_.RecordSystemEvent("CIRCULAR_JOB_DEPENDENCY",
            "Circular dependency rejected: job " + std::to_string(job_id) +
            " → " + std::to_string(depends_on_id),
            now_unix);
        return common::ErrorEnvelope{
            common::ErrorCode::InvalidInput,
            "Adding this dependency would create a circular chain"
        };
    }

    scheduler_.InsertDependency(job_id, depends_on_id);
    return std::monostate{};
}

int64_t SchedulerService::EnqueueOnDemand(domain::JobType job_type,
                                           const std::string& params_json,
                                           const std::string& job_name,
                                           const UserContext& user_ctx,
                                           int64_t now_unix) {
    repositories::ScheduledJobRecord job_rec;
    job_rec.name            = job_name;
    job_rec.job_type        = job_type;
    job_rec.parameters_json = params_json;
    job_rec.priority        = 5;
    job_rec.max_concurrency = 4;
    job_rec.is_active       = true;
    job_rec.created_by      = user_ctx.user_id;
    job_rec.created_at      = now_unix;

    int64_t job_id = scheduler_.InsertScheduledJob(job_rec);
    int64_t run_id = scheduler_.InsertJobRun(job_id, now_unix);

    if (job_queue_ != nullptr) {
        workers::JobDescriptor desc;
        desc.run_id = run_id;
        desc.job_id = job_id;
        desc.job_type = job_type;
        desc.parameters_json = params_json;
        desc.priority = job_rec.priority;
        desc.max_concurrency = job_rec.max_concurrency;
        desc.submitted_at = now_unix;
        desc.submitted_by = user_ctx.user_id;
        job_queue_->Submit(desc);
    }

    audit_.RecordSystemEvent("JOB_ENQUEUED",
        "Job " + std::to_string(job_id) +
        " (" + job_name + ") queued as run " + std::to_string(run_id),
        now_unix);

    return run_id;
}

std::vector<int64_t> SchedulerService::ListReadyJobIds(int64_t now_unix) const {
    (void)now_unix;
    auto jobs = scheduler_.ListActiveJobs();

    // Build job statuses from recent runs.
    std::unordered_map<int64_t, std::string> job_statuses;
    for (const auto& job : jobs) {
        job_statuses[job.job_id] = "queued";
    }

    auto edges_raw = scheduler_.ListAllDependencies();
    std::vector<domain::SchedulerEdge> edges;
    for (const auto& e : edges_raw) {
        edges.push_back({e.job_id, e.depends_on_job_id});
    }

    return domain::NextReadyJobs(edges, job_statuses);
}

int SchedulerService::DispatchDueJobs(int64_t now_unix) {
    if (job_queue_ == nullptr) return 0;

    int dispatched = 0;
    auto jobs = scheduler_.ListActiveJobs();
    for (const auto& job : jobs) {
        if (job.next_run_at <= 0 || job.next_run_at > now_unix) {
            continue;
        }
        if (scheduler_.HasActiveRunForJob(job.job_id)) {
            continue;
        }

        bool deps_ready = true;
        auto prereqs = scheduler_.ListPrerequisiteJobIds(job.job_id);
        for (int64_t dep_job_id : prereqs) {
            if (!scheduler_.HasCompletedRunForJob(dep_job_id)) {
                deps_ready = false;
                break;
            }
        }
        if (!deps_ready) {
            continue;
        }

        int64_t run_id = scheduler_.InsertJobRun(job.job_id, now_unix);
        workers::JobDescriptor desc;
        desc.run_id = run_id;
        desc.job_id = job.job_id;
        desc.job_type = job.job_type;
        desc.parameters_json = job.parameters_json;
        desc.priority = job.priority;
        desc.max_concurrency = job.max_concurrency;
        desc.submitted_at = now_unix;
        desc.submitted_by = job.created_by;
        job_queue_->Submit(desc);

        scheduler_.UpdateLastRunAt(job.job_id, now_unix);
        if (!job.cron_expression.empty()) {
            // Conservative fallback cadence until cron parser is introduced.
            scheduler_.UpdateNextRunAt(job.job_id, now_unix + 60);
        }

        audit_.RecordSystemEvent("JOB_DISPATCHED",
            "Job " + std::to_string(job.job_id) +
            " dispatched as run " + std::to_string(run_id),
            now_unix);
        ++dispatched;
    }
    return dispatched;
}

int64_t SchedulerService::SubmitLanSync(const std::string& db_path,
                                         const std::string& peer_host,
                                         int                peer_port,
                                         const std::string& pinned_certs_path,
                                         const UserContext& user_ctx,
                                         int64_t            now_unix) {
    if (peer_host.empty() || peer_port <= 0 || pinned_certs_path.empty()) {
        spdlog::warn("SchedulerService::SubmitLanSync: incomplete config "
                     "(peer_host={}, peer_port={}, pinned_certs_path={})",
                     peer_host, peer_port, pinned_certs_path);
        return -1;
    }

    namespace fs = std::filesystem;
    const std::string snap_dir = "lan_sync_snapshots";
    try {
        fs::create_directories(snap_dir);
    } catch (const std::exception& ex) {
        spdlog::error("SchedulerService::SubmitLanSync: cannot create snapshot dir: {}",
                      ex.what());
        return -1;
    }

    const std::string source_path =
        snap_dir + "/snap_" + std::to_string(now_unix) + ".db";
    try {
        fs::copy_file(db_path, source_path, fs::copy_options::overwrite_existing);
    } catch (const std::exception& ex) {
        spdlog::error("SchedulerService::SubmitLanSync: snapshot copy failed "
                      "(src={}): {}", db_path, ex.what());
        return -1;
    }

    nlohmann::json params;
    params["source_path"]       = source_path;
    params["peer_host"]         = peer_host;
    params["peer_port"]         = peer_port;
    params["pinned_certs_path"] = pinned_certs_path;

    return EnqueueOnDemand(domain::JobType::LanSync, params.dump(),
                           "lan_sync", user_ctx, now_unix);
}

void SchedulerService::CancelRun(int64_t run_id, const UserContext& user_ctx,
                                  int64_t now_unix) {
    scheduler_.UpdateJobRunStatus(run_id, "cancelled", "Cancelled by user", "", now_unix);
    audit_.RecordSystemEvent("JOB_CANCELLED",
        "Run " + std::to_string(run_id) +
        " cancelled by user " + std::to_string(user_ctx.user_id),
        now_unix);
}

} // namespace shelterops::services
