#pragma once
#include "shelterops/repositories/SchedulerRepository.h"
#include "shelterops/domain/SchedulerGraph.h"
#include "shelterops/workers/JobQueue.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"    // for UserContext
#include "shelterops/common/ErrorEnvelope.h"
#include <vector>
#include <variant>
#include <string>
#include <cstdint>

namespace shelterops::services {

class SchedulerService {
public:
    SchedulerService(repositories::SchedulerRepository& scheduler,
                     AuditService&                      audit,
                     workers::JobQueue*                 job_queue = nullptr);

    // Register a dependency edge. Uses SchedulerGraph::HasCircularDependency.
    // On cycle: emits CIRCULAR_JOB_DEPENDENCY audit and returns error.
    std::variant<std::monostate, common::ErrorEnvelope>
    RegisterDependency(int64_t job_id, int64_t depends_on_id,
                       const UserContext& user_ctx, int64_t now_unix);

    // Insert scheduled_job + queued job_run. Returns run_id.
    int64_t EnqueueOnDemand(domain::JobType job_type,
                             const std::string& params_json,
                             const std::string& job_name,
                             const UserContext& user_ctx,
                             int64_t now_unix);

    // Returns job_ids whose prerequisites are all completed and are ready to run.
    std::vector<int64_t> ListReadyJobIds(int64_t now_unix) const;

    // Dispatches due scheduled jobs into job_runs + JobQueue.
    // Returns number of runs submitted in this pass.
    int DispatchDueJobs(int64_t now_unix);

    void CancelRun(int64_t run_id, const UserContext& user_ctx, int64_t now_unix);

    // Create a WAL-safe DB snapshot, build params_json from the supplied config
    // fields, and enqueue a LanSync job. Returns the run_id, or -1 on error.
    // Validates that peer_host is non-empty and peer_port is positive before
    // creating the snapshot so no disk work is done when config is incomplete.
    int64_t SubmitLanSync(const std::string& db_path,
                          const std::string& peer_host,
                          int                peer_port,
                          const std::string& pinned_certs_path,
                          const UserContext& user_ctx,
                          int64_t            now_unix);

private:
    repositories::SchedulerRepository& scheduler_;
    AuditService&                      audit_;
    workers::JobQueue*                 job_queue_ = nullptr;
};

} // namespace shelterops::services
