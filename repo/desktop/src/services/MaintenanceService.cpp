#include "shelterops/services/MaintenanceService.h"

namespace shelterops::services {

MaintenanceService::MaintenanceService(
    repositories::MaintenanceRepository& maintenance,
    AuditService& audit)
    : maintenance_(maintenance), audit_(audit) {}

int64_t MaintenanceService::CreateTicket(const repositories::NewTicketParams& params,
                                          const UserContext& user_ctx,
                                          int64_t now_unix) {
    int64_t ticket_id = maintenance_.InsertTicket(params, now_unix);

    // Record 'created' event.
    maintenance_.InsertEvent(ticket_id, user_ctx.user_id,
                              "created", "", "open", "", now_unix);

    audit_.RecordSystemEvent("MAINTENANCE_TICKET_CREATED",
        "Ticket " + std::to_string(ticket_id) + ": " + params.title,
        now_unix);

    return ticket_id;
}

common::ErrorEnvelope MaintenanceService::RecordEvent(
    int64_t ticket_id, const std::string& event_type,
    const std::string& new_status, const std::string& notes,
    const UserContext& user_ctx, int64_t now_unix) {

    auto ticket = maintenance_.FindById(ticket_id);
    if (!ticket) {
        return common::ErrorEnvelope{common::ErrorCode::NotFound,
                                      "Maintenance ticket not found"};
    }

    const std::string old_status = ticket->status;

    maintenance_.InsertEvent(ticket_id, user_ctx.user_id,
                              event_type, old_status, new_status, notes, now_unix);

    // Conditionally set first_action_at for action events.
    if (event_type == "status_changed" ||
        event_type == "assigned"       ||
        event_type == "resolved") {
        if (ticket->first_action_at == 0) {
            maintenance_.SetFirstActionAt(ticket_id, now_unix);
        }
    }

    if (!new_status.empty() && new_status != old_status) {
        maintenance_.SetStatus(ticket_id, new_status);
    }

    audit_.RecordSystemEvent("MAINTENANCE_EVENT",
        "Ticket " + std::to_string(ticket_id) +
        " event: " + event_type,
        now_unix);

    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

common::ErrorEnvelope MaintenanceService::Resolve(int64_t ticket_id,
                                                   const std::string& notes,
                                                   const UserContext& user_ctx,
                                                   int64_t now_unix) {
    auto result = RecordEvent(ticket_id, "resolved", "resolved", notes,
                               user_ctx, now_unix);
    if (!result.message.empty() && result.code != common::ErrorCode::Internal) {
        return result;
    }
    maintenance_.SetResolvedAt(ticket_id, now_unix);
    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

common::ErrorEnvelope MaintenanceService::AssignTo(int64_t ticket_id,
                                                    int64_t assignee_id,
                                                    const UserContext& user_ctx,
                                                    int64_t now_unix) {
    auto ticket = maintenance_.FindById(ticket_id);
    if (!ticket) {
        return common::ErrorEnvelope{common::ErrorCode::NotFound,
                                      "Maintenance ticket not found"};
    }

    maintenance_.SetAssignedTo(ticket_id, assignee_id);
    maintenance_.InsertEvent(ticket_id, user_ctx.user_id,
                              "assigned", ticket->status, ticket->status,
                              "Assigned to user " + std::to_string(assignee_id),
                              now_unix);

    // Conditionally set first_action_at.
    if (ticket->first_action_at == 0) {
        maintenance_.SetFirstActionAt(ticket_id, now_unix);
    }

    audit_.RecordSystemEvent("MAINTENANCE_ASSIGNED",
        "Ticket " + std::to_string(ticket_id) +
        " assigned to user " + std::to_string(assignee_id),
        now_unix);

    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

} // namespace shelterops::services
