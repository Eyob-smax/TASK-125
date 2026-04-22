#include "shelterops/services/AdminService.h"

namespace shelterops::services {

AdminService::AdminService(repositories::AdminRepository& admin,
                            AuditService& audit)
    : admin_(admin), audit_(audit) {}

common::ErrorEnvelope AdminService::CreatePriceRule(
    const repositories::PriceRuleRecord& rule,
    const UserContext& user_ctx,
    int64_t now_unix) {

    auto denied = AuthorizationService::RequireAdminPanel(user_ctx.role);
    if (denied) return *denied;

    admin_.InsertPriceRule(rule);
    audit_.RecordSystemEvent("PRICE_RULE_CREATED",
        "Price rule '" + rule.name + "' created",
        now_unix);
    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

common::ErrorEnvelope AdminService::DeactivatePriceRule(
    int64_t rule_id, const UserContext& user_ctx, int64_t now_unix) {

    auto denied = AuthorizationService::RequireAdminPanel(user_ctx.role);
    if (denied) return *denied;

    admin_.SetPriceRuleActive(rule_id, false);
    audit_.RecordSystemEvent("PRICE_RULE_DEACTIVATED",
        "Price rule " + std::to_string(rule_id) + " deactivated",
        now_unix);
    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

common::ErrorEnvelope AdminService::UpdateCatalogEntry(
    int64_t entry_id, const std::string& name,
    int default_unit_cost_cents, bool is_active,
    const UserContext& user_ctx, int64_t now_unix) {

    auto denied = AuthorizationService::RequireAdminPanel(user_ctx.role);
    if (denied) return *denied;

    admin_.UpdateCatalogEntry(entry_id, name, default_unit_cost_cents, is_active);
    audit_.RecordSystemEvent("CATALOG_ENTRY_UPDATED",
        "Catalog entry " + std::to_string(entry_id) + " updated",
        now_unix);
    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

common::ErrorEnvelope AdminService::SetRetentionPolicy(
    const std::string& entity_type, int retention_years,
    domain::RetentionActionKind action,
    const UserContext& user_ctx, int64_t now_unix) {

    auto denied = AuthorizationService::RequireAdminPanel(user_ctx.role);
    if (denied) return *denied;

    admin_.UpsertRetentionPolicy(entity_type, retention_years, action,
                                  user_ctx.user_id, now_unix);
    audit_.RecordSystemEvent("RETENTION_POLICY_UPDATED",
        entity_type + " retention=" + std::to_string(retention_years) + "yr",
        now_unix);
    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

common::ErrorEnvelope AdminService::SetExportPermission(
    const std::string& role, const std::string& report_type,
    bool csv_allowed, bool pdf_allowed,
    const UserContext& user_ctx, int64_t now_unix) {

    auto denied = AuthorizationService::RequireAdminPanel(user_ctx.role);
    if (denied) return *denied;

    admin_.UpsertExportPermission(role, report_type, csv_allowed, pdf_allowed);

    audit_.RecordSystemEvent("EXPORT_PERMISSION_UPDATED",
        role + "/" + report_type + " csv=" + (csv_allowed ? "1" : "0") +
        " pdf=" + (pdf_allowed ? "1" : "0"),
        now_unix);
    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

} // namespace shelterops::services
