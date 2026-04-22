#if defined(_WIN32)
#include "shelterops/ui/views/ItemLedgerView.h"
#include "shelterops/shell/ClipboardHelper.h"
#include "shelterops/shell/ErrorDisplay.h"
#include <imgui.h>
#include <cstring>
#include <cstdio>
#include <chrono>

namespace shelterops::ui::views {

ItemLedgerView::ItemLedgerView(
    controllers::ItemLedgerController& ctrl,
    shell::SessionContext&              session)
    : ctrl_(ctrl), session_(session)
{}

bool ItemLedgerView::Render(int64_t now_unix) {
    if (!open_) return false;

    ImGui::SetNextWindowSize({900.0f, 600.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Item Ledger", &open_)) {
        ImGui::End();
        return open_;
    }

    RenderToolbar(now_unix);
    ImGui::Separator();
    RenderFilterBar();
    ImGui::Separator();

    auto state = ctrl_.State();

    if (state == controllers::ItemLedgerState::BarcodeEntry)
        RenderBarcodeInput(now_unix);

    if (state == controllers::ItemLedgerState::Error ||
        state == controllers::ItemLedgerState::DuplicateSerial ||
        state == controllers::ItemLedgerState::BarcodeNotFound)
        RenderErrorBanner();

    RenderItemTable(now_unix);

    // Modals
    if (show_receive_) RenderReceiveStockModal(now_unix);
    if (show_issue_)   RenderIssueStockModal(now_unix);
    if (show_add_)     RenderAddItemModal(now_unix);

    ImGui::End();
    return open_;
}

void ItemLedgerView::RenderToolbar(int64_t now_unix) {
    bool can_write = session_.CurrentRole() != domain::UserRole::Auditor;

    if (ImGui::Button("Refresh"))
        ctrl_.Refresh(now_unix);

    ImGui::SameLine();
    if (!can_write) ImGui::BeginDisabled();
    if (ImGui::Button("Receive Stock")) {
        ctrl_.BeginReceiveStock(ctrl_.SelectedItem());
        std::memset(recv_vendor_, 0, sizeof(recv_vendor_));
        recv_qty_ = 1; recv_cost_ = 0;
        show_receive_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Issue Stock")) {
        ctrl_.BeginIssueStock(ctrl_.SelectedItem());
        std::memset(issue_recip_, 0, sizeof(issue_recip_));
        std::memset(issue_reason_, 0, sizeof(issue_reason_));
        issue_qty_ = 1;
        show_issue_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Item [Ctrl+N]")) {
        ctrl_.BeginAddItem();
        std::memset(add_name_, 0, sizeof(add_name_));
        std::memset(add_loc_,  0, sizeof(add_loc_));
        std::memset(add_barcode_, 0, sizeof(add_barcode_));
        std::memset(add_serial_,  0, sizeof(add_serial_));
        add_cost_ = 0;
        show_add_ = true;
    }
    if (!can_write) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Scan Barcode")) {
        barcode_buf_[0]  = '\0';
        barcode_focused_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy TSV [Ctrl+Shift+E]"))
        shell::ClipboardHelper::SetText(ctrl_.ClipboardTsv());
    (void)now_unix;
}

void ItemLedgerView::RenderFilterBar() {
    auto f = ctrl_.CurrentFilter();
    controllers::ItemLedgerFilter updated = f;

    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("Search##il", search_buf_, sizeof(search_buf_)))
        updated.search_text = search_buf_;

    ImGui::SameLine();
    bool exp = f.show_expired;
    if (ImGui::Checkbox("Show Expired", &exp))
        updated.show_expired = exp;

    ImGui::SameLine();
    bool low = f.show_low_stock_only;
    if (ImGui::Checkbox("Low Stock Only", &low))
        updated.show_low_stock_only = low;

    if (updated.search_text     != f.search_text     ||
        updated.show_expired    != f.show_expired    ||
        updated.show_low_stock_only != f.show_low_stock_only)
        ctrl_.SetFilter(updated);
}

void ItemLedgerView::RenderBarcodeInput(int64_t now_unix) {
    ImGui::SeparatorText("Barcode Scanner");
    if (barcode_focused_) {
        ImGui::SetKeyboardFocusHere();
        barcode_focused_ = false;
    }
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("Barcode##il", barcode_buf_, sizeof(barcode_buf_),
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        ctrl_.ProcessBarcodeInput(barcode_buf_, now_unix);
        barcode_buf_[0] = '\0';
    }

    // Show result.
    auto state = ctrl_.State();
    if (state == controllers::ItemLedgerState::BarcodeFound) {
        const auto& r = ctrl_.BarcodeResult();
        if (r) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            ImGui::Text("Found: %s  (Qty: %d)", r->name.c_str(), r->quantity);
            ImGui::PopStyleColor();
        }
    }
    ImGui::Separator();
}

void ItemLedgerView::RenderItemTable(int64_t now_unix) {
    const auto& items = ctrl_.Items();
    if (ctrl_.State() == controllers::ItemLedgerState::Loading) {
        ImGui::TextUnformatted("Loading...");
        return;
    }
    if (items.empty()) {
        ImGui::TextDisabled("No items match the current filter.");
        return;
    }

    auto& sort = ctrl_.SortState();

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Sortable;
    float table_h = ImGui::GetContentRegionAvail().y * 0.65f;

    if (!ImGui::BeginTable("##items", 7, flags, {0.0f, table_h})) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("ID",       ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
    ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_None,        0.0f, 1);
    ImGui::TableSetupColumn("Qty",      ImGuiTableColumnFlags_None,        0.0f, 2);
    ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_None,        0.0f, 3);
    ImGui::TableSetupColumn("Barcode",  ImGuiTableColumnFlags_None,        0.0f, 4);
    ImGui::TableSetupColumn("Serial",   ImGuiTableColumnFlags_None,        0.0f, 5);
    ImGui::TableSetupColumn("Status",   ImGuiTableColumnFlags_NoSort,      0.0f, 6);
    ImGui::TableHeadersRow();

    if (auto* ss = ImGui::TableGetSortSpecs()) {
        if (ss->SpecsDirty && ss->SpecsCount > 0) {
            sort.SetSort(ss->Specs[0].ColumnIndex,
                         ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
            ss->SpecsDirty = false;
        }
    }

    auto indices = sort.ComputeIndices(
        items.size(),
        [&](std::size_t a, std::size_t b) {
            const auto& ia = items[a]; const auto& ib = items[b];
            bool asc = sort.GetSort().ascending;
            switch (sort.GetSort().column_idx) {
            case 0: return asc ? ia.item_id < ib.item_id : ia.item_id > ib.item_id;
            case 1: return asc ? ia.name < ib.name : ia.name > ib.name;
            case 2: return asc ? ia.quantity < ib.quantity : ia.quantity > ib.quantity;
            default: return a < b;
            }
        },
        [&](std::size_t i) { return true; });

    bool can_write = session_.CurrentRole() != domain::UserRole::Auditor;

    for (std::size_t i : indices) {
        const auto& it = items[i];
        ImGui::TableNextRow();
        bool selected = it.item_id == ctrl_.SelectedItem();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::Selectable(std::to_string(it.item_id).c_str(),
                              selected,
                              ImGuiSelectableFlags_SpanAllColumns, {0, 0}))
            ctrl_.SelectItem(it.item_id);

        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(it.name.c_str());
        ImGui::TableSetColumnIndex(2); ImGui::Text("%d", it.quantity);
        ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(it.storage_location.c_str());
        ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(it.barcode.c_str());
        ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(it.serial_number.c_str());
        ImGui::TableSetColumnIndex(6); RenderStatusBadges(it, now_unix);

        // Context menu on right-click.
        if (ImGui::BeginPopupContextItem()) {
            if (can_write && ImGui::MenuItem("Receive Stock")) {
                ctrl_.BeginReceiveStock(it.item_id);
                show_receive_ = true;
            }
            if (can_write && ImGui::MenuItem("Issue Stock")) {
                ctrl_.BeginIssueStock(it.item_id);
                show_issue_ = true;
            }
            if (can_write && ImGui::MenuItem("Mark Expired")) {
                ctrl_.MarkExpired(it.item_id, session_.Get(), now_unix);
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndTable();
}

void ItemLedgerView::RenderReceiveStockModal(int64_t now_unix) {
    ImGui::OpenPopup("Receive Stock##modal");
    ImGui::SetNextWindowSize({400.0f, 260.0f}, ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Receive Stock##modal", &show_receive_)) return;

    auto& form = ctrl_.ReceiveForm();
    const auto& vstate = ctrl_.Validation();

    ImGui::Text("Item: %lld", (long long)form.item_id);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Quantity##recv", &recv_qty_)) form.quantity = recv_qty_;
    if (vstate.HasError("quantity")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
        ImGui::TextUnformatted(vstate.GetError("quantity").c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("Vendor##recv", recv_vendor_, sizeof(recv_vendor_)))
        form.vendor = recv_vendor_;
    if (vstate.HasError("vendor")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
        ImGui::TextUnformatted(vstate.GetError("vendor").c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Lot##recv", recv_lot_, sizeof(recv_lot_)))
        form.lot_number = recv_lot_;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Unit Cost (cents)##recv", &recv_cost_))
        form.unit_cost_cents = recv_cost_;

    ImGui::Separator();
    if (ImGui::Button("Submit##recv")) {
        if (ctrl_.SubmitReceiveStock(session_.Get(), now_unix))
            show_receive_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##recv")) show_receive_ = false;
    ImGui::EndPopup();
}

void ItemLedgerView::RenderIssueStockModal(int64_t now_unix) {
    ImGui::OpenPopup("Issue Stock##modal");
    ImGui::SetNextWindowSize({400.0f, 240.0f}, ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Issue Stock##modal", &show_issue_)) return;

    auto& form = ctrl_.IssueForm();
    const auto& vstate = ctrl_.Validation();

    ImGui::Text("Item: %lld", (long long)form.item_id);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Quantity##issue", &issue_qty_)) form.quantity = issue_qty_;
    if (vstate.HasError("quantity")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
        ImGui::TextUnformatted(vstate.GetError("quantity").c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("Recipient##issue", issue_recip_, sizeof(issue_recip_)))
        form.recipient = issue_recip_;
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::InputText("Reason##issue", issue_reason_, sizeof(issue_reason_)))
        form.reason = issue_reason_;
    if (vstate.HasError("reason")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
        ImGui::TextUnformatted(vstate.GetError("reason").c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::Button("Submit##issue")) {
        if (ctrl_.SubmitIssueStock(session_.Get(), now_unix))
            show_issue_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##issue")) show_issue_ = false;
    ImGui::EndPopup();
}

void ItemLedgerView::RenderAddItemModal(int64_t now_unix) {
    ImGui::OpenPopup("Add Item##modal");
    ImGui::SetNextWindowSize({440.0f, 300.0f}, ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Add Item##modal", &show_add_)) return;

    auto& form = ctrl_.AddItemForm();
    const auto& vstate = ctrl_.Validation();

    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::InputText("Name##add", add_name_, sizeof(add_name_)))
        form.name = add_name_;
    if (vstate.HasError("name")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
        ImGui::TextUnformatted(vstate.GetError("name").c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("Location##add", add_loc_, sizeof(add_loc_)))
        form.storage_location = add_loc_;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Barcode##add", add_barcode_, sizeof(add_barcode_)))
        form.barcode = add_barcode_;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Serial##add", add_serial_, sizeof(add_serial_)))
        form.serial_number = add_serial_;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("Unit Cost (cents)##add", &add_cost_))
        form.unit_cost_cents = add_cost_;

    if (ctrl_.State() == controllers::ItemLedgerState::DuplicateSerial) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.6f,0.1f,1));
        ImGui::TextWrapped("Duplicate serial number. %s",
                            ctrl_.LastError().message.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    if (ImGui::Button("Add##add")) {
        if (ctrl_.SubmitAddItem(session_.Get(), now_unix))
            show_add_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##add")) show_add_ = false;
    ImGui::EndPopup();
}

void ItemLedgerView::RenderDetailPanel() {
    // No-op; detail is shown via context menu or inline selection.
}

void ItemLedgerView::RenderErrorBanner() {
    auto state = ctrl_.State();
    if (state == controllers::ItemLedgerState::DuplicateSerial) return; // shown in modal
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImGui::TextWrapped("Error: %s",
                        shell::ErrorDisplay::UserMessage(ctrl_.LastError()).c_str());
    ImGui::PopStyleColor();
    if (ImGui::Button("Dismiss##il")) ctrl_.ClearError();
}

void ItemLedgerView::RenderStatusBadges(
    const repositories::InventoryItemRecord& item, int64_t now_unix)
{
    if (!item.is_active) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
        ImGui::TextUnformatted("Expired");
        ImGui::PopStyleColor();
        return;
    }
    if (item.quantity == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.3f, 0.3f, 1));
        ImGui::TextUnformatted("Out");
        ImGui::PopStyleColor();
    } else if (item.quantity < 5) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.1f, 1));
        ImGui::TextUnformatted("Low");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1));
        ImGui::TextUnformatted("OK");
        ImGui::PopStyleColor();
    }
    if (item.expiration_date > 0 && item.expiration_date < now_unix + 14 * 86400) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.5f, 0.0f, 1));
        ImGui::TextUnformatted("Exp soon");
        ImGui::PopStyleColor();
    }
}

} // namespace shelterops::ui::views
#endif // _WIN32
