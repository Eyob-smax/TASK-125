#include "shelterops/services/InventoryService.h"
#include "shelterops/services/AuthorizationService.h"
#include "shelterops/domain/InventoryRules.h"
#include <spdlog/spdlog.h>
#include <ctime>

namespace shelterops::services {

InventoryService::InventoryService(repositories::InventoryRepository& inventory,
                                    AuditService& audit)
    : inventory_(inventory), audit_(audit) {}

static int64_t DayMidnight(int64_t ts) {
    return (ts / 86400) * 86400;
}

ItemResult InventoryService::AddItem(const repositories::NewItemParams& params,
                                      const UserContext& user_ctx,
                                      int64_t now_unix) {
    if (auto denied = AuthorizationService::RequireInventoryAccess(user_ctx.role))
        return *denied;

    // Serial validation.
    if (!params.serial_number.empty()) {
        auto existing = inventory_.FindBySerial(params.serial_number);
        auto serial_result = domain::ValidateSerial(
            params.serial_number,
            existing.has_value(),
            existing ? existing->item_id : 0);

        if (!serial_result.is_valid) {
            audit_.RecordSystemEvent("DUPLICATE_SERIAL_REJECTED",
                "Duplicate serial number already owned by item " +
                std::to_string(serial_result.existing_item_id),
                now_unix);
            return common::ErrorEnvelope{
                common::ErrorCode::InvalidInput,
                "Duplicate serial number; already assigned to item " +
                std::to_string(serial_result.existing_item_id)
            };
        }
    }

    int64_t item_id = inventory_.InsertItem(params, now_unix);

    audit_.RecordSystemEvent("INVENTORY_ITEM_ADDED",
        "Item " + std::to_string(item_id) + " '" + params.name + "' added",
        now_unix);

    return item_id;
}

std::optional<common::ErrorEnvelope> InventoryService::ReceiveStock(
                                                      int64_t item_id, int quantity,
                                                      const std::string& vendor,
                                                      const std::string& lot_number,
                                                      int unit_cost_cents,
                                                      const UserContext& user_ctx,
                                                      int64_t now_unix) {
    if (auto denied = AuthorizationService::RequireInventoryAccess(user_ctx.role))
        return *denied;

    if (quantity <= 0) {
        return common::ErrorEnvelope{common::ErrorCode::InvalidInput,
                                      "Quantity must be positive"};
    }

    auto item = inventory_.FindItemById(item_id);
    if (!item) {
        return common::ErrorEnvelope{common::ErrorCode::ItemNotFound,
                                      "Item not found"};
    }

    inventory_.InsertInbound(item_id, quantity, vendor, lot_number,
                              unit_cost_cents, user_ctx.user_id, now_unix);

    audit_.RecordSystemEvent("STOCK_RECEIVED",
        "Item " + std::to_string(item_id) + " received " +
        std::to_string(quantity) + " units",
        now_unix);

    return std::nullopt;
}

std::optional<common::ErrorEnvelope> InventoryService::IssueStock(
                                                    int64_t item_id, int quantity,
                                                    const std::string& recipient,
                                                    const std::string& reason,
                                                    int64_t booking_id,
                                                    const UserContext& user_ctx,
                                                    int64_t now_unix) {
    if (auto denied = AuthorizationService::RequireInventoryAccess(user_ctx.role))
        return *denied;

    if (quantity <= 0) {
        return common::ErrorEnvelope{common::ErrorCode::InvalidInput,
                                      "Quantity must be positive"};
    }

    auto item = inventory_.FindItemById(item_id);
    if (!item) {
        return common::ErrorEnvelope{common::ErrorCode::ItemNotFound,
                                      "Item not found"};
    }

    if (!inventory_.IssueStockAtomic(item_id, quantity, recipient, reason,
                                     booking_id, user_ctx.user_id, now_unix,
                                     DayMidnight(now_unix))) {
        auto refreshed = inventory_.FindItemById(item_id);
        const int available = refreshed ? refreshed->quantity : item->quantity;
        return common::ErrorEnvelope{common::ErrorCode::InvalidInput,
                                     "Insufficient stock: only " +
                                     std::to_string(available) + " available"};
    }

    audit_.RecordSystemEvent("STOCK_ISSUED",
        "Item " + std::to_string(item_id) + " issued " +
        std::to_string(quantity) + " units",
        now_unix);

    return std::nullopt;
}

std::optional<common::ErrorEnvelope> InventoryService::IssueUnits(
                                                    int64_t item_id, int quantity,
                                                    const std::string& reason,
                                                    const UserContext& user_ctx,
                                                    int64_t now_unix) {
    return IssueStock(item_id, quantity, "", reason, 0, user_ctx, now_unix);
}

InventoryService::LookupResult InventoryService::LookupByBarcode(
    const std::string& barcode) {
    auto result = domain::ValidateBarcode(barcode);
    if (!result.is_valid) {
        return common::ErrorEnvelope{common::ErrorCode::InvalidInput,
                                      result.error_message};
    }

    auto item = inventory_.FindByBarcode(barcode);
    if (!item) {
        return common::ErrorEnvelope{common::ErrorCode::ItemNotFound,
                                      "No item found with barcode: " + barcode};
    }
    return *item;
}

void InventoryService::MarkExpired(int64_t item_id, const UserContext& user_ctx,
                                    int64_t now_unix) {
    if (auto denied = AuthorizationService::RequireInventoryAccess(user_ctx.role)) {
        spdlog::warn("InventoryService::MarkExpired denied: role={}", user_ctx.role_string);
        return;
    }
    inventory_.MarkExpired(item_id, now_unix);
    audit_.RecordSystemEvent("ITEM_MARKED_EXPIRED",
        "Item " + std::to_string(item_id) + " marked expired",
        now_unix);
}

} // namespace shelterops::services
