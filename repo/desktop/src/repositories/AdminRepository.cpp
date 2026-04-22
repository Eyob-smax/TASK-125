#include "shelterops/repositories/AdminRepository.h"

namespace shelterops::repositories {

AdminRepository::AdminRepository(infrastructure::Database& db) : db_(db) {}

domain::PriceAdjustmentType AdminRepository::ParseAdjType(const std::string& s) {
    if (s == "fixed_discount_cents")  return domain::PriceAdjustmentType::FixedDiscountCents;
    if (s == "percent_discount")      return domain::PriceAdjustmentType::PercentDiscount;
    if (s == "fixed_surcharge_cents") return domain::PriceAdjustmentType::FixedSurchargeCents;
    return domain::PriceAdjustmentType::PercentSurcharge;
}

domain::RetentionActionKind AdminRepository::ParseRetentionAction(const std::string& s) {
    if (s == "delete") return domain::RetentionActionKind::Delete;
    return domain::RetentionActionKind::Anonymize;
}

PriceRuleRecord AdminRepository::RowToPriceRule(const std::vector<std::string>& vals) {
    PriceRuleRecord r;
    r.rule_id         = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.name            = vals[1];
    r.applies_to      = vals[2];
    r.condition_json  = vals[3].empty() ? "{}" : vals[3];
    r.adjustment_type = ParseAdjType(vals[4]);
    r.amount          = vals[5].empty() ? 0.0 : std::stod(vals[5]);
    r.valid_from      = vals[6].empty() ? 0 : std::stoll(vals[6]);
    r.valid_to        = vals[7].empty() ? 0 : std::stoll(vals[7]);
    r.is_active       = vals[8].empty() ? true : (vals[8] == "1");
    r.created_by      = vals[9].empty() ? 0 : std::stoll(vals[9]);
    r.created_at      = vals[10].empty() ? 0 : std::stoll(vals[10]);
    return r;
}

RetentionPolicyRecord AdminRepository::RowToRetentionPolicy(
    const std::vector<std::string>& vals) {
    RetentionPolicyRecord r;
    r.policy_id       = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.entity_type     = vals[1];
    r.retention_years = vals[2].empty() ? 7 : std::stoi(vals[2]);
    r.action          = ParseRetentionAction(vals[3]);
    r.updated_by      = vals[4].empty() ? 0 : std::stoll(vals[4]);
    r.updated_at      = vals[5].empty() ? 0 : std::stoll(vals[5]);
    return r;
}

ExportPermissionRecord AdminRepository::RowToExportPermission(
    const std::vector<std::string>& vals) {
    ExportPermissionRecord r;
    r.permission_id = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.role          = vals[1];
    r.report_type   = vals[2];
    r.csv_allowed   = vals[3] == "1";
    r.pdf_allowed   = vals[4] == "1";
    return r;
}

// ---------------------------------------------------------------------------
// Product catalog
// ---------------------------------------------------------------------------

int64_t AdminRepository::InsertCatalogEntry(const CatalogEntryRecord& rec) {
    static const std::string sql =
        "INSERT INTO product_catalog "
        "(name, category_id, default_unit_cost_cents, vendor, sku, is_active, "
        " created_by, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {rec.name,
                     std::to_string(rec.category_id),
                     std::to_string(rec.default_unit_cost_cents),
                     rec.vendor, rec.sku,
                     rec.is_active ? "1" : "0",
                     std::to_string(rec.created_by),
                     std::to_string(rec.created_at)});
    return conn->LastInsertRowId();
}

void AdminRepository::UpdateCatalogEntry(int64_t entry_id, const std::string& name,
                                          int default_unit_cost_cents, bool is_active) {
    static const std::string sql =
        "UPDATE product_catalog SET name=?, default_unit_cost_cents=?, is_active=? "
        "WHERE entry_id=?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {name,
                     std::to_string(default_unit_cost_cents),
                     is_active ? "1" : "0",
                     std::to_string(entry_id)});
}

std::optional<CatalogEntryRecord> AdminRepository::FindCatalogEntry(
    int64_t entry_id) const {
    static const std::string sql =
        "SELECT entry_id, name, COALESCE(category_id,0), default_unit_cost_cents, "
        "       COALESCE(vendor,''), COALESCE(sku,''), is_active, "
        "       COALESCE(created_by,0), created_at "
        "FROM product_catalog WHERE entry_id = ?";
    std::optional<CatalogEntryRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(entry_id)},
        [&](const auto&, const auto& vals) {
            CatalogEntryRecord r;
            r.entry_id                = vals[0].empty() ? 0 : std::stoll(vals[0]);
            r.name                    = vals[1];
            r.category_id             = vals[2].empty() ? 0 : std::stoll(vals[2]);
            r.default_unit_cost_cents = vals[3].empty() ? 0 : std::stoi(vals[3]);
            r.vendor                  = vals[4];
            r.sku                     = vals[5];
            r.is_active               = vals[6].empty() ? true : (vals[6] == "1");
            r.created_by              = vals[7].empty() ? 0 : std::stoll(vals[7]);
            r.created_at              = vals[8].empty() ? 0 : std::stoll(vals[8]);
            result = r;
        });
    return result;
}

// ---------------------------------------------------------------------------
// Price rules
// ---------------------------------------------------------------------------

int64_t AdminRepository::InsertPriceRule(const PriceRuleRecord& rec) {
    static const std::string sql =
        "INSERT INTO price_rules "
        "(name, applies_to, condition_json, adjustment_type, amount, "
        " valid_from, valid_to, is_active, created_by, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    auto adj_type_str = [](domain::PriceAdjustmentType t) -> std::string {
        switch (t) {
        case domain::PriceAdjustmentType::FixedDiscountCents:  return "fixed_discount_cents";
        case domain::PriceAdjustmentType::PercentDiscount:     return "percent_discount";
        case domain::PriceAdjustmentType::FixedSurchargeCents: return "fixed_surcharge_cents";
        default:                                                return "percent_surcharge";
        }
    };

    auto conn = db_.Acquire();
    conn->Exec(sql, {rec.name, rec.applies_to, rec.condition_json,
                     adj_type_str(rec.adjustment_type),
                     std::to_string(rec.amount),
                     std::to_string(rec.valid_from),
                     std::to_string(rec.valid_to),
                     rec.is_active ? "1" : "0",
                     std::to_string(rec.created_by),
                     std::to_string(rec.created_at)});
    return conn->LastInsertRowId();
}

void AdminRepository::SetPriceRuleActive(int64_t rule_id, bool active) {
    static const std::string sql =
        "UPDATE price_rules SET is_active = ? WHERE rule_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {active ? "1" : "0", std::to_string(rule_id)});
}

std::vector<PriceRuleRecord> AdminRepository::ListActivePriceRules(
    int64_t now_unix) const {
    static const std::string sql =
        "SELECT rule_id, name, applies_to, COALESCE(condition_json,'{}'), "
        "       adjustment_type, amount, "
        "       COALESCE(valid_from,0), COALESCE(valid_to,0), is_active, "
        "       COALESCE(created_by,0), created_at "
        "FROM price_rules "
        "WHERE is_active = 1 "
        "  AND (valid_from IS NULL OR valid_from <= ?) "
        "  AND (valid_to   IS NULL OR valid_to = 0 OR valid_to >= ?) "
        "ORDER BY rule_id";
    std::vector<PriceRuleRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(now_unix), std::to_string(now_unix)},
        [&](const auto&, const auto& vals) { result.push_back(RowToPriceRule(vals)); });
    return result;
}

// ---------------------------------------------------------------------------
// After-sales adjustments
// ---------------------------------------------------------------------------

int64_t AdminRepository::InsertAfterSalesAdjustment(int64_t booking_id,
                                                      int amount_cents,
                                                      const std::string& reason,
                                                      int64_t approved_by,
                                                      int64_t created_by,
                                                      int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO after_sales_adjustments "
        "(booking_id, amount_cents, reason, approved_by, created_by, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(booking_id),
                     std::to_string(amount_cents),
                     reason,
                     std::to_string(approved_by),
                     std::to_string(created_by),
                     std::to_string(now_unix)});
    return conn->LastInsertRowId();
}

// ---------------------------------------------------------------------------
// Consent records
// ---------------------------------------------------------------------------

int64_t AdminRepository::InsertConsent(const std::string& entity_type,
                                        int64_t entity_id,
                                        const std::string& consent_type,
                                        int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO consent_records "
        "(entity_type, entity_id, consent_type, given_at) VALUES (?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {entity_type, std::to_string(entity_id),
                     consent_type, std::to_string(now_unix)});
    return conn->LastInsertRowId();
}

void AdminRepository::WithdrawConsent(int64_t consent_id, int64_t now_unix) {
    static const std::string sql =
        "UPDATE consent_records SET withdrawn_at = ? WHERE consent_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(now_unix), std::to_string(consent_id)});
}

std::vector<ConsentRecord> AdminRepository::ListConsentsFor(
    const std::string& entity_type, int64_t entity_id) const {
    static const std::string sql =
        "SELECT consent_id, entity_type, entity_id, consent_type, "
        "       given_at, COALESCE(withdrawn_at,0) "
        "FROM consent_records WHERE entity_type = ? AND entity_id = ?";
    std::vector<ConsentRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {entity_type, std::to_string(entity_id)},
        [&](const auto&, const auto& vals) {
            ConsentRecord r;
            r.consent_id   = vals[0].empty() ? 0 : std::stoll(vals[0]);
            r.entity_type  = vals[1];
            r.entity_id    = vals[2].empty() ? 0 : std::stoll(vals[2]);
            r.consent_type = vals[3];
            r.given_at     = vals[4].empty() ? 0 : std::stoll(vals[4]);
            r.withdrawn_at = vals[5].empty() ? 0 : std::stoll(vals[5]);
            result.push_back(r);
        });
    return result;
}

// ---------------------------------------------------------------------------
// Retention policies
// ---------------------------------------------------------------------------

std::vector<RetentionPolicyRecord> AdminRepository::ListRetentionPolicies() const {
    static const std::string sql =
        "SELECT policy_id, entity_type, retention_years, action, "
        "       COALESCE(updated_by,0), updated_at "
        "FROM retention_policies ORDER BY entity_type";
    std::vector<RetentionPolicyRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {}, [&](const auto&, const auto& vals) {
        result.push_back(RowToRetentionPolicy(vals));
    });
    return result;
}

void AdminRepository::UpsertRetentionPolicy(const std::string& entity_type,
                                              int retention_years,
                                              domain::RetentionActionKind action,
                                              int64_t updated_by,
                                              int64_t now_unix) {
    auto action_str = [&]() -> std::string {
        return action == domain::RetentionActionKind::Delete ? "delete" : "anonymize";
    };
    static const std::string sql =
        "INSERT INTO retention_policies "
        "(entity_type, retention_years, action, updated_by, updated_at) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(entity_type) DO UPDATE SET "
        "  retention_years=excluded.retention_years, "
        "  action=excluded.action, "
        "  updated_by=excluded.updated_by, "
        "  updated_at=excluded.updated_at";
    auto conn = db_.Acquire();
    conn->Exec(sql, {entity_type,
                     std::to_string(retention_years),
                     action_str(),
                     std::to_string(updated_by),
                     std::to_string(now_unix)});
}

// ---------------------------------------------------------------------------
// Export permissions
// ---------------------------------------------------------------------------

std::vector<ExportPermissionRecord> AdminRepository::ListExportPermissions(
    const std::string& role) const {
    static const std::string sql =
        "SELECT permission_id, role, report_type, csv_allowed, pdf_allowed "
        "FROM export_permissions WHERE role = ?";
    std::vector<ExportPermissionRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {role}, [&](const auto&, const auto& vals) {
        result.push_back(RowToExportPermission(vals));
    });
    return result;
}

void AdminRepository::UpsertExportPermission(const std::string& role,
                                              const std::string& report_type,
                                              bool csv_allowed, bool pdf_allowed) {
    static const std::string sql =
        "INSERT INTO export_permissions (role, report_type, csv_allowed, pdf_allowed) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(role, report_type) DO UPDATE SET "
        "  csv_allowed=excluded.csv_allowed, pdf_allowed=excluded.pdf_allowed";
    auto conn = db_.Acquire();
    conn->Exec(sql, {role, report_type,
                     csv_allowed ? "1" : "0",
                     pdf_allowed ? "1" : "0"});
}

bool AdminRepository::CanExport(const std::string& role,
                                 const std::string& report_type,
                                 const std::string& format) const {
    static const std::string sql =
        "SELECT csv_allowed, pdf_allowed FROM export_permissions "
        "WHERE role = ? AND report_type = ?";
    bool allowed = false;
    auto conn = db_.Acquire();
    conn->Query(sql, {role, report_type},
        [&](const auto&, const auto& vals) {
            if (format == "csv") allowed = vals[0] == "1";
            else if (format == "pdf") allowed = vals[1] == "1";
        });
    return allowed;
}

// ---------------------------------------------------------------------------
// System policies
// ---------------------------------------------------------------------------

std::string AdminRepository::GetPolicy(const std::string& key,
                                        const std::string& default_val) const {
    static const std::string sql =
        "SELECT value FROM system_policies WHERE key = ?";
    std::string result = default_val;
    auto conn = db_.Acquire();
    conn->Query(sql, {key},
        [&](const auto&, const auto& vals) { if (!vals[0].empty()) result = vals[0]; });
    return result;
}

void AdminRepository::SetPolicy(const std::string& key, const std::string& value,
                                  int64_t updated_by, int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO system_policies (key, value, updated_by, updated_at) VALUES (?, ?, ?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value, "
        "  updated_by=excluded.updated_by, updated_at=excluded.updated_at";
    auto conn = db_.Acquire();
    conn->Exec(sql, {key, value,
                     std::to_string(updated_by),
                     std::to_string(now_unix)});
}

} // namespace shelterops::repositories
