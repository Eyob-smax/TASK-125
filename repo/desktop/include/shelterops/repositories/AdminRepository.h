#pragma once
#include "shelterops/infrastructure/Database.h"
#include "shelterops/domain/Types.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::repositories {

struct CatalogEntryRecord {
    int64_t     entry_id                 = 0;
    std::string name;
    int64_t     category_id              = 0;
    int         default_unit_cost_cents  = 0;
    std::string vendor;
    std::string sku;
    bool        is_active                = true;
    int64_t     created_by               = 0;
    int64_t     created_at               = 0;    // IMMUTABLE
};

struct PriceRuleRecord {
    int64_t     rule_id           = 0;
    std::string name;
    std::string applies_to;
    std::string condition_json    = "{}";
    domain::PriceAdjustmentType adjustment_type = domain::PriceAdjustmentType::FixedDiscountCents;
    double      amount            = 0.0;
    int64_t     valid_from        = 0;
    int64_t     valid_to          = 0;
    bool        is_active         = true;
    int64_t     created_by        = 0;
    int64_t     created_at        = 0;    // IMMUTABLE
};

struct AfterSalesAdjustmentRecord {
    int64_t     adjustment_id = 0;
    int64_t     booking_id    = 0;
    int         amount_cents  = 0;
    std::string reason;
    int64_t     approved_by   = 0;
    int64_t     created_by    = 0;
    int64_t     created_at    = 0;    // IMMUTABLE
};

struct ConsentRecord {
    int64_t     consent_id   = 0;
    std::string entity_type;
    int64_t     entity_id    = 0;
    std::string consent_type;
    int64_t     given_at     = 0;    // IMMUTABLE
    int64_t     withdrawn_at = 0;
};

struct RetentionPolicyRecord {
    int64_t     policy_id       = 0;
    std::string entity_type;
    int         retention_years = 7;
    domain::RetentionActionKind action = domain::RetentionActionKind::Anonymize;
    int64_t     updated_by      = 0;
    int64_t     updated_at      = 0;
};

struct ExportPermissionRecord {
    int64_t     permission_id = 0;
    std::string role;
    std::string report_type;
    bool        csv_allowed   = false;
    bool        pdf_allowed   = false;
};

class AdminRepository {
public:
    explicit AdminRepository(infrastructure::Database& db);

    // Product catalog
    int64_t InsertCatalogEntry(const CatalogEntryRecord& rec);
    void UpdateCatalogEntry(int64_t entry_id, const std::string& name,
                             int default_unit_cost_cents, bool is_active);
    std::optional<CatalogEntryRecord> FindCatalogEntry(int64_t entry_id) const;

    // Price rules
    int64_t InsertPriceRule(const PriceRuleRecord& rec);
    void SetPriceRuleActive(int64_t rule_id, bool active);
    // Returns rules where is_active=true and now is within [valid_from, valid_to] (if set).
    std::vector<PriceRuleRecord> ListActivePriceRules(int64_t now_unix) const;

    // After-sales adjustments
    int64_t InsertAfterSalesAdjustment(int64_t booking_id, int amount_cents,
                                        const std::string& reason,
                                        int64_t approved_by, int64_t created_by,
                                        int64_t now_unix);

    // Consent records
    int64_t InsertConsent(const std::string& entity_type, int64_t entity_id,
                           const std::string& consent_type, int64_t now_unix);
    void WithdrawConsent(int64_t consent_id, int64_t now_unix);
    std::vector<ConsentRecord> ListConsentsFor(const std::string& entity_type,
                                                int64_t entity_id) const;

    // Retention policies
    std::vector<RetentionPolicyRecord> ListRetentionPolicies() const;
    void UpsertRetentionPolicy(const std::string& entity_type,
                                int retention_years,
                                domain::RetentionActionKind action,
                                int64_t updated_by, int64_t now_unix);

    // Export permissions
    std::vector<ExportPermissionRecord> ListExportPermissions(
        const std::string& role) const;
    bool CanExport(const std::string& role, const std::string& report_type,
                   const std::string& format) const;
    void UpsertExportPermission(const std::string& role,
                                 const std::string& report_type,
                                 bool csv_allowed, bool pdf_allowed);

    // System policies (key/value store)
    std::string GetPolicy(const std::string& key,
                           const std::string& default_val = "") const;
    void SetPolicy(const std::string& key, const std::string& value,
                   int64_t updated_by, int64_t now_unix);

private:
    static PriceRuleRecord          RowToPriceRule(const std::vector<std::string>& vals);
    static RetentionPolicyRecord    RowToRetentionPolicy(const std::vector<std::string>& vals);
    static ExportPermissionRecord   RowToExportPermission(const std::vector<std::string>& vals);
    static domain::PriceAdjustmentType ParseAdjType(const std::string& s);
    static domain::RetentionActionKind ParseRetentionAction(const std::string& s);

    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
