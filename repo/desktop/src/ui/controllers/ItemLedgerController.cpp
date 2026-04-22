#include "shelterops/ui/controllers/ItemLedgerController.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>

namespace shelterops::ui::controllers {

ItemLedgerController::ItemLedgerController(
    services::InventoryService&        inventory_svc,
    repositories::InventoryRepository& inventory_repo)
    : inventory_svc_(inventory_svc), inventory_repo_(inventory_repo)
{}

void ItemLedgerController::SetFilter(const ItemLedgerFilter& f) {
    filter_ = f;
    is_dirty_ = true;
}

void ItemLedgerController::Refresh(int64_t now_unix) {
    state_ = ItemLedgerState::Loading;
    last_refresh_unix_ = now_unix;

    try {
        auto pool = inventory_repo_.ListAllActiveItems();

        items_.clear();
        for (const auto& item : pool) {
            if (PassesFilter(item, now_unix))
                items_.push_back(item);
        }
        state_    = ItemLedgerState::Loaded;
        is_dirty_ = true;
        spdlog::debug("ItemLedgerController: loaded {} items", items_.size());
    } catch (const std::exception& e) {
        last_error_ = { common::ErrorCode::Internal, e.what() };
        state_ = ItemLedgerState::Error;
        spdlog::error("ItemLedgerController::Refresh: {}", e.what());
    }
}

bool ItemLedgerController::PassesFilter(
    const repositories::InventoryItemRecord& item, int64_t now_unix) const
{
    if (!item.is_active) return false;
    if (filter_.category_id != 0 && item.category_id != filter_.category_id)
        return false;
    if (filter_.show_low_stock_only && item.quantity >= 10)
        return false;
    if (!filter_.show_expired && item.expiration_date > 0 && now_unix >= item.expiration_date)
        return false;
    if (!filter_.search_text.empty()) {
        auto lc = [](const std::string& s) {
            std::string out = s;
            for (auto& c : out) c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            return out;
        };
        const std::string q = lc(filter_.search_text);
        if (lc(item.name).find(q)             == std::string::npos &&
            lc(item.barcode).find(q)          == std::string::npos &&
            lc(item.serial_number).find(q)    == std::string::npos &&
            lc(item.storage_location).find(q) == std::string::npos)
            return false;
    }
    (void)now_unix;
    return true;
}

void ItemLedgerController::SelectItem(int64_t item_id) {
    selected_item_id_ = item_id;
}

void ItemLedgerController::ProcessBarcodeInput(
    const std::string& raw_input, int64_t now_unix)
{
    state_ = ItemLedgerState::BarcodeEntry;
    barcode_result_.reset();

    auto token = infrastructure::BarcodeHandler::ProcessScan(raw_input);
    if (!token.is_printable) {
        last_error_ = { common::ErrorCode::InvalidInput,
                        "Barcode contains non-printable characters." };
        state_ = ItemLedgerState::BarcodeNotFound;
        return;
    }

    auto result = inventory_svc_.LookupByBarcode(token.value);
    if (auto* item = std::get_if<repositories::InventoryItemRecord>(&result)) {
        barcode_result_ = *item;
        selected_item_id_ = item->item_id;
        state_ = ItemLedgerState::BarcodeFound;
    } else {
        last_error_ = std::get<common::ErrorEnvelope>(result);
        state_ = ItemLedgerState::BarcodeNotFound;
    }
    (void)now_unix;
}

void ItemLedgerController::ClearBarcodeResult() {
    barcode_result_.reset();
    state_ = ItemLedgerState::Loaded;
}

void ItemLedgerController::BeginReceiveStock(int64_t item_id) {
    receive_form_ = {};
    receive_form_.item_id = item_id;
    validation_.Clear();
    state_ = ItemLedgerState::ReceivingStock;
}

bool ItemLedgerController::SubmitReceiveStock(
    const services::UserContext& ctx, int64_t now_unix)
{
    validation_.Clear();
    if (receive_form_.quantity <= 0)
        validation_.SetError("quantity", "Quantity must be at least 1.");
    if (receive_form_.vendor.empty())
        validation_.SetError("vendor", "Vendor is required.");
    if (validation_.HasErrors()) return false;

    if (auto err = inventory_svc_.ReceiveStock(
            receive_form_.item_id, receive_form_.quantity,
            receive_form_.vendor, receive_form_.lot_number,
            receive_form_.unit_cost_cents, ctx, now_unix)) {
        last_error_ = *err;
        state_ = ItemLedgerState::Error;
        return false;
    }
    state_    = ItemLedgerState::Loaded;
    is_dirty_ = true;
    return true;
}

void ItemLedgerController::BeginIssueStock(int64_t item_id) {
    issue_form_ = {};
    issue_form_.item_id = item_id;
    validation_.Clear();
    state_ = ItemLedgerState::IssuingStock;
}

bool ItemLedgerController::SubmitIssueStock(
    const services::UserContext& ctx, int64_t now_unix)
{
    validation_.Clear();
    if (issue_form_.quantity <= 0)
        validation_.SetError("quantity", "Quantity must be at least 1.");
    if (issue_form_.reason.empty())
        validation_.SetError("reason", "Reason is required.");
    if (validation_.HasErrors()) return false;

    if (auto err = inventory_svc_.IssueStock(
            issue_form_.item_id, issue_form_.quantity,
            issue_form_.recipient, issue_form_.reason,
            issue_form_.booking_id, ctx, now_unix)) {
        last_error_ = *err;
        state_ = ItemLedgerState::Error;
        return false;
    }
    state_    = ItemLedgerState::Loaded;
    is_dirty_ = true;
    return true;
}

void ItemLedgerController::BeginAddItem() {
    add_item_form_ = {};
    validation_.Clear();
    state_ = ItemLedgerState::AddingItem;
}

bool ItemLedgerController::SubmitAddItem(
    const services::UserContext& ctx, int64_t now_unix)
{
    validation_.Clear();
    if (add_item_form_.name.empty())
        validation_.SetError("name", "Item name is required.");
    if (validation_.HasErrors()) return false;

    auto result = inventory_svc_.AddItem(add_item_form_, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&result)) {
        if (err->code == common::ErrorCode::InvalidInput &&
            err->message.find("Duplicate") != std::string::npos)
            state_ = ItemLedgerState::DuplicateSerial;
        else
            state_ = ItemLedgerState::Error;
        last_error_ = *err;
        return false;
    }
    state_    = ItemLedgerState::Loaded;
    is_dirty_ = true;
    return true;
}

bool ItemLedgerController::MarkExpired(
    int64_t item_id, const services::UserContext& ctx, int64_t now_unix)
{
    state_ = ItemLedgerState::MarkingExpired;
    inventory_svc_.MarkExpired(item_id, ctx, now_unix);
    state_    = ItemLedgerState::Loaded;
    is_dirty_ = true;
    return true;
}

std::string ItemLedgerController::ClipboardTsv() const {
    static const std::vector<std::string> kHeaders{
        "ID", "Name", "Category", "Qty", "Location", "Barcode", "Serial", "Expires"
    };

    return primitives::TableSortState::FormatTsv(
        kHeaders,
        sort_state_.ComputeIndices(items_.size(), nullptr, nullptr),
        kHeaders.size(),
        [this](std::size_t row, std::size_t col) -> std::string {
            const auto& it = items_[row];
            switch (col) {
            case 0: return std::to_string(it.item_id);
            case 1: return it.name;
            case 2: return std::to_string(it.category_id);
            case 3: return std::to_string(it.quantity);
            case 4: return it.storage_location;
            case 5: return it.barcode;
            case 6: return it.serial_number;
            case 7: return it.expiration_date
                    ? std::to_string(it.expiration_date) : "";
            default: return "";
            }
        });
}

} // namespace shelterops::ui::controllers
