#if defined(_WIN32)
#include "shelterops/ui/views/AlertsPanelView.h"
#include "shelterops/shell/ErrorDisplay.h"
#include <imgui.h>
#include <cstdio>

namespace shelterops::ui::views {

AlertsPanelView::AlertsPanelView(
    controllers::AlertsPanelController& ctrl,
    shell::SessionContext&              session)
    : ctrl_(ctrl), session_(session)
{}

bool AlertsPanelView::Render(int64_t now_unix) {
    if (!open_) return false;

    ImGui::SetNextWindowSize({640.0f, 440.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Alerts Panel", &open_)) {
        ImGui::End();
        return open_;
    }

    auto state = ctrl_.State();
    if (state == controllers::AlertsPanelState::Error)
        shell::ErrorDisplay::RenderBanner(ctrl_.LastError().message);

    // Badge count in title area
    int total = ctrl_.TotalUnacknowledged();
    if (total > 0)
        ImGui::TextColored({1.0f, 0.4f, 0.1f, 1.0f},
                           "⚠ %d unacknowledged alert(s)", total);

    if (ImGui::Button("Refresh Alerts##alp")) {
        domain::AlertThreshold th;
        ctrl_.Refresh(th, now_unix);
    }

    ImGui::Separator();

    const auto& alerts = ctrl_.Alerts();
    if (alerts.empty()) {
        ImGui::TextColored({0.3f, 1.0f, 0.3f, 1.0f}, "✓ No active alerts.");
    } else {
        bool can_ack = (session_.CurrentRole() != domain::UserRole::Auditor);

        if (ImGui::BeginTable("AlertTbl", 5,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("Item ID");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Triggered At");
            ImGui::TableSetupColumn("Action");
            ImGui::TableHeadersRow();

            for (const auto& a : alerts) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%lld", static_cast<long long>(a.alert_id));
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%lld", static_cast<long long>(a.item_id));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(a.alert_type.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%lld", static_cast<long long>(a.triggered_at));
                ImGui::TableSetColumnIndex(4);
                if (!can_ack) {
                    ImGui::TextDisabled("—");
                } else {
                    char lbl[32];
                    std::snprintf(lbl, sizeof(lbl), "Ack##%lld",
                                  static_cast<long long>(a.alert_id));
                    if (ImGui::SmallButton(lbl))
                        ctrl_.AcknowledgeAlert(a.alert_id, session_.Get(), now_unix);
                }
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
    return open_;
}

} // namespace shelterops::ui::views
#endif // _WIN32
