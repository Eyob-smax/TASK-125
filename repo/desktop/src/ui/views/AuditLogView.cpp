#if defined(_WIN32)
#include "shelterops/ui/views/AuditLogView.h"
#include "shelterops/shell/ErrorDisplay.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

namespace shelterops::ui::views {

AuditLogView::AuditLogView(
    controllers::AuditLogController& ctrl,
    shell::ClipboardHelper&          clipboard,
    shell::SessionContext&            session)
    : ctrl_(ctrl), clipboard_(clipboard), session_(session)
{}

bool AuditLogView::Render(int64_t now_unix) {
    if (!open_) return false;

    ImGui::SetNextWindowSize({900.0f, 560.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audit Log", &open_)) {
        ImGui::End();
        return open_;
    }

    auto state = ctrl_.State();
    if (state == controllers::AuditLogState::Error)
        shell::ErrorDisplay::RenderBanner(ctrl_.LastError().message);

    RenderFilterBar(now_unix);
    ImGui::Separator();
    RenderEventTable();
    ImGui::Separator();
    RenderExportBar(now_unix);

    ImGui::End();
    return open_;
}

void AuditLogView::RenderFilterBar(int64_t now_unix) {
    auto& f = ctrl_.Filter();

    ImGui::Text("Filter:");
    ImGui::SameLine();

    static char et_buf[64] = "";
    if (ImGui::IsWindowAppearing()) std::strncpy(et_buf, f.event_type.c_str(), sizeof(et_buf)-1);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputText("Event Type##alf", et_buf, sizeof(et_buf)))
        f.event_type = et_buf;

    ImGui::SameLine();
    static char ent_buf[64] = "";
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputText("Entity##alf", ent_buf, sizeof(ent_buf)))
        f.entity_type = ent_buf;

    ImGui::SameLine();
    int lim = f.limit;
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::InputInt("Limit##alf", &lim, 0))
        f.limit = lim;

    ImGui::SameLine();
    if (ImGui::Button("Search##alf"))
        ctrl_.Refresh(session_.Get(), now_unix);

    if (ctrl_.IsDirty())
        ImGui::SameLine(), ImGui::TextColored({1.0f, 0.8f, 0.0f, 1.0f}, "●");
}

void AuditLogView::RenderEventTable() {
    const auto& events = ctrl_.Events();
    ImGui::Text("%zu event(s)", events.size());

    if (ImGui::BeginTable("AuditTbl", 7,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Actor");
        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("Event");
        ImGui::TableSetupColumn("Entity");
        ImGui::TableSetupColumn("Entity ID");
        ImGui::TableSetupColumn("Description");
        ImGui::TableHeadersRow();

        bool is_auditor = (session_.CurrentRole() == domain::UserRole::Auditor);

        for (const auto& e : events) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%lld", static_cast<long long>(e.occurred_at));
            ImGui::TableSetColumnIndex(1);
            if (is_auditor)
                ImGui::TextDisabled("[masked]");
            else
                ImGui::Text("%lld", static_cast<long long>(e.actor_user_id));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e.actor_role.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(e.event_type.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(e.entity_type.c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%lld", static_cast<long long>(e.entity_id));
            ImGui::TableSetColumnIndex(6);
            if (is_auditor)
                ImGui::TextDisabled("[masked]");
            else
                ImGui::TextUnformatted(e.description.c_str());
        }
        ImGui::EndTable();
    }
}

void AuditLogView::RenderExportBar(int64_t /*now_unix*/) {
    if (session_.CurrentRole() == domain::UserRole::Auditor ||
        session_.CurrentRole() == domain::UserRole::Administrator) {
        if (ImGui::Button("Export CSV (masked)##alf")) {
            std::string csv = ctrl_.ExportCsv(session_.Get());
            if (!csv.empty())
                clipboard_.SetText(csv);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(copies to clipboard)");
    }
}

} // namespace shelterops::ui::views
#endif // _WIN32
