#pragma once
#include "shelterops/services/InventoryService.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/infrastructure/BarcodeHandler.h"
#include "shelterops/services/UserContext.h"
#include "shelterops/common/ErrorEnvelope.h"
#include "shelterops/ui/primitives/TableSortState.h"
#include "shelterops/ui/primitives/ValidationState.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::ui::controllers {

enum class ItemLedgerState {
    Idle,
    Loading,
    Loaded,
    Error,
    BarcodeEntry,
    BarcodeFound,
    BarcodeNotFound,
    ReceivingStock,
    IssuingStock,
    AddingItem,
    DuplicateSerial,
    MarkingExpired
};

struct ItemLedgerFilter {
    int64_t     category_id         = 0;    // 0 = all categories
    std::string search_text;
    bool        show_expired        = false;
    bool        show_low_stock_only = false;
};

struct ReceiveStockForm {
    int64_t     item_id        = 0;
    int         quantity       = 1;
    std::string vendor;
    std::string lot_number;
    int         unit_cost_cents = 0;
};

struct IssueStockForm {
    int64_t     item_id    = 0;
    int         quantity   = 1;
    std::string recipient;
    std::string reason;
    int64_t     booking_id = 0;
};

// Controller for the Item Ledger window.
// Holds filter/items/selection state; delegates business calls to InventoryService.
// Cross-platform: no ImGui dependency.
class ItemLedgerController {
public:
    ItemLedgerController(services::InventoryService&        inventory_svc,
                          repositories::InventoryRepository& inventory_repo);

    // --- State queries ---
    ItemLedgerState                                    State()          const noexcept { return state_; }
    const std::vector<repositories::InventoryItemRecord>& Items()      const noexcept { return items_; }
    int64_t                                            SelectedItem()   const noexcept { return selected_item_id_; }
    const std::optional<repositories::InventoryItemRecord>& BarcodeResult() const noexcept { return barcode_result_; }
    const common::ErrorEnvelope&                       LastError()      const noexcept { return last_error_; }
    const ItemLedgerFilter&                            CurrentFilter()  const noexcept { return filter_; }
    ReceiveStockForm&                                  ReceiveForm()    noexcept       { return receive_form_; }
    IssueStockForm&                                    IssueForm()      noexcept       { return issue_form_; }
    repositories::NewItemParams&                       AddItemForm()    noexcept       { return add_item_form_; }
    primitives::ValidationState&                       Validation()     noexcept       { return validation_; }
    primitives::TableSortState&                        SortState()      noexcept       { return sort_state_; }
    bool                                               IsDirty()        const noexcept { return is_dirty_; }

    // --- Commands ---
    void SetFilter(const ItemLedgerFilter& f);
    void Refresh(int64_t now_unix);

    void SelectItem(int64_t item_id);

    // Process a raw barcode scanner string (USB-wedge CR/LF stripped internally).
    void ProcessBarcodeInput(const std::string& raw_input, int64_t now_unix);
    void ClearBarcodeResult();

    void BeginReceiveStock(int64_t item_id);
    bool SubmitReceiveStock(const services::UserContext& ctx, int64_t now_unix);

    void BeginIssueStock(int64_t item_id);
    bool SubmitIssueStock(const services::UserContext& ctx, int64_t now_unix);

    void BeginAddItem();
    bool SubmitAddItem(const services::UserContext& ctx, int64_t now_unix);

    bool MarkExpired(int64_t item_id, const services::UserContext& ctx, int64_t now_unix);

    // Tab-separated values of the current filtered item list.
    std::string ClipboardTsv() const;

    void ClearDirty() noexcept { is_dirty_ = false; }
    void ClearError() noexcept { last_error_ = {}; state_ = ItemLedgerState::Loaded; }

private:
    bool PassesFilter(const repositories::InventoryItemRecord& item,
                      int64_t now_unix) const;

    services::InventoryService&        inventory_svc_;
    repositories::InventoryRepository& inventory_repo_;

    ItemLedgerState                                   state_           = ItemLedgerState::Idle;
    std::vector<repositories::InventoryItemRecord>    items_;
    int64_t                                           selected_item_id_ = 0;
    std::optional<repositories::InventoryItemRecord>  barcode_result_;
    ItemLedgerFilter                                  filter_;
    ReceiveStockForm                                  receive_form_;
    IssueStockForm                                    issue_form_;
    repositories::NewItemParams                       add_item_form_;
    common::ErrorEnvelope                             last_error_;
    primitives::ValidationState                       validation_;
    primitives::TableSortState                        sort_state_;
    int64_t                                           last_refresh_unix_ = 0;
    bool                                              is_dirty_          = false;
};

} // namespace shelterops::ui::controllers
