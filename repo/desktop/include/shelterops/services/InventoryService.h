#pragma once
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/UserContext.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <variant>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::services {

using ItemResult = std::variant<int64_t, common::ErrorEnvelope>;

class InventoryService {
public:
    InventoryService(repositories::InventoryRepository& inventory,
                     AuditService&                      audit);

    // Add a new item. Validates serial uniqueness; on duplicate emits audit + error
    // with the existing item's id in the message.
    ItemResult AddItem(const repositories::NewItemParams& params,
                       const UserContext& user_ctx,
                       int64_t now_unix);

    // Receive stock: insert inbound_record, increment quantity.
    // Returns std::nullopt on success, or the error envelope on failure.
    std::optional<common::ErrorEnvelope> ReceiveStock(
                                       int64_t item_id, int quantity,
                                       const std::string& vendor,
                                       const std::string& lot_number,
                                       int unit_cost_cents,
                                       const UserContext& user_ctx,
                                       int64_t now_unix);

    // Issue stock: guard qty <= current, insert outbound_record, decrement,
    // update usage history.
    // Returns std::nullopt on success, or the error envelope on failure.
    std::optional<common::ErrorEnvelope> IssueStock(
                                     int64_t item_id, int quantity,
                                     const std::string& recipient,
                                     const std::string& reason,
                                     int64_t booking_id,
                                     const UserContext& user_ctx,
                                     int64_t now_unix);

    // Quick-action issue with no recipient/booking linkage.
    // Returns std::nullopt on success, or the error envelope on failure.
    std::optional<common::ErrorEnvelope> IssueUnits(
                                     int64_t item_id, int quantity,
                                     const std::string& reason,
                                     const UserContext& user_ctx,
                                     int64_t now_unix);

    // Barcode lookup: validates format, returns item or ITEM_NOT_FOUND envelope.
    using LookupResult = std::variant<repositories::InventoryItemRecord,
                                      common::ErrorEnvelope>;
    LookupResult LookupByBarcode(const std::string& barcode);

    void MarkExpired(int64_t item_id, const UserContext& user_ctx, int64_t now_unix);

private:
    repositories::InventoryRepository& inventory_;
    AuditService&                      audit_;
};

} // namespace shelterops::services
