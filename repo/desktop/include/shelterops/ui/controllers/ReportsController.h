#pragma once
#include "shelterops/services/ReportService.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/services/UserContext.h"
#include "shelterops/common/ErrorEnvelope.h"
#include "shelterops/domain/Types.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::ui::controllers {

enum class ReportsState {
    Idle,
    Triggering,
    Running,
    Completed,
    Failed,
    ExportRequested
};

struct ReportJobStatus {
    int64_t     run_id      = 0;
    std::string status;          // "queued", "running", "completed", "failed"
    std::string output_path;
    std::string version_label;
    int64_t     triggered_at = 0;
    int64_t     completed_at = 0;
    std::string error_message;
};

// Controller for the Reports Studio window.
// Holds active-run list and export state; delegates to ReportService/ExportService.
// Cross-platform: no ImGui dependency.
class ReportsController {
public:
    ReportsController(services::ReportService&   report_svc,
                      services::ExportService&   export_svc,
                      repositories::ReportRepository& report_repo);

    ReportsState                          State()       const noexcept { return state_; }
    const std::vector<ReportJobStatus>&   ActiveRuns()  const noexcept { return active_runs_; }
    const std::vector<domain::MetricDelta>& Comparison() const noexcept { return comparison_; }
    const common::ErrorEnvelope&          LastError()   const noexcept { return last_error_; }
    bool                                  IsDirty()     const noexcept { return is_dirty_; }

    // Trigger a report pipeline run. Returns run_id, or 0 on failure.
    int64_t TriggerReport(int64_t report_id,
                          const std::string& filter_json,
                          const services::UserContext& ctx,
                          int64_t now_unix);

    // Poll status of a running job.
    void RefreshRunStatus(int64_t run_id);

    // Request a CSV or PDF export for a completed run.
    int64_t RequestExport(int64_t run_id, const std::string& format,
                          const services::UserContext& ctx, int64_t now_unix);

    // Reload the active-runs list for a given report.
    void LoadRunsForReport(int64_t report_id);
    bool CompareRuns(int64_t run_id_a, int64_t run_id_b);

    void ClearDirty() noexcept { is_dirty_ = false; }
    void ClearError() noexcept { last_error_ = {}; state_ = ReportsState::Idle; }

    repositories::ReportRepository& GetReportRepo() noexcept { return report_repo_; }

private:
    services::ReportService&        report_svc_;
    services::ExportService&        export_svc_;
    repositories::ReportRepository& report_repo_;

    ReportsState               state_       = ReportsState::Idle;
    std::vector<ReportJobStatus> active_runs_;
    std::vector<domain::MetricDelta> comparison_;
    common::ErrorEnvelope      last_error_;
    bool                       is_dirty_    = false;
};

} // namespace shelterops::ui::controllers
