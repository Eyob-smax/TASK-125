#pragma once
#include "shelterops/ui/controllers/ItemLedgerController.h"
#include "shelterops/shell/SessionContext.h"
#include <cstdint>

namespace shelterops::ui::views {

// Dear ImGui rendering of the Item Ledger window.
// Win32 only — compiled into ShelterOpsDesk.exe.
class ItemLedgerView {
public:
    ItemLedgerView(controllers::ItemLedgerController& ctrl,
                   shell::SessionContext&              session);

    // Returns false when the window is closed.
    bool Render(int64_t now_unix);

private:
    void RenderToolbar(int64_t now_unix);
    void RenderFilterBar();
    void RenderItemTable(int64_t now_unix);
    void RenderBarcodeInput(int64_t now_unix);
    void RenderReceiveStockModal(int64_t now_unix);
    void RenderIssueStockModal(int64_t now_unix);
    void RenderAddItemModal(int64_t now_unix);
    void RenderDetailPanel();
    void RenderErrorBanner();
    void RenderStatusBadges(const repositories::InventoryItemRecord& item,
                             int64_t now_unix);

    controllers::ItemLedgerController& ctrl_;
    shell::SessionContext&              session_;

    bool   open_           = true;
    bool   show_receive_   = false;
    bool   show_issue_     = false;
    bool   show_add_       = false;
    char   search_buf_[256] = {};
    char   barcode_buf_[64] = {};
    bool   barcode_focused_ = false;

    // Receive-stock modal fields
    int    recv_qty_          = 1;
    char   recv_vendor_[128]  = {};
    char   recv_lot_[64]      = {};
    int    recv_cost_         = 0;

    // Issue-stock modal fields
    int    issue_qty_         = 1;
    char   issue_recip_[128]  = {};
    char   issue_reason_[256] = {};

    // Add-item modal fields
    char   add_name_[128]     = {};
    char   add_loc_[128]      = {};
    char   add_barcode_[64]   = {};
    char   add_serial_[64]    = {};
    int    add_cost_          = 0;
};

} // namespace shelterops::ui::views
