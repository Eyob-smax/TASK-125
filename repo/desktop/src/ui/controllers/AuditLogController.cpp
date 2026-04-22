#include "shelterops/ui/controllers/AuditLogController.h"
#include "shelterops/domain/RolePermissions.h"
#include <spdlog/spdlog.h>

namespace shelterops::ui::controllers {

AuditLogController::AuditLogController(repositories::AuditRepository& audit_repo)
    : audit_repo_(audit_repo)
{}

void AuditLogController::Refresh(
    const services::UserContext& ctx, int64_t /*now_unix*/)
{
    if (!domain::CanViewAuditLog(ctx.role)) {
        last_error_ = { common::ErrorCode::Forbidden, "Insufficient role for audit log" };
        state_ = AuditLogState::Error;
        return;
    }

    state_ = AuditLogState::Loading;
    events_.clear();

    repositories::AuditQueryFilter f;
    f.from_unix     = filter_.from_unix;
    f.to_unix       = filter_.to_unix;
    f.event_type    = filter_.event_type;
    f.entity_type   = filter_.entity_type;
    f.actor_user_id = filter_.actor_user_id;
    f.limit         = filter_.limit;
    f.offset        = filter_.offset;

    events_   = audit_repo_.Query(f);
    state_    = AuditLogState::Loaded;
    is_dirty_ = false;
    spdlog::debug("AuditLogController: loaded {} events", events_.size());
}

std::string AuditLogController::ExportCsv(
    const services::UserContext& ctx) const
{
    if (!domain::CanViewAuditLog(ctx.role))
        return {};

    std::string result;
    result.reserve(8192);

    // CSV header
    result += "occurred_at,actor_user_id,actor_role,event_type,"
              "entity_type,entity_id,description\r\n";

    repositories::AuditQueryFilter f;
    f.from_unix     = filter_.from_unix;
    f.to_unix       = filter_.to_unix;
    f.event_type    = filter_.event_type;
    f.entity_type   = filter_.entity_type;
    f.actor_user_id = filter_.actor_user_id;
    f.limit         = filter_.limit;
    f.offset        = filter_.offset;

    bool is_auditor = (ctx.role == domain::UserRole::Auditor);

    audit_repo_.ExportCsv(f,
        [is_auditor](const std::string& field, const std::string& value) -> std::string {
            if (is_auditor && (field == "actor_user_id" || field == "description"))
                return "[masked]";
            return value;
        },
        [&result](const std::string& line) {
            result += line;
            result += "\r\n";
        });

    return result;
}

} // namespace shelterops::ui::controllers
