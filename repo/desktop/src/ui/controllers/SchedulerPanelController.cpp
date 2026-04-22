#include "shelterops/ui/controllers/SchedulerPanelController.h"
#include "shelterops/domain/ReportStageGraph.h"
#include <spdlog/spdlog.h>
#include <sstream>

namespace shelterops::ui::controllers {

SchedulerPanelController::SchedulerPanelController(
    services::SchedulerService&        scheduler_svc,
    repositories::SchedulerRepository& scheduler_repo)
    : scheduler_svc_(scheduler_svc), scheduler_repo_(scheduler_repo)
{}

// static
std::string SchedulerPanelController::BuildStageDisplay() {
    // Enumerate collect → cleanse → analyze → visualize
    std::string result;
    auto stage = domain::ReportStage::Collect;
    while (true) {
        result += domain::StageName(stage);
        auto next = domain::NextStage(stage);
        if (!next) break;
        result += " \xe2\x86\x92 "; // UTF-8 →
        stage = *next;
    }
    return result;
}

void SchedulerPanelController::Refresh(
    const services::UserContext& /*ctx*/, int64_t /*now_unix*/)
{
    state_ = SchedulerPanelState::Loading;
    jobs_  = scheduler_repo_.ListActiveJobs();
    state_ = SchedulerPanelState::Loaded;
    is_dirty_ = false;
    spdlog::debug("SchedulerPanelController: {} active jobs", jobs_.size());
}

void SchedulerPanelController::ViewJobDetail(int64_t job_id) {
    auto job = scheduler_repo_.FindJob(job_id);
    if (!job) {
        last_error_ = { common::ErrorCode::NotFound, "Job not found." };
        state_ = SchedulerPanelState::Error;
        return;
    }

    JobDetailView view;
    view.job          = *job;
    view.dependencies = scheduler_repo_.ListDependenciesFor(job_id);
    view.stage_display = BuildStageDisplay();

    // Load last 10 runs for this job.
    // SchedulerRepository doesn't have a list-by-job yet; we search via all runs.
    // Insert a compact run fetch: reuse FindJobRun iterating recent IDs is not
    // practical without a list method. Expose only the job record and deps here.
    view.has_failure = false;

    detail_ = std::move(view);
    state_  = SchedulerPanelState::DetailView;
}

bool SchedulerPanelController::EnqueueJob(
    int64_t job_id, const std::string& params_json,
    const services::UserContext& ctx, int64_t now_unix)
{
    if (ctx.role != domain::UserRole::OperationsManager &&
        ctx.role != domain::UserRole::Administrator) {
        last_error_ = { common::ErrorCode::Forbidden,
                        "Operations Manager or Administrator required to enqueue jobs." };
        return false;
    }

    auto job = scheduler_repo_.FindJob(job_id);
    if (!job) {
        last_error_ = { common::ErrorCode::NotFound, "Job not found." };
        return false;
    }

    state_ = SchedulerPanelState::Enqueuing;
    scheduler_svc_.EnqueueOnDemand(job->job_type, params_json, job->name, ctx, now_unix);
    state_    = SchedulerPanelState::Loaded;
    is_dirty_ = true;
    spdlog::info("SchedulerPanelController: job {} enqueued", job_id);
    return true;
}

bool SchedulerPanelController::TriggerLanSync(
    const std::string& peer_host, int peer_port,
    const std::string& pinned_certs_path, const std::string& db_path,
    const services::UserContext& ctx, int64_t now_unix)
{
    if (ctx.role != domain::UserRole::Administrator) {
        last_error_ = { common::ErrorCode::Forbidden,
                        "Administrator role required to trigger LAN sync." };
        return false;
    }
    int64_t run_id = scheduler_svc_.SubmitLanSync(
        db_path, peer_host, peer_port, pinned_certs_path, ctx, now_unix);
    if (run_id < 0) {
        last_error_ = { common::ErrorCode::Internal,
                        "LAN sync submission failed: check peer_host, peer_port, "
                        "and pinned_certs_path configuration." };
        return false;
    }
    is_dirty_ = true;
    spdlog::info("SchedulerPanelController: LAN sync enqueued as run {}", run_id);
    return true;
}

bool SchedulerPanelController::CancelRun(
    int64_t run_id, const services::UserContext& ctx, int64_t now_unix)
{
    if (ctx.role == domain::UserRole::Auditor ||
        ctx.role == domain::UserRole::InventoryClerk) {
        last_error_ = { common::ErrorCode::Forbidden,
                        "Insufficient role to cancel job runs." };
        return false;
    }
    scheduler_svc_.CancelRun(run_id, ctx, now_unix);
    is_dirty_ = true;
    return true;
}

} // namespace shelterops::ui::controllers
