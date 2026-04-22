#include "shelterops/ui/controllers/ReportsController.h"
#include <spdlog/spdlog.h>

namespace shelterops::ui::controllers {

ReportsController::ReportsController(
    services::ReportService&        report_svc,
    services::ExportService&        export_svc,
    repositories::ReportRepository& report_repo)
    : report_svc_(report_svc), export_svc_(export_svc), report_repo_(report_repo)
{}

int64_t ReportsController::TriggerReport(
    int64_t report_id,
    const std::string& filter_json,
    const services::UserContext& ctx,
    int64_t now_unix)
{
    state_ = ReportsState::Triggering;
    last_error_ = {};

    int64_t run_id = report_svc_.RunPipeline(report_id, filter_json,
                                              "manual", ctx, now_unix);
    if (run_id <= 0) {
        last_error_ = { common::ErrorCode::Internal,
                        "Report pipeline returned no run id." };
        state_ = ReportsState::Failed;
        return 0;
    }

    ReportJobStatus status;
    status.run_id       = run_id;
    status.status       = "running";
    status.triggered_at = now_unix;
    active_runs_.push_back(status);
    state_    = ReportsState::Running;
    is_dirty_ = true;
    spdlog::info("ReportsController: triggered report {} run_id={}", report_id, run_id);
    return run_id;
}

void ReportsController::RefreshRunStatus(int64_t run_id) {
    auto rec = report_repo_.FindRun(run_id);
    if (!rec) return;

    for (auto& s : active_runs_) {
        if (s.run_id != run_id) continue;
        s.status       = rec->status;
        s.output_path  = rec->output_path;
        s.completed_at = rec->completed_at;
        s.error_message = rec->error_message;
        s.version_label = rec->version_label;
        break;
    }

    if (rec->status == "completed")      state_ = ReportsState::Completed;
    else if (rec->status == "failed")    state_ = ReportsState::Failed;
    else                                  state_ = ReportsState::Running;
}

int64_t ReportsController::RequestExport(
    int64_t run_id, const std::string& format,
    const services::UserContext& ctx, int64_t now_unix)
{
    auto result = export_svc_.RequestExport(run_id, format, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&result)) {
        last_error_ = *err;
        return 0;
    }
    state_    = ReportsState::ExportRequested;
    is_dirty_ = true;
    return std::get<int64_t>(result);
}

void ReportsController::LoadRunsForReport(int64_t report_id) {
    active_runs_.clear();
    auto runs = report_repo_.ListRunsForReport(report_id);
    for (const auto& r : runs) {
        ReportJobStatus s;
        s.run_id        = r.run_id;
        s.status        = r.status;
        s.output_path   = r.output_path;
        s.version_label = r.version_label;
        s.triggered_at  = r.started_at;
        s.completed_at  = r.completed_at;
        s.error_message = r.error_message;
        active_runs_.push_back(s);
    }
}

bool ReportsController::CompareRuns(int64_t run_id_a, int64_t run_id_b) {
    comparison_.clear();
    last_error_ = {};

    if (run_id_a <= 0 || run_id_b <= 0 || run_id_a == run_id_b) {
        last_error_ = {common::ErrorCode::InvalidInput,
                       "Select two different report runs to compare."};
        state_ = ReportsState::Failed;
        return false;
    }

    comparison_ = report_svc_.CompareVersions(run_id_a, run_id_b);
    if (comparison_.empty()) {
        last_error_ = {common::ErrorCode::NotFound,
                       "No comparable metrics were found for the selected runs."};
        state_ = ReportsState::Failed;
        return false;
    }

    state_ = ReportsState::Completed;
    is_dirty_ = true;
    return true;
}

} // namespace shelterops::ui::controllers
