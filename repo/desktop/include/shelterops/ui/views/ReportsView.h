#pragma once
#if defined(_WIN32)
#include "shelterops/ui/controllers/ReportsController.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/shell/SessionContext.h"
#include <string>
#include <cstdint>

namespace shelterops::ui::views {

// Reports Studio view.
// Renders pipeline trigger, filter bar, run history table, metric snapshots,
// version comparison, anomaly surfacing, and export controls.
class ReportsView {
public:
    ReportsView(controllers::ReportsController& ctrl,
                repositories::ReportRepository& report_repo,
                shell::SessionContext&          session);

    // Renders the window. Returns false when the window was closed.
    bool Render(int64_t now_unix);

    bool IsOpen() const noexcept { return open_; }
    void Open()   noexcept { open_ = true; }
    void Close()  noexcept { open_ = false; }

private:
    void RenderFilterBar(int64_t now_unix);
    void RenderRunHistory();
    void RenderMetricSnapshots(int64_t run_id);
    void RenderAnomalyBanner(const std::string& anomaly_json);
    void RenderExportControls(int64_t run_id, int64_t now_unix);
    void RenderComparisonPanel();

    controllers::ReportsController& ctrl_;
    repositories::ReportRepository& report_repo_;
    shell::SessionContext&          session_;

    bool    open_              = true;
    int64_t selected_run_id_   = 0;
    int64_t compare_run_id_a_  = 0;
    int64_t compare_run_id_b_  = 0;
    int     selected_report_id_ = 0;
    char    filter_json_buf_[1024] = "{}";
};

} // namespace shelterops::ui::views
#endif // _WIN32
