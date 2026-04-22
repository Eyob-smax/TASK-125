#pragma once
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/domain/PriceRuleEngine.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"    // for UserContext
#include "shelterops/common/ErrorEnvelope.h"
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::services {

class AdminService {
public:
    AdminService(repositories::AdminRepository& admin,
                 AuditService&                  audit);

    // All methods require Administrator role; return FORBIDDEN if not met.
    common::ErrorEnvelope CreatePriceRule(const repositories::PriceRuleRecord& rule,
                                          const UserContext& user_ctx,
                                          int64_t now_unix);

    common::ErrorEnvelope DeactivatePriceRule(int64_t rule_id,
                                               const UserContext& user_ctx,
                                               int64_t now_unix);

    common::ErrorEnvelope UpdateCatalogEntry(int64_t entry_id,
                                              const std::string& name,
                                              int default_unit_cost_cents,
                                              bool is_active,
                                              const UserContext& user_ctx,
                                              int64_t now_unix);

    common::ErrorEnvelope SetRetentionPolicy(const std::string& entity_type,
                                              int retention_years,
                                              domain::RetentionActionKind action,
                                              const UserContext& user_ctx,
                                              int64_t now_unix);

    common::ErrorEnvelope SetExportPermission(const std::string& role,
                                               const std::string& report_type,
                                               bool csv_allowed,
                                               bool pdf_allowed,
                                               const UserContext& user_ctx,
                                               int64_t now_unix);

private:
    repositories::AdminRepository& admin_;
    AuditService&                  audit_;
};

} // namespace shelterops::services
