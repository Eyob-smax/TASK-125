#if defined(_WIN32)
#include "shelterops/ui/views/AdminPanelView.h"
#include "shelterops/shell/ErrorDisplay.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>

namespace shelterops::ui::views {

AdminPanelView::AdminPanelView(
    controllers::AdminPanelController& ctrl,
    shell::SessionContext&             session)
    : ctrl_(ctrl), session_(session)
{}

bool AdminPanelView::Render(int64_t now_unix) {
    if (!open_) return false;

    ImGui::SetNextWindowSize({880.0f, 580.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Admin Panel", &open_)) {
        ImGui::End();
        return open_;
    }

    auto state = ctrl_.State();
    if (state == controllers::AdminPanelState::Error)
        shell::ErrorDisplay::RenderBanner(ctrl_.LastError().message);
    if (state == controllers::AdminPanelState::SaveSuccess) {
        ImGui::TextColored({0.2f, 0.9f, 0.2f, 1.0f}, "Saved.");
        ImGui::SameLine();
        if (ImGui::SmallButton("OK")) ctrl_.ClearError();
    }

    if (ImGui::BeginTabBar("AdminTabs")) {
        if (ImGui::BeginTabItem("Catalog"))     { RenderCatalogTab(now_unix);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Price Rules")) { RenderPriceRulesTab(now_unix); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Approvals"))   { RenderApprovalQueueTab(now_unix); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Retention"))   { RenderRetentionTab(now_unix);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Export Perms")){ RenderExportPermsTab(now_unix); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
    return open_;
}

void AdminPanelView::RenderCatalogTab(int64_t now_unix) {
    if (ImGui::Button("Refresh##cat")) ctrl_.LoadCatalog(now_unix);
    ImGui::Separator();

    const auto& entries = ctrl_.CatalogEntries();
    if (ImGui::BeginTable("CatalogTbl", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
            ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Default Cost (¢)");
        ImGui::TableSetupColumn("Active");
        ImGui::TableHeadersRow();
        for (const auto& e : entries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%lld", static_cast<long long>(e.entry_id));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", e.default_unit_cost_cents);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", e.is_active ? "Yes" : "No");
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Edit Entry");
    auto& form = ctrl_.CatalogForm();
    ImGui::SetNextItemWidth(80.0f);
    int eid = static_cast<int>(form.entry_id);
    if (ImGui::InputInt("Entry ID##cat", &eid))
        form.entry_id = eid;
    ImGui::SameLine();
    if (ImGui::Button("Load##cat")) ctrl_.BeginEditCatalogEntry(form.entry_id);

    char nbuf[256]; std::strncpy(nbuf, form.name.c_str(), sizeof(nbuf)-1);
    if (ImGui::InputText("Name##cat", nbuf, sizeof(nbuf)))
        form.name = nbuf;
    int cost = form.default_unit_cost_cents;
    if (ImGui::InputInt("Default Cost (¢)##cat", &cost))
        form.default_unit_cost_cents = cost;
    ImGui::Checkbox("Active##cat", &form.is_active);
    if (ImGui::Button("Save Catalog Entry##cat"))
        ctrl_.SubmitCatalogEntry(session_.Get(), now_unix);
}

void AdminPanelView::RenderPriceRulesTab(int64_t now_unix) {
    if (ImGui::Button("Refresh##pr")) ctrl_.LoadPriceRules(now_unix);
    ImGui::Separator();

    const auto& rules = ctrl_.PriceRules();
    if (ImGui::BeginTable("PriceTbl", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
            ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Amount");
        ImGui::TableHeadersRow();
        for (const auto& r : rules) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            char id_lbl[32];
            std::snprintf(id_lbl, sizeof(id_lbl), "%lld",
                          static_cast<long long>(r.rule_id));
            bool sel = false;
            if (ImGui::Selectable(id_lbl, sel,
                    ImGuiSelectableFlags_SpanAllColumns)) {}
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d", static_cast<int>(r.adjustment_type));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", r.amount);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("New Price Rule");
    auto& form = ctrl_.PriceRuleForm();
    char nbuf[256]; std::strncpy(nbuf, form.name.c_str(), sizeof(nbuf)-1);
    if (ImGui::InputText("Name##pr", nbuf, sizeof(nbuf))) form.name = nbuf;
    char abuf[64]; std::strncpy(abuf, form.applies_to.c_str(), sizeof(abuf)-1);
    if (ImGui::InputText("Applies To##pr", abuf, sizeof(abuf))) form.applies_to = abuf;
    float amt = static_cast<float>(form.amount);
    if (ImGui::InputFloat("Amount##pr", &amt)) form.amount = amt;
    if (ImGui::Button("Create Price Rule")) ctrl_.SubmitPriceRule(session_.Get(), now_unix);
}

void AdminPanelView::RenderApprovalQueueTab(int64_t now_unix) {
    if (ImGui::Button("Refresh##aq")) ctrl_.LoadApprovalQueue(now_unix);
    ImGui::Separator();

    const auto& queue = ctrl_.ApprovalQueue();
    if (queue.empty()) {
        ImGui::TextDisabled("No pending approvals.");
    } else {
        if (ImGui::BeginTable("ApprTbl", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Approval ID");
            ImGui::TableSetupColumn("Booking ID");
            ImGui::TableSetupColumn("Requested By");
            ImGui::TableSetupColumn("Actions");
            ImGui::TableHeadersRow();
            for (const auto& e : queue) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%lld", static_cast<long long>(e.approval_id));
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%lld", static_cast<long long>(e.booking_id));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%lld", static_cast<long long>(e.requested_by));
                ImGui::TableSetColumnIndex(3);
                char albl[32]; std::snprintf(albl, sizeof(albl), "Approve##%lld",
                    static_cast<long long>(e.booking_id));
                if (ImGui::SmallButton(albl))
                    ctrl_.ApproveBooking(e.booking_id, session_.Get(), now_unix);
                ImGui::SameLine();
                char rlbl[32]; std::snprintf(rlbl, sizeof(rlbl), "Reject##%lld",
                    static_cast<long long>(e.booking_id));
                if (ImGui::SmallButton(rlbl))
                    ctrl_.RejectBooking(e.booking_id, session_.Get(), now_unix);
            }
            ImGui::EndTable();
        }
    }
}

void AdminPanelView::RenderRetentionTab(int64_t now_unix) {
    if (ImGui::Button("Refresh##ret")) ctrl_.LoadRetentionPolicies();
    ImGui::Separator();

    const auto& policies = ctrl_.RetentionPolicies();
    if (ImGui::BeginTable("RetTbl", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Entity Type");
        ImGui::TableSetupColumn("Years");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();
        for (const auto& p : policies) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(p.entity_type.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", p.retention_years);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s",
                p.action == domain::RetentionActionKind::Delete ? "delete" : "anonymize");
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Set Retention Policy");
    static char et_buf[64] = "";
    ImGui::InputText("Entity Type##ret", et_buf, sizeof(et_buf));
    static int years = 7;
    ImGui::InputInt("Years##ret", &years);
    static bool do_delete = false;
    ImGui::Checkbox("Delete (vs. anonymize)##ret", &do_delete);
    if (ImGui::Button("Save Retention Policy##ret")) {
        ctrl_.SetRetentionPolicy(
            et_buf, years,
            do_delete ? domain::RetentionActionKind::Delete
                      : domain::RetentionActionKind::Anonymize,
            session_.Get(), now_unix);
    }
}

void AdminPanelView::RenderExportPermsTab(int64_t now_unix) {
    static char role_buf[64] = "operations_manager";
    ImGui::InputText("Role##ep", role_buf, sizeof(role_buf));
    ImGui::SameLine();
    if (ImGui::Button("Load##ep")) ctrl_.LoadExportPermissions(role_buf);
    ImGui::Separator();

    const auto& perms = ctrl_.ExportPermissions();
    if (ImGui::BeginTable("ExpPermTbl", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("Report Type");
        ImGui::TableSetupColumn("CSV");
        ImGui::TableSetupColumn("PDF");
        ImGui::TableHeadersRow();
        for (const auto& p : perms) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(p.role.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(p.report_type.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", p.csv_allowed ? "Yes" : "No");
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", p.pdf_allowed ? "Yes" : "No");
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Text("Edit Permission");
    static char rt_buf[64] = "occupancy";
    static bool csv_ok = false, pdf_ok = false;
    ImGui::InputText("Report Type##ep2", rt_buf, sizeof(rt_buf));
    ImGui::Checkbox("CSV##ep", &csv_ok);
    ImGui::SameLine();
    ImGui::Checkbox("PDF##ep", &pdf_ok);
    if (ImGui::Button("Save Permission##ep"))
        ctrl_.SetExportPermission(role_buf, rt_buf, csv_ok, pdf_ok,
                                   session_.Get(), now_unix);
}

} // namespace shelterops::ui::views
#endif // _WIN32
