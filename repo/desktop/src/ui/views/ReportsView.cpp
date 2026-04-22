#if defined(_WIN32)
#include "shelterops/ui/views/ReportsView.h"
#include "shelterops/shell/ErrorDisplay.h"
#include <imgui.h>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace shelterops::ui::views {

ReportsView::ReportsView(
    controllers::ReportsController& ctrl,
    repositories::ReportRepository& report_repo,
    shell::SessionContext&          session)
    : ctrl_(ctrl), report_repo_(report_repo), session_(session)
{}

bool ReportsView::Render(int64_t now_unix) {
    if (!open_) return false;

    ImGui::SetNextWindowSize({960.0f, 620.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Reports Studio", &open_)) {
        ImGui::End();
        return open_;
    }

    auto state = ctrl_.State();

    // Toolbar
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Report ID##rpt", &selected_report_id_);
    ImGui::SameLine();
    RenderFilterBar(now_unix);
    ImGui::SameLine();
    if (ImGui::Button("Run Report")) {
        int64_t run_id = ctrl_.TriggerReport(
            static_cast<int64_t>(selected_report_id_),
            filter_json_buf_,
            session_.Get(),
            now_unix);
        if (run_id > 0) selected_run_id_ = run_id;
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh History"))
        ctrl_.LoadRunsForReport(selected_report_id_);

    ImGui::Separator();

    if (state == controllers::ReportsState::Failed) {
        shell::ErrorDisplay::RenderBanner(ctrl_.LastError().message);
    }

    // Left: run history
    ImGui::BeginChild("RunList", {280.0f, -1.0f}, true);
    RenderRunHistory();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: run detail / anomaly / export
    ImGui::BeginChild("RunDetail", {-1.0f, -1.0f}, true);
    if (selected_run_id_ > 0) {
        RenderMetricSnapshots(selected_run_id_);
        ImGui::Separator();

        // Show anomaly banner if the selected run has anomaly flags
        for (const auto& r : ctrl_.ActiveRuns()) {
            if (r.run_id == selected_run_id_ &&
                !r.error_message.empty()) {
                RenderAnomalyBanner(r.error_message);
            }
        }

        ImGui::Separator();
        RenderExportControls(selected_run_id_, now_unix);
        ImGui::Separator();
        RenderComparisonPanel();
    } else {
        ImGui::TextDisabled("Select a run from the left panel.");
    }
    ImGui::EndChild();

    ImGui::End();
    return open_;
}

void ReportsView::RenderFilterBar(int64_t /*now_unix*/) {
    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText("Filter JSON##rptflt", filter_json_buf_,
                     sizeof(filter_json_buf_));
}

void ReportsView::RenderRunHistory() {
    ImGui::Text("Run History");
    ImGui::Separator();
    if (ImGui::BeginTable("RunHistTbl", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
            ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Label");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Rows");
        ImGui::TableHeadersRow();

        for (const auto& r : ctrl_.ActiveRuns()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool sel = (selected_run_id_ == r.run_id);
            char lbl[64];
            std::snprintf(lbl, sizeof(lbl), "##run%lld",
                          static_cast<long long>(r.run_id));
            if (ImGui::Selectable(
                    r.version_label.empty()
                        ? std::to_string(r.run_id).c_str()
                        : r.version_label.c_str(),
                    sel,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                selected_run_id_ = r.run_id;
                ctrl_.RefreshRunStatus(r.run_id);
            }
            ImGui::TableSetColumnIndex(1);
            if (r.status == "failed")
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "%s", r.status.c_str());
            else if (r.status == "completed")
                ImGui::TextColored({0.3f, 1.0f, 0.3f, 1.0f}, "%s", r.status.c_str());
            else
                ImGui::TextUnformatted(r.status.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("—");
        }
        ImGui::EndTable();
    }
}

void ReportsView::RenderMetricSnapshots(int64_t run_id) {
    ImGui::Text("Metric Snapshots — run %lld", static_cast<long long>(run_id));
    auto snapshots = report_repo_.ListSnapshotsForRun(run_id);
    if (snapshots.empty()) {
        ImGui::TextDisabled("No snapshots for this run.");
        return;
    }
    if (ImGui::BeginTable("SnapTbl", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Dimensions");
        ImGui::TableHeadersRow();
        for (const auto& s : snapshots) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(s.metric_name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.4f", s.metric_value);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(s.dimension_json.c_str());
        }
        ImGui::EndTable();
    }
}

void ReportsView::RenderAnomalyBanner(const std::string& anomaly_json) {
    ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.8f, 0.0f, 1.0f});
    ImGui::TextWrapped("Anomaly flags: %s", anomaly_json.c_str());
    ImGui::PopStyleColor();
}

void ReportsView::RenderExportControls(int64_t run_id, int64_t now_unix) {
    ImGui::Text("Export");
    if (ImGui::Button("Export CSV##rpt"))
        ctrl_.RequestExport(run_id, "csv", session_.Get(), now_unix);
    ImGui::SameLine();
    if (ImGui::Button("Export PDF##rpt"))
        ctrl_.RequestExport(run_id, "pdf", session_.Get(), now_unix);
}

void ReportsView::RenderComparisonPanel() {
    ImGui::Text("Version Comparison");
    ImGui::SetNextItemWidth(100.0f);
    int ia = static_cast<int>(compare_run_id_a_);
    ImGui::InputInt("Run A##cmp", &ia);
    compare_run_id_a_ = static_cast<int64_t>(ia);
    ImGui::SameLine();
    int ib = static_cast<int>(compare_run_id_b_);
    ImGui::InputInt("Run B##cmp", &ib);
    compare_run_id_b_ = static_cast<int64_t>(ib);
    ImGui::SameLine();
    if (ImGui::Button("Compare##rpt")) {
        ctrl_.CompareRuns(compare_run_id_a_, compare_run_id_b_);
    }

    const auto& deltas = ctrl_.Comparison();
    if (deltas.empty()) {
        ImGui::TextDisabled("Select two completed runs to view metric deltas.");
        return;
    }

    if (ImGui::BeginTable("CmpTbl", 5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Metric");
        ImGui::TableSetupColumn("Before");
        ImGui::TableSetupColumn("After");
        ImGui::TableSetupColumn("Delta");
        ImGui::TableSetupColumn("Delta %");
        ImGui::TableHeadersRow();
        for (const auto& delta : deltas) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(delta.metric_name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.4f", delta.value_before);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.4f", delta.value_after);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.4f", delta.delta_absolute);
            ImGui::TableSetColumnIndex(4);
            if (std::isnan(delta.delta_pct)) {
                ImGui::TextDisabled("n/a");
            } else {
                ImGui::Text("%.2f%%", delta.delta_pct);
            }
        }
        ImGui::EndTable();
    }
}

} // namespace shelterops::ui::views
#endif // _WIN32
