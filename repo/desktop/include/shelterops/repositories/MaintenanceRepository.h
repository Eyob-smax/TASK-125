#pragma once
#include "shelterops/infrastructure/Database.h"
#include "shelterops/domain/ReportPipeline.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::repositories {

struct MaintenanceTicketRecord {
    int64_t     ticket_id       = 0;
    int64_t     zone_id         = 0;
    int64_t     kennel_id       = 0;
    std::string title;
    std::string description;
    std::string priority;
    std::string status;
    int64_t     created_at      = 0;    // IMMUTABLE
    int64_t     created_by      = 0;
    int64_t     assigned_to     = 0;
    int64_t     first_action_at = 0;    // 0 = not yet set
    int64_t     resolved_at     = 0;
};

struct MaintenanceEventRecord {
    int64_t     event_id    = 0;
    int64_t     ticket_id   = 0;
    int64_t     actor_id    = 0;
    std::string event_type;
    std::string old_status;
    std::string new_status;
    std::string notes;
    int64_t     occurred_at = 0;    // IMMUTABLE
};

struct NewTicketParams {
    int64_t     zone_id     = 0;
    int64_t     kennel_id   = 0;
    std::string title;
    std::string description;
    std::string priority    = "normal";
    int64_t     created_by  = 0;
};

class MaintenanceRepository {
public:
    explicit MaintenanceRepository(infrastructure::Database& db);

    // Insert a new ticket. created_at set from now_unix. Returns ticket_id.
    int64_t InsertTicket(const NewTicketParams& params, int64_t now_unix);

    // Insert an event for a ticket. occurred_at set from now_unix.
    int64_t InsertEvent(int64_t ticket_id, int64_t actor_id,
                        const std::string& event_type,
                        const std::string& old_status,
                        const std::string& new_status,
                        const std::string& notes,
                        int64_t now_unix);

    // Sets first_action_at only when the current value IS NULL.
    // Safe to call multiple times; subsequent calls are no-ops.
    void SetFirstActionAt(int64_t ticket_id, int64_t ts);

    void SetResolvedAt(int64_t ticket_id, int64_t ts);
    void SetStatus(int64_t ticket_id, const std::string& status);
    void SetAssignedTo(int64_t ticket_id, int64_t user_id);

    std::optional<MaintenanceTicketRecord> FindById(int64_t ticket_id) const;

    // Returns tickets with created_at in [from_unix, to_unix].
    std::vector<MaintenanceTicketRecord> ListTicketsInRange(
        int64_t from_unix, int64_t to_unix) const;

    // Returns all events for a ticket, ordered by occurred_at ASC.
    std::vector<MaintenanceEventRecord> ListEventsFor(int64_t ticket_id) const;

    // Returns response-time data points for the report pipeline.
    std::vector<domain::MaintenanceResponsePoint> GetResponsePoints(
        int64_t from_unix, int64_t to_unix) const;

    struct RetentionCandidate {
        int64_t ticket_id;
        int64_t created_at;
        bool    already_anonymized;
    };
    std::vector<RetentionCandidate> ListRetentionCandidates(int64_t cutoff_unix) const;

private:
    static MaintenanceTicketRecord RowToTicket(const std::vector<std::string>& vals);
    static MaintenanceEventRecord  RowToEvent(const std::vector<std::string>& vals);

    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
