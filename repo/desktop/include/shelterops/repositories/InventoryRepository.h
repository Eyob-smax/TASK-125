#pragma once
#include "shelterops/infrastructure/Database.h"
#include "shelterops/domain/InventoryRules.h"
#include "shelterops/domain/AlertRules.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::repositories {

struct CategoryRecord {
    int64_t     category_id               = 0;
    std::string name;
    std::string unit;
    int         low_stock_threshold_days  = 7;
    int         expiration_alert_days     = 14;
    bool        is_active                 = true;
};

struct InventoryItemRecord {
    int64_t     item_id          = 0;
    int64_t     category_id      = 0;
    std::string name;
    std::string description;
    std::string storage_location;
    int         quantity         = 0;
    int         unit_cost_cents  = 0;
    int64_t     expiration_date  = 0;    // 0 = no expiry
    std::string serial_number;           // empty = none
    std::string barcode;
    bool        is_active        = true;
    int64_t     created_at       = 0;    // IMMUTABLE
};

struct NewItemParams {
    int64_t     category_id      = 0;
    std::string name;
    std::string description;
    std::string storage_location;
    int         unit_cost_cents  = 0;
    int64_t     expiration_date  = 0;
    std::string serial_number;
    std::string barcode;
};

struct InboundRecord {
    int64_t     record_id       = 0;
    int64_t     item_id         = 0;
    int         quantity        = 0;
    int64_t     received_at     = 0;    // IMMUTABLE
    int64_t     received_by     = 0;
    std::string vendor;
    int         unit_cost_cents = 0;
    std::string lot_number;
    std::string notes;
};

struct OutboundRecord {
    int64_t     record_id   = 0;
    int64_t     item_id     = 0;
    int         quantity    = 0;
    int64_t     issued_at   = 0;    // IMMUTABLE
    int64_t     issued_by   = 0;
    std::string recipient;
    std::string reason;
    int64_t     booking_id  = 0;
    std::string notes;
};

struct AlertStateRecord {
    int64_t     alert_id        = 0;
    int64_t     item_id         = 0;
    std::string alert_type;
    int64_t     triggered_at    = 0;
    int64_t     acknowledged_at = 0;    // 0 = active
    int64_t     acknowledged_by = 0;
};

class InventoryRepository {
public:
    explicit InventoryRepository(infrastructure::Database& db);

    std::optional<InventoryItemRecord> FindItemById(int64_t item_id) const;
    std::optional<InventoryItemRecord> FindByBarcode(const std::string& barcode) const;
    std::optional<InventoryItemRecord> FindBySerial(const std::string& serial) const;

    // Returns all active inventory items for full-ledger views.
    std::vector<InventoryItemRecord> ListAllActiveItems() const;

    // Insert a new item. Returns item_id.
    int64_t InsertItem(const NewItemParams& params, int64_t now_unix);

    // Insert inbound record; increments item quantity. Returns record_id.
    int64_t InsertInbound(int64_t item_id, int quantity, const std::string& vendor,
                          const std::string& lot_number, int unit_cost_cents,
                          int64_t received_by, int64_t now_unix,
                          const std::string& notes = "");

    // Insert outbound record. Returns record_id. Does NOT modify quantity — caller
    // must call DecrementQuantity separately within the same transaction.
    int64_t InsertOutbound(int64_t item_id, int quantity, const std::string& recipient,
                           const std::string& reason, int64_t booking_id,
                           int64_t issued_by, int64_t now_unix,
                           const std::string& notes = "");
    bool IssueStockAtomic(int64_t item_id, int quantity, const std::string& recipient,
                          const std::string& reason, int64_t booking_id,
                          int64_t issued_by, int64_t issued_at,
                          int64_t period_date_midnight,
                          const std::string& notes = "");

    // Atomically decrement quantity. Throws if result would go below 0.
    void DecrementQuantity(int64_t item_id, int qty);

    // Upsert daily usage history (adds qty_delta to the day bucket for period_date).
    void UpsertUsageHistory(int64_t item_id, int64_t period_date_midnight, int qty_delta);

    // Returns usage history for computing average daily usage.
    std::vector<domain::DailyUsage> GetUsageHistory(int64_t item_id,
                                                      int64_t from_unix,
                                                      int64_t to_unix) const;

    // Returns items that may be low-stock (quantity > 0 still, caller computes).
    std::vector<InventoryItemRecord> ListLowStockCandidates() const;

    // Returns items with a non-NULL expiration_date.
    std::vector<InventoryItemRecord> ListExpirationCandidates() const;

    // Global search support over active inventory rows.
    std::vector<InventoryItemRecord> SearchByQuery(const std::string& query,
                                                   int limit = 200) const;

    // Alert state management.
    int64_t InsertAlertState(int64_t item_id, const std::string& alert_type,
                              int64_t now_unix);
    void AcknowledgeAlert(int64_t alert_id, int64_t user_id, int64_t now_unix);
    std::vector<AlertStateRecord> ListActiveAlerts() const;

    void MarkExpired(int64_t item_id, int64_t now_unix);

    void Anonymize(int64_t item_id, int64_t now_unix);

    struct RetentionCandidate {
        int64_t item_id;
        int64_t created_at;
        bool    already_anonymized;
    };
    std::vector<RetentionCandidate> ListRetentionCandidates(int64_t cutoff_unix) const;
    bool DeleteForRetention(int64_t item_id);

    // Convenience: build AlertCandidate list for AlertService::Scan.
    std::vector<domain::AlertCandidate> BuildAlertCandidates(
        int64_t now_unix, int window_days = 30) const;

    std::optional<CategoryRecord> FindCategoryById(int64_t category_id) const;
    int64_t InsertCategory(const std::string& name, const std::string& unit);

private:
    static InventoryItemRecord RowToItem(const std::vector<std::string>& vals);
    static AlertStateRecord RowToAlert(const std::vector<std::string>& vals);

    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
