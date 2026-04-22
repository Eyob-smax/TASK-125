#if defined(_WIN32)
#include "shelterops/ui/views/SchedulerPanelView.h"
#include "shelterops/shell/ErrorDisplay.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

namespace shelterops::ui::views {

SchedulerPanelView::SchedulerPanelView(
    controllers::SchedulerPanelController& ctrl,
    shell::SessionContext&                 session)
    : ctrl_(ctrl), session_(session)
{}

bool SchedulerPanelView::Render(int64_t now_unix) {
    if (!open_) return false;

    ImGui::SetNextWindowSize({900.0f, 520.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scheduler Panel", &open_)) {
        ImGui::End();
        return open_;
    }

    auto state = ctrl_.State();
    if (state == controllers::SchedulerPanelState::Error)
        shell::ErrorDisplay::RenderBanner(ctrl_.LastError().message);

    // Left pane: job list
    ImGui::BeginChild("JobListPane", {320.0f, -1.0f}, true);
    RenderJobListPanel(now_unix);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right pane: job detail / run history
    ImGui::BeginChild("JobDetailPane", {-1.0f, -1.0f}, true);
    RenderDetailPanel(now_unix);
    ImGui::EndChild();

    ImGui::End();
    return open_;
}

void SchedulerPanelView::RenderJobListPanel(int64_t now_unix) {
    ImGui::Text("Scheduled Jobs");
    if (ImGui::Button("Refresh##sch")) ctrl_.Refresh(session_.Get(), now_unix);
    ImGui::Separator();

    const auto& jobs = ctrl_.Jobs();
    if (ImGui::BeginTable("JobTbl", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
            ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Active");
        ImGui::TableHeadersRow();
        for (const auto& j : jobs) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char lbl[32];
            std::snprintf(lbl, sizeof(lbl), "%lld##j",
                          static_cast<long long>(j.job_id));
            if (ImGui::Selectable(lbl, false,
                    ImGuiSelectableFlags_SpanAllColumns))
                ctrl_.ViewJobDetail(j.job_id);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(j.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", j.is_active ? "Yes" : "No");
        }
        ImGui::EndTable();
    }
}

void SchedulerPanelView::RenderDetailPanel(int64_t now_unix) {
    const auto& detail = ctrl_.Detail();
    if (!detail) {
        ImGui::TextDisabled("Select a job to view details.");
        return;
    }

    const auto& d = *detail;
    ImGui::Text("Job: %s  (ID %lld)",
                d.job.name.c_str(),
                static_cast<long long>(d.job.job_id));
    ImGui::Text("Stage pipeline: %s", d.stage_display.c_str());
    ImGui::Separator();

    if (d.has_failure) {
        ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.3f, 0.3f, 1.0f});
        ImGui::TextWrapped("Last error: %s", d.last_error.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Text("Dependencies:");
    for (const auto& dep : d.dependencies)
        ImGui::BulletText("depends on job %lld",
                          static_cast<long long>(dep.depends_on_job_id));
    if (d.dependencies.empty())
        ImGui::TextDisabled("  (none)");

    ImGui::Separator();
    ImGui::Text("Recent Runs:");
    if (d.recent_runs.empty()) {
        ImGui::TextDisabled("  No run history.");
    } else {
        for (const auto& r : d.recent_runs) {
            ImGui::BulletText("Run %lld  status=%s",
                              static_cast<long long>(r.run_id),
                              r.status.c_str());
            if (!r.error_message.empty())
                ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f},
                                   "  Error: %s", r.error_message.c_str());
        }
    }

    ImGui::Separator();
    bool can_enqueue = (session_.CurrentRole() == domain::UserRole::OperationsManager ||
                        session_.CurrentRole() == domain::UserRole::Administrator);
    if (!can_enqueue) ImGui::BeginDisabled();
    if (ImGui::Button("Enqueue Now##sch"))
        ctrl_.EnqueueJob(d.job.job_id, d.job.parameters_json,
                         session_.Get(), now_unix);
    if (!can_enqueue) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Close Detail##sch")) ctrl_.ClearDetail();
}

} // namespace shelterops::ui::views
#endif // _WIN32
