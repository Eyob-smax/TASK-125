#include "shelterops/repositories/MaintenanceRepository.h"

namespace shelterops::repositories {

MaintenanceRepository::MaintenanceRepository(infrastructure::Database& db) : db_(db) {}

MaintenanceTicketRecord MaintenanceRepository::RowToTicket(
    const std::vector<std::string>& vals) {
    // Columns: ticket_id, zone_id, kennel_id, title, description, priority,
    //          status, created_at, created_by, assigned_to, first_action_at, resolved_at
    MaintenanceTicketRecord r;
    r.ticket_id       = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.zone_id         = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.kennel_id       = vals[2].empty() ? 0 : std::stoll(vals[2]);
    r.title           = vals[3];
    r.description     = vals[4];
    r.priority        = vals[5];
    r.status          = vals[6];
    r.created_at      = vals[7].empty() ? 0 : std::stoll(vals[7]);
    r.created_by      = vals[8].empty() ? 0 : std::stoll(vals[8]);
    r.assigned_to     = vals[9].empty() ? 0 : std::stoll(vals[9]);
    r.first_action_at = vals[10].empty() ? 0 : std::stoll(vals[10]);
    r.resolved_at     = vals[11].empty() ? 0 : std::stoll(vals[11]);
    return r;
}

MaintenanceEventRecord MaintenanceRepository::RowToEvent(
    const std::vector<std::string>& vals) {
    MaintenanceEventRecord r;
    r.event_id   = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.ticket_id  = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.actor_id   = vals[2].empty() ? 0 : std::stoll(vals[2]);
    r.event_type = vals[3];
    r.old_status = vals[4];
    r.new_status = vals[5];
    r.notes      = vals[6];
    r.occurred_at = vals[7].empty() ? 0 : std::stoll(vals[7]);
    return r;
}

int64_t MaintenanceRepository::InsertTicket(const NewTicketParams& params,
                                              int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO maintenance_tickets "
        "(zone_id, kennel_id, title, description, priority, status, created_at, created_by) "
        "VALUES (?, ?, ?, ?, ?, 'open', ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {
        params.zone_id > 0 ? std::to_string(params.zone_id) : "",
        params.kennel_id > 0 ? std::to_string(params.kennel_id) : "",
        params.title,
        params.description,
        params.priority,
        std::to_string(now_unix),
        std::to_string(params.created_by)
    });
    return conn->LastInsertRowId();
}

int64_t MaintenanceRepository::InsertEvent(int64_t ticket_id,
                                             int64_t actor_id,
                                             const std::string& event_type,
                                             const std::string& old_status,
                                             const std::string& new_status,
                                             const std::string& notes,
                                             int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO maintenance_events "
        "(ticket_id, actor_id, event_type, old_status, new_status, notes, occurred_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(ticket_id),
                     std::to_string(actor_id),
                     event_type,
                     old_status,
                     new_status,
                     notes,
                     std::to_string(now_unix)});
    return conn->LastInsertRowId();
}

void MaintenanceRepository::SetFirstActionAt(int64_t ticket_id, int64_t ts) {
    // Conditional UPDATE: only sets first_action_at when it is currently NULL.
    static const std::string sql =
        "UPDATE maintenance_tickets SET first_action_at = ? "
        "WHERE ticket_id = ? AND first_action_at IS NULL";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(ts), std::to_string(ticket_id)});
}

void MaintenanceRepository::SetResolvedAt(int64_t ticket_id, int64_t ts) {
    static const std::string sql =
        "UPDATE maintenance_tickets SET resolved_at = ? WHERE ticket_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(ts), std::to_string(ticket_id)});
}

void MaintenanceRepository::SetStatus(int64_t ticket_id, const std::string& status) {
    static const std::string sql =
        "UPDATE maintenance_tickets SET status = ? WHERE ticket_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {status, std::to_string(ticket_id)});
}

void MaintenanceRepository::SetAssignedTo(int64_t ticket_id, int64_t user_id) {
    static const std::string sql =
        "UPDATE maintenance_tickets SET assigned_to = ? WHERE ticket_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(user_id), std::to_string(ticket_id)});
}

std::optional<MaintenanceTicketRecord> MaintenanceRepository::FindById(
    int64_t ticket_id) const {
    static const std::string sql =
        "SELECT ticket_id, COALESCE(zone_id,0), COALESCE(kennel_id,0), "
        "       title, COALESCE(description,''), priority, status, "
        "       created_at, COALESCE(created_by,0), COALESCE(assigned_to,0), "
        "       COALESCE(first_action_at,0), COALESCE(resolved_at,0) "
        "FROM maintenance_tickets WHERE ticket_id = ?";
    std::optional<MaintenanceTicketRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(ticket_id)},
        [&](const auto&, const auto& vals) { result = RowToTicket(vals); });
    return result;
}

std::vector<MaintenanceTicketRecord> MaintenanceRepository::ListTicketsInRange(
    int64_t from_unix, int64_t to_unix) const {
    static const std::string sql =
        "SELECT ticket_id, COALESCE(zone_id,0), COALESCE(kennel_id,0), "
        "       title, COALESCE(description,''), priority, status, "
        "       created_at, COALESCE(created_by,0), COALESCE(assigned_to,0), "
        "       COALESCE(first_action_at,0), COALESCE(resolved_at,0) "
        "FROM maintenance_tickets WHERE created_at >= ? AND created_at <= ? "
        "ORDER BY created_at";
    std::vector<MaintenanceTicketRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(from_unix), std::to_string(to_unix)},
        [&](const auto&, const auto& vals) { result.push_back(RowToTicket(vals)); });
    return result;
}

std::vector<MaintenanceEventRecord> MaintenanceRepository::ListEventsFor(
    int64_t ticket_id) const {
    static const std::string sql =
        "SELECT event_id, ticket_id, actor_id, event_type, "
        "       COALESCE(old_status,''), COALESCE(new_status,''), "
        "       COALESCE(notes,''), occurred_at "
        "FROM maintenance_events WHERE ticket_id = ? ORDER BY occurred_at";
    std::vector<MaintenanceEventRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(ticket_id)},
        [&](const auto&, const auto& vals) { result.push_back(RowToEvent(vals)); });
    return result;
}

std::vector<domain::MaintenanceResponsePoint> MaintenanceRepository::GetResponsePoints(
    int64_t from_unix, int64_t to_unix) const {
    static const std::string sql =
        "SELECT ticket_id, created_at, first_action_at, resolved_at "
        "FROM maintenance_tickets WHERE created_at >= ? AND created_at <= ? "
        "ORDER BY created_at";
    std::vector<domain::MaintenanceResponsePoint> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(from_unix), std::to_string(to_unix)},
        [&](const auto&, const auto& vals) {
            domain::MaintenanceResponsePoint p;
            p.ticket_id  = vals[0].empty() ? 0 : std::stoll(vals[0]);
            p.created_at = vals[1].empty() ? 0 : std::stoll(vals[1]);
            if (!vals[2].empty()) p.first_action_at = std::stoll(vals[2]);
            if (!vals[3].empty()) p.resolved_at     = std::stoll(vals[3]);
            result.push_back(p);
        });
    return result;
}

std::vector<MaintenanceRepository::RetentionCandidate>
MaintenanceRepository::ListRetentionCandidates(int64_t cutoff_unix) const {
    static const std::string sql =
        "SELECT ticket_id, created_at, 0 "
        "FROM maintenance_tickets WHERE created_at < ?";
    std::vector<RetentionCandidate> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(cutoff_unix)},
        [&](const auto&, const auto& vals) {
            RetentionCandidate c;
            c.ticket_id          = vals[0].empty() ? 0 : std::stoll(vals[0]);
            c.created_at         = vals[1].empty() ? 0 : std::stoll(vals[1]);
            c.already_anonymized = false;
            result.push_back(c);
        });
    return result;
}

} // namespace shelterops::repositories
