#pragma once
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"    // for UserContext
#include "shelterops/common/ErrorEnvelope.h"
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::services {

class MaintenanceService {
public:
    MaintenanceService(repositories::MaintenanceRepository& maintenance,
                       AuditService&                        audit);

    // Creates a ticket; created_at is immutable from now_unix. Returns ticket_id.
    int64_t CreateTicket(const repositories::NewTicketParams& params,
                         const UserContext& user_ctx,
                         int64_t now_unix);

    // Records an event. For action events (status_changed, assigned, resolved)
    // conditionally sets first_action_at when not yet set.
    common::ErrorEnvelope RecordEvent(int64_t ticket_id,
                                      const std::string& event_type,
                                      const std::string& new_status,
                                      const std::string& notes,
                                      const UserContext& user_ctx,
                                      int64_t now_unix);

    common::ErrorEnvelope Resolve(int64_t ticket_id,
                                  const std::string& notes,
                                  const UserContext& user_ctx,
                                  int64_t now_unix);

    common::ErrorEnvelope AssignTo(int64_t ticket_id, int64_t assignee_id,
                                   const UserContext& user_ctx,
                                   int64_t now_unix);

private:
    repositories::MaintenanceRepository& maintenance_;
    AuditService&                        audit_;
};

} // namespace shelterops::services
