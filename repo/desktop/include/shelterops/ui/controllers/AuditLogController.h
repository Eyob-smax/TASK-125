#pragma once
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuthorizationService.h"
#include "shelterops/services/UserContext.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <vector>
#include <string>
#include <cstdint>

namespace shelterops::ui::controllers {

enum class AuditLogState {
    Idle,
    Loading,
    Loaded,
    Exporting,
    ExportReady,
    Error
};

struct AuditLogFilter {
    int64_t     from_unix       = 0;
    int64_t     to_unix         = 0;
    std::string event_type;
    std::string entity_type;
    int64_t     actor_user_id   = 0;
    int         limit           = 250;
    int         offset          = 0;
};

// Controller for the Audit Log window.
// All reads are role-gated; Auditor role sees masked actor names.
// Append-only — no write path. Export produces masked CSV.
// Cross-platform: no ImGui dependency.
class AuditLogController {
public:
    explicit AuditLogController(repositories::AuditRepository& audit_repo);

    AuditLogState                          State()   const noexcept { return state_; }
    const std::vector<repositories::AuditEvent>& Events() const noexcept { return events_; }
    const common::ErrorEnvelope&           LastError() const noexcept { return last_error_; }
    bool                                   IsDirty() const noexcept { return is_dirty_; }

    AuditLogFilter& Filter() noexcept { return filter_; }
    const AuditLogFilter& Filter() const noexcept { return filter_; }

    // Load events matching the current filter. Requires AuditLogAccess role.
    void Refresh(const services::UserContext& ctx, int64_t now_unix);

    // Collect the full current result set as a masked CSV string.
    // PII fields are masked according to the caller role:
    //   Auditor → all actor names become initials.
    //   Others  → raw values.
    // Returns empty string on permission failure.
    std::string ExportCsv(const services::UserContext& ctx) const;

    void ClearDirty() noexcept { is_dirty_ = false; }
    void ClearError() noexcept { last_error_ = {}; state_ = AuditLogState::Idle; }

private:
    repositories::AuditRepository& audit_repo_;

    AuditLogState                       state_    = AuditLogState::Idle;
    AuditLogFilter                      filter_;
    std::vector<repositories::AuditEvent> events_;
    common::ErrorEnvelope               last_error_;
    bool                                is_dirty_ = false;
};

} // namespace shelterops::ui::controllers
