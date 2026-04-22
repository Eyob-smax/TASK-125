#pragma once
#include "shelterops/services/AdminService.h"
#include "shelterops/services/BookingService.h"
#include "shelterops/services/ConsentService.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/ui/primitives/ValidationState.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::ui::controllers {

enum class AdminPanelState {
    Idle,
    LoadingCatalog,
    LoadedCatalog,
    LoadingPriceRules,
    LoadedPriceRules,
    LoadingApprovals,
    LoadedApprovals,
    LoadingRetention,
    LoadedRetention,
    LoadingExportPerms,
    LoadedExportPerms,
    Saving,
    SaveSuccess,
    Error
};

struct CatalogEditForm {
    int64_t     entry_id              = 0;
    std::string name;
    int         default_unit_cost_cents = 0;
    bool        is_active             = true;
};

struct PriceRuleEditForm {
    std::string name;
    std::string applies_to;
    std::string condition_json    = "{}";
    domain::PriceAdjustmentType adjustment_type =
        domain::PriceAdjustmentType::FixedDiscountCents;
    double      amount            = 0.0;
    int64_t     valid_from        = 0;
    int64_t     valid_to          = 0;
};

struct ApprovalQueueEntry {
    int64_t     approval_id   = 0;
    int64_t     booking_id    = 0;
    int64_t     requested_by  = 0;
    int64_t     requested_at  = 0;
    std::string decision;    // "" = pending
};

// Controller for the Admin Panel window.
// Manages product catalog, price rules, approval queues, retention policies,
// export permissions, and consent records.
// Cross-platform: no ImGui dependency.
class AdminPanelController {
public:
    AdminPanelController(services::AdminService&          admin_svc,
                          services::BookingService&        booking_svc,
                          services::ConsentService&        consent_svc,
                          repositories::AdminRepository&   admin_repo,
                          repositories::BookingRepository& booking_repo);

    AdminPanelState                        State()         const noexcept { return state_; }
    const common::ErrorEnvelope&           LastError()     const noexcept { return last_error_; }
    const primitives::ValidationState&     Validation()    const noexcept { return validation_; }
    bool                                   IsDirty()       const noexcept { return is_dirty_; }

    // Catalog
    const std::vector<repositories::CatalogEntryRecord>& CatalogEntries() const noexcept { return catalog_; }
    void LoadCatalog(int64_t now_unix);
    CatalogEditForm& CatalogForm() noexcept { return catalog_form_; }
    void BeginEditCatalogEntry(int64_t entry_id);
    bool SubmitCatalogEntry(const services::UserContext& ctx, int64_t now_unix);

    // Price rules
    const std::vector<repositories::PriceRuleRecord>& PriceRules() const noexcept { return price_rules_; }
    void LoadPriceRules(int64_t now_unix);
    PriceRuleEditForm& PriceRuleForm() noexcept { return price_rule_form_; }
    void BeginCreatePriceRule();
    bool SubmitPriceRule(const services::UserContext& ctx, int64_t now_unix);
    bool DeactivatePriceRule(int64_t rule_id, const services::UserContext& ctx, int64_t now_unix);

    // Approval queue
    const std::vector<ApprovalQueueEntry>& ApprovalQueue() const noexcept { return approval_queue_; }
    void LoadApprovalQueue(int64_t now_unix);
    bool ApproveBooking(int64_t booking_id, const services::UserContext& ctx, int64_t now_unix);
    bool RejectBooking(int64_t booking_id, const services::UserContext& ctx, int64_t now_unix);

    // Retention policies
    const std::vector<repositories::RetentionPolicyRecord>& RetentionPolicies() const noexcept { return retention_; }
    void LoadRetentionPolicies();
    bool SetRetentionPolicy(const std::string& entity_type, int years,
                             domain::RetentionActionKind action,
                             const services::UserContext& ctx, int64_t now_unix);

    // Export permissions
    const std::vector<repositories::ExportPermissionRecord>& ExportPermissions() const noexcept { return export_perms_; }
    void LoadExportPermissions(const std::string& role);
    bool SetExportPermission(const std::string& role, const std::string& report_type,
                              bool csv, bool pdf,
                              const services::UserContext& ctx, int64_t now_unix);

    void ClearDirty() noexcept { is_dirty_ = false; }
    void ClearError() noexcept { last_error_ = {}; state_ = AdminPanelState::Idle; }

private:
    services::AdminService&          admin_svc_;
    services::BookingService&        booking_svc_;
    services::ConsentService&        consent_svc_;
    repositories::AdminRepository&   admin_repo_;
    repositories::BookingRepository& booking_repo_;

    AdminPanelState                              state_        = AdminPanelState::Idle;
    common::ErrorEnvelope                        last_error_;
    primitives::ValidationState                  validation_;
    bool                                         is_dirty_     = false;

    std::vector<repositories::CatalogEntryRecord>    catalog_;
    std::vector<repositories::PriceRuleRecord>       price_rules_;
    std::vector<ApprovalQueueEntry>                  approval_queue_;
    std::vector<repositories::RetentionPolicyRecord> retention_;
    std::vector<repositories::ExportPermissionRecord> export_perms_;

    CatalogEditForm                  catalog_form_;
    PriceRuleEditForm                price_rule_form_;
};

} // namespace shelterops::ui::controllers
