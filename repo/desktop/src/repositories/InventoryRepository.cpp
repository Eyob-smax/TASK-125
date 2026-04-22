#include "shelterops/repositories/InventoryRepository.h"
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace shelterops::repositories {

InventoryRepository::InventoryRepository(infrastructure::Database& db) : db_(db) {}

InventoryItemRecord InventoryRepository::RowToItem(const std::vector<std::string>& vals) {
    // Columns: item_id, category_id, name, description, storage_location,
    //          quantity, unit_cost_cents, expiration_date, serial_number,
    //          barcode, is_active, created_at
    InventoryItemRecord r;
    r.item_id          = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.category_id      = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.name             = vals[2];
    r.description      = vals[3];
    r.storage_location = vals[4];
    r.quantity         = vals[5].empty() ? 0 : std::stoi(vals[5]);
    r.unit_cost_cents  = vals[6].empty() ? 0 : std::stoi(vals[6]);
    r.expiration_date  = vals[7].empty() ? 0 : std::stoll(vals[7]);
    r.serial_number    = vals[8];
    r.barcode          = vals[9];
    r.is_active        = vals[10].empty() ? true : (vals[10] == "1");
    r.created_at       = vals[11].empty() ? 0 : std::stoll(vals[11]);
    return r;
}

AlertStateRecord InventoryRepository::RowToAlert(const std::vector<std::string>& vals) {
    AlertStateRecord r;
    r.alert_id        = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.item_id         = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.alert_type      = vals[2];
    r.triggered_at    = vals[3].empty() ? 0 : std::stoll(vals[3]);
    r.acknowledged_at = vals[4].empty() ? 0 : std::stoll(vals[4]);
    r.acknowledged_by = vals[5].empty() ? 0 : std::stoll(vals[5]);
    return r;
}

std::optional<InventoryItemRecord> InventoryRepository::FindItemById(int64_t item_id) const {
    static const std::string sql =
        "SELECT item_id, category_id, name, COALESCE(description,''), "
        "       COALESCE(storage_location,''), quantity, unit_cost_cents, "
        "       COALESCE(expiration_date,0), COALESCE(serial_number,''), "
        "       COALESCE(barcode,''), is_active, created_at "
        "FROM inventory_items WHERE item_id = ?";
    std::optional<InventoryItemRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(item_id)},
        [&](const auto&, const auto& vals) { result = RowToItem(vals); });
    return result;
}

std::optional<InventoryItemRecord> InventoryRepository::FindByBarcode(
    const std::string& barcode) const {
    static const std::string sql =
        "SELECT item_id, category_id, name, COALESCE(description,''), "
        "       COALESCE(storage_location,''), quantity, unit_cost_cents, "
        "       COALESCE(expiration_date,0), COALESCE(serial_number,''), "
        "       barcode, is_active, created_at "
        "FROM inventory_items WHERE barcode = ? AND is_active = 1";
    std::optional<InventoryItemRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {barcode},
        [&](const auto&, const auto& vals) { result = RowToItem(vals); });
    return result;
}

std::optional<InventoryItemRecord> InventoryRepository::FindBySerial(
    const std::string& serial) const {
    static const std::string sql =
        "SELECT item_id, category_id, name, COALESCE(description,''), "
        "       COALESCE(storage_location,''), quantity, unit_cost_cents, "
        "       COALESCE(expiration_date,0), serial_number, "
        "       COALESCE(barcode,''), is_active, created_at "
        "FROM inventory_items WHERE serial_number = ?";
    std::optional<InventoryItemRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {serial},
        [&](const auto&, const auto& vals) { result = RowToItem(vals); });
    return result;
}

std::vector<InventoryItemRecord> InventoryRepository::ListAllActiveItems() const {
    static const std::string sql =
        "SELECT item_id, category_id, name, COALESCE(description,''), "
        "       COALESCE(storage_location,''), quantity, unit_cost_cents, "
        "       COALESCE(expiration_date,0), COALESCE(serial_number,''), "
        "       COALESCE(barcode,''), is_active, created_at "
        "FROM inventory_items WHERE is_active = 1 ORDER BY item_id";

    std::vector<InventoryItemRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        result.push_back(RowToItem(vals));
    });
    return result;
}

int64_t InventoryRepository::InsertItem(const NewItemParams& params, int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO inventory_items "
        "(category_id, name, description, storage_location, quantity, "
        " unit_cost_cents, expiration_date, serial_number, barcode, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, 0, ?, ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {
        std::to_string(params.category_id),
        params.name,
        params.description,
        params.storage_location,
        std::to_string(params.unit_cost_cents),
        params.expiration_date > 0 ? std::to_string(params.expiration_date) : "",
        params.serial_number.empty() ? "" : params.serial_number,
        params.barcode,
        std::to_string(now_unix),
        std::to_string(now_unix)
    });
    return conn->LastInsertRowId();
}

int64_t InventoryRepository::InsertInbound(int64_t item_id, int quantity,
                                             const std::string& vendor,
                                             const std::string& lot_number,
                                             int unit_cost_cents,
                                             int64_t received_by,
                                             int64_t now_unix,
                                             const std::string& notes) {
    static const std::string ins_sql =
        "INSERT INTO inbound_records "
        "(item_id, quantity, received_at, received_by, vendor, unit_cost_cents, lot_number, notes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    static const std::string upd_sql =
        "UPDATE inventory_items SET quantity = quantity + ?, updated_at = ? WHERE item_id = ?";

    auto conn = db_.Acquire();
    conn->Exec("BEGIN", {});
    conn->Exec(ins_sql, {std::to_string(item_id),
                          std::to_string(quantity),
                          std::to_string(now_unix),
                          std::to_string(received_by),
                          vendor,
                          std::to_string(unit_cost_cents),
                          lot_number,
                          notes});
    int64_t rowid = conn->LastInsertRowId();
    conn->Exec(upd_sql, {std::to_string(quantity),
                          std::to_string(now_unix),
                          std::to_string(item_id)});
    conn->Exec("COMMIT", {});
    return rowid;
}

int64_t InventoryRepository::InsertOutbound(int64_t item_id, int quantity,
                                              const std::string& recipient,
                                              const std::string& reason,
                                              int64_t booking_id,
                                              int64_t issued_by,
                                              int64_t now_unix,
                                              const std::string& notes) {
    static const std::string sql =
        "INSERT INTO outbound_records "
        "(item_id, quantity, issued_at, issued_by, recipient, reason, booking_id, notes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(item_id),
                     std::to_string(quantity),
                     std::to_string(now_unix),
                     std::to_string(issued_by),
                     recipient,
                     reason,
                     booking_id > 0 ? std::to_string(booking_id) : "",
                     notes});
    return conn->LastInsertRowId();
}

bool InventoryRepository::IssueStockAtomic(int64_t item_id, int quantity,
                                           const std::string& recipient,
                                           const std::string& reason,
                                           int64_t booking_id,
                                           int64_t issued_by,
                                           int64_t issued_at,
                                           int64_t period_date_midnight,
                                           const std::string& notes) {
    static const std::string dec_sql =
        "UPDATE inventory_items "
        "SET quantity = quantity - ?, updated_at = ? "
        "WHERE item_id = ? AND quantity >= ?";
    static const std::string outbound_sql =
        "INSERT INTO outbound_records "
        "(item_id, quantity, issued_at, issued_by, recipient, reason, booking_id, notes) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    static const std::string usage_sql =
        "INSERT INTO item_usage_history (item_id, period_date, quantity_used) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(item_id, period_date) DO UPDATE SET "
        "  quantity_used = quantity_used + excluded.quantity_used";

    auto conn = db_.Acquire();
    try {
        conn->Exec("BEGIN IMMEDIATE", {});
        conn->Exec(dec_sql, {std::to_string(quantity),
                             std::to_string(issued_at),
                             std::to_string(item_id),
                             std::to_string(quantity)});
        if (conn->ChangeCount() == 0) {
            conn->Exec("ROLLBACK", {});
            return false;
        }
        conn->Exec(outbound_sql, {std::to_string(item_id),
                                  std::to_string(quantity),
                                  std::to_string(issued_at),
                                  std::to_string(issued_by),
                                  recipient,
                                  reason,
                                  booking_id > 0 ? std::to_string(booking_id) : "",
                                  notes});
        conn->Exec(usage_sql, {std::to_string(item_id),
                               std::to_string(period_date_midnight),
                               std::to_string(quantity)});
        conn->Exec("COMMIT", {});
        return true;
    } catch (...) {
        try { conn->Exec("ROLLBACK", {}); } catch (...) {}
        throw;
    }
}

void InventoryRepository::DecrementQuantity(int64_t item_id, int qty) {
    static const std::string check_sql =
        "SELECT quantity FROM inventory_items WHERE item_id = ?";
    static const std::string upd_sql =
        "UPDATE inventory_items SET quantity = quantity - ? WHERE item_id = ? AND quantity >= ?";

    auto conn = db_.Acquire();
    int current = 0;
    conn->Query(check_sql, {std::to_string(item_id)},
        [&](const auto&, const auto& vals) {
            current = vals[0].empty() ? 0 : std::stoi(vals[0]);
        });
    if (current < qty) {
        throw std::runtime_error("Insufficient quantity: cannot decrement below 0");
    }
    conn->Exec(upd_sql, {std::to_string(qty),
                          std::to_string(item_id),
                          std::to_string(qty)});
}

void InventoryRepository::UpsertUsageHistory(int64_t item_id,
                                               int64_t period_date_midnight,
                                               int qty_delta) {
    static const std::string sql =
        "INSERT INTO item_usage_history (item_id, period_date, quantity_used) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(item_id, period_date) DO UPDATE SET "
        "  quantity_used = quantity_used + excluded.quantity_used";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(item_id),
                     std::to_string(period_date_midnight),
                     std::to_string(qty_delta)});
}

std::vector<domain::DailyUsage> InventoryRepository::GetUsageHistory(
    int64_t item_id, int64_t from_unix, int64_t to_unix) const {
    static const std::string sql =
        "SELECT period_date, quantity_used FROM item_usage_history "
        "WHERE item_id = ? AND period_date >= ? AND period_date <= ? "
        "ORDER BY period_date";
    std::vector<domain::DailyUsage> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(item_id),
                      std::to_string(from_unix),
                      std::to_string(to_unix)},
        [&](const auto&, const auto& vals) {
            domain::DailyUsage u;
            u.period_date_unix = vals[0].empty() ? 0 : std::stoll(vals[0]);
            u.quantity_used    = vals[1].empty() ? 0 : std::stoi(vals[1]);
            result.push_back(u);
        });
    return result;
}

std::vector<InventoryItemRecord> InventoryRepository::ListLowStockCandidates() const {
    static const std::string sql =
        "SELECT item_id, category_id, name, COALESCE(description,''), "
        "       COALESCE(storage_location,''), quantity, unit_cost_cents, "
        "       COALESCE(expiration_date,0), COALESCE(serial_number,''), "
        "       COALESCE(barcode,''), is_active, created_at "
        "FROM inventory_items WHERE is_active = 1 ORDER BY item_id";
    std::vector<InventoryItemRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        result.push_back(RowToItem(vals));
    });
    return result;
}

std::vector<InventoryItemRecord> InventoryRepository::ListExpirationCandidates() const {
    static const std::string sql =
        "SELECT item_id, category_id, name, COALESCE(description,''), "
        "       COALESCE(storage_location,''), quantity, unit_cost_cents, "
        "       expiration_date, COALESCE(serial_number,''), "
        "       COALESCE(barcode,''), is_active, created_at "
        "FROM inventory_items WHERE expiration_date IS NOT NULL AND is_active = 1 "
        "ORDER BY expiration_date";
    std::vector<InventoryItemRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        result.push_back(RowToItem(vals));
    });
    return result;
}

std::vector<InventoryItemRecord> InventoryRepository::SearchByQuery(
    const std::string& query,
    int limit) const {
    static const std::string sql =
        "SELECT item_id, category_id, name, COALESCE(description,''), "
        "       COALESCE(storage_location,''), quantity, unit_cost_cents, "
        "       COALESCE(expiration_date,0), COALESCE(serial_number,''), "
        "       COALESCE(barcode,''), is_active, created_at "
        "FROM inventory_items "
        "WHERE is_active = 1 "
        "  AND (LOWER(name) LIKE LOWER(?) "
        "       OR LOWER(COALESCE(description,'')) LIKE LOWER(?) "
        "       OR LOWER(COALESCE(storage_location,'')) LIKE LOWER(?) "
        "       OR LOWER(COALESCE(serial_number,'')) LIKE LOWER(?) "
        "       OR LOWER(COALESCE(barcode,'')) LIKE LOWER(?) "
        "       OR CAST(item_id AS TEXT) LIKE ?) "
        "ORDER BY updated_at DESC LIMIT ?";

    const std::string q = "%" + query + "%";
    std::vector<InventoryItemRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {q, q, q, q, q, q, std::to_string(limit)},
        [&](const auto&, const auto& vals) {
            result.push_back(RowToItem(vals));
        });
    return result;
}

int64_t InventoryRepository::InsertAlertState(int64_t item_id,
                                                const std::string& alert_type,
                                                int64_t now_unix) {
    static const std::string sql =
        "INSERT OR IGNORE INTO alert_states (item_id, alert_type, triggered_at) "
        "VALUES (?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(item_id), alert_type, std::to_string(now_unix)});
    return conn->LastInsertRowId();
}

void InventoryRepository::AcknowledgeAlert(int64_t alert_id, int64_t user_id,
                                             int64_t now_unix) {
    static const std::string sql =
        "UPDATE alert_states SET acknowledged_at = ?, acknowledged_by = ? "
        "WHERE alert_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(now_unix),
                     std::to_string(user_id),
                     std::to_string(alert_id)});
}

std::vector<AlertStateRecord> InventoryRepository::ListActiveAlerts() const {
    static const std::string sql =
        "SELECT alert_id, item_id, alert_type, triggered_at, "
        "       COALESCE(acknowledged_at,0), COALESCE(acknowledged_by,0) "
        "FROM alert_states WHERE acknowledged_at IS NULL ORDER BY triggered_at";
    std::vector<AlertStateRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        result.push_back(RowToAlert(vals));
    });
    return result;
}

void InventoryRepository::MarkExpired(int64_t item_id, int64_t now_unix) {
    static const std::string sql =
        "UPDATE inventory_items SET is_active = 0, updated_at = ? WHERE item_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(now_unix), std::to_string(item_id)});
}

void InventoryRepository::Anonymize(int64_t item_id, int64_t now_unix) {
    static const std::string sql =
        "UPDATE inventory_items SET name='[anonymized]', description=NULL, "
        "  serial_number=NULL, barcode=NULL, updated_at=? WHERE item_id=?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(now_unix), std::to_string(item_id)});
}

std::vector<InventoryRepository::RetentionCandidate>
InventoryRepository::ListRetentionCandidates(int64_t cutoff_unix) const {
    static const std::string sql =
        "SELECT item_id, created_at, "
        "  CASE WHEN name='[anonymized]' THEN 1 ELSE 0 END "
        "FROM inventory_items WHERE created_at < ?";
    std::vector<RetentionCandidate> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(cutoff_unix)},
        [&](const auto&, const auto& vals) {
            RetentionCandidate c;
            c.item_id            = vals[0].empty() ? 0 : std::stoll(vals[0]);
            c.created_at         = vals[1].empty() ? 0 : std::stoll(vals[1]);
            c.already_anonymized = vals[2] == "1";
            result.push_back(c);
        });
    return result;
}

bool InventoryRepository::DeleteForRetention(int64_t item_id) {
    auto conn = db_.Acquire();
    try {
        conn->Exec("DELETE FROM item_usage_history WHERE item_id = ?",
                   {std::to_string(item_id)});
        conn->Exec("DELETE FROM inbound_records WHERE item_id = ?",
                   {std::to_string(item_id)});
        conn->Exec("DELETE FROM outbound_records WHERE item_id = ?",
                   {std::to_string(item_id)});
        conn->Exec("DELETE FROM alert_states WHERE item_id = ?",
                   {std::to_string(item_id)});
        conn->Exec("DELETE FROM inventory_items WHERE item_id = ?",
                   {std::to_string(item_id)});
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<domain::AlertCandidate> InventoryRepository::BuildAlertCandidates(
    int64_t now_unix, int window_days) const {
    // Load all active items and their current alert states.
    auto items    = ListLowStockCandidates();
    auto active   = ListActiveAlerts();

    // Build a set of (item_id, type) for already-active alerts.
    struct AlertKey { int64_t item_id; std::string type; };
    std::vector<AlertKey> active_keys;
    for (auto& a : active) {
        active_keys.push_back({a.item_id, a.alert_type});
    }

    auto already = [&](int64_t id, const std::string& type) {
        for (auto& k : active_keys) {
            if (k.item_id == id && k.type == type) return true;
        }
        return false;
    };

    const int64_t from = now_unix - static_cast<int64_t>(window_days) * 86400;

    std::vector<domain::AlertCandidate> result;
    for (auto& item : items) {
        domain::AlertCandidate c;
        c.item_id             = item.item_id;
        c.current_quantity    = item.quantity;
        c.expiration_unix     = item.expiration_date;
        c.already_alerted_low_stock  = already(item.item_id, "low_stock");
        c.already_alerted_expiring   = already(item.item_id, "expiring_soon");
        c.already_alerted_expired    = already(item.item_id, "expired");

        auto history = GetUsageHistory(item.item_id, from, now_unix);
        c.average_daily_usage = domain::ComputeAverageDailyUsage(history, now_unix, window_days);

        result.push_back(c);
    }
    return result;
}

std::optional<CategoryRecord> InventoryRepository::FindCategoryById(
    int64_t category_id) const {
    static const std::string sql =
        "SELECT category_id, name, unit, low_stock_threshold_days, "
        "       expiration_alert_days, is_active "
        "FROM inventory_categories WHERE category_id = ?";
    std::optional<CategoryRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(category_id)},
        [&](const auto&, const auto& vals) {
            CategoryRecord r;
            r.category_id              = vals[0].empty() ? 0 : std::stoll(vals[0]);
            r.name                     = vals[1];
            r.unit                     = vals[2];
            r.low_stock_threshold_days = vals[3].empty() ? 7 : std::stoi(vals[3]);
            r.expiration_alert_days    = vals[4].empty() ? 14 : std::stoi(vals[4]);
            r.is_active                = vals[5].empty() ? true : (vals[5] == "1");
            result = r;
        });
    return result;
}

int64_t InventoryRepository::InsertCategory(const std::string& name,
                                              const std::string& unit) {
    static const std::string sql =
        "INSERT INTO inventory_categories (name, unit) VALUES (?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {name, unit});
    return conn->LastInsertRowId();
}

} // namespace shelterops::repositories
