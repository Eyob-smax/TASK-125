#pragma once
#include "shelterops/services/SchedulerService.h"
#include "shelterops/repositories/SchedulerRepository.h"
#include "shelterops/domain/ReportStageGraph.h"
#include "shelterops/services/UserContext.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::ui::controllers {

enum class SchedulerPanelState {
    Idle,
    Loading,
    Loaded,
    DetailView,
    Enqueuing,
    Error
};

struct JobDetailView {
    repositories::ScheduledJobRecord          job;
    std::vector<repositories::JobRunRecord>   recent_runs;
    std::vector<repositories::DependencyEdge> dependencies;
    std::string                               stage_display; // e.g. "collect→cleanse→analyze→visualize"
    std::string                               last_error;
    bool                                      has_failure = false;
};

// Controller for the Scheduler Panel window.
// Shows job list, run history, failure details, dependency stages, and queue state.
// Exposes enqueue-on-demand for Operations Manager role.
// Cross-platform: no ImGui dependency.
class SchedulerPanelController {
public:
    SchedulerPanelController(services::SchedulerService&        scheduler_svc,
                              repositories::SchedulerRepository& scheduler_repo);

    SchedulerPanelState                                    State()    const noexcept { return state_; }
    const std::vector<repositories::ScheduledJobRecord>&  Jobs()     const noexcept { return jobs_; }
    const std::optional<JobDetailView>&                   Detail()   const noexcept { return detail_; }
    const common::ErrorEnvelope&                           LastError() const noexcept { return last_error_; }
    bool                                                   IsDirty()  const noexcept { return is_dirty_; }

    // Load all active scheduled jobs.
    void Refresh(const services::UserContext& ctx, int64_t now_unix);

    // Load detailed view for a specific job (recent runs, dependencies, failure).
    void ViewJobDetail(int64_t job_id);

    // Enqueue a job run on-demand. Requires OperationsManager or Administrator.
    bool EnqueueJob(int64_t job_id, const std::string& params_json,
                    const services::UserContext& ctx, int64_t now_unix);

    // Cancel a queued or running job run.
    bool CancelRun(int64_t run_id, const services::UserContext& ctx, int64_t now_unix);

    // Create a DB snapshot and submit an on-demand LAN sync job.
    // Requires Administrator role. Returns false and sets LastError on failure.
    bool TriggerLanSync(const std::string&           peer_host,
                        int                          peer_port,
                        const std::string&           pinned_certs_path,
                        const std::string&           db_path,
                        const services::UserContext& ctx,
                        int64_t                      now_unix);

    void ClearDetail() noexcept { detail_.reset(); state_ = SchedulerPanelState::Loaded; }
    void ClearDirty() noexcept { is_dirty_ = false; }
    void ClearError() noexcept { last_error_ = {}; state_ = SchedulerPanelState::Idle; }

private:
    static std::string BuildStageDisplay();

    services::SchedulerService&        scheduler_svc_;
    repositories::SchedulerRepository& scheduler_repo_;

    SchedulerPanelState                             state_    = SchedulerPanelState::Idle;
    std::vector<repositories::ScheduledJobRecord>   jobs_;
    std::optional<JobDetailView>                    detail_;
    common::ErrorEnvelope                           last_error_;
    bool                                            is_dirty_ = false;
};

} // namespace shelterops::ui::controllers
