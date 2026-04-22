#include "shelterops/repositories/AuditRepository.h"
#include <sstream>

namespace shelterops::repositories {

AuditRepository::AuditRepository(infrastructure::Database& db) : db_(db) {
    auto guard = db_.Acquire();
    auto ensure_column = [&](const char* sql) {
        try {
            guard->Exec(sql, {});
        } catch (...) {
            // Column already exists in newer schemas.
        }
    };
    ensure_column("ALTER TABLE audit_events ADD COLUMN actor_user_id INTEGER");
    ensure_column("ALTER TABLE audit_events ADD COLUMN actor_role TEXT");
    ensure_column("ALTER TABLE audit_events ADD COLUMN session_id TEXT");
}

AuditEvent AuditRepository::RowToEvent(const std::vector<std::string>& vals) {
    // 0:event_id 1:occurred_at 2:actor_user_id 3:actor_role 4:event_type
    // 5:entity_type 6:entity_id 7:description 8:session_id
    auto safeInt = [&](size_t i, int64_t def = 0) -> int64_t {
        return (i < vals.size() && !vals[i].empty()) ? std::stoll(vals[i]) : def;
    };
    AuditEvent e;
    e.occurred_at   = safeInt(1);
    e.actor_user_id = safeInt(2);
    e.actor_role    = vals.size() > 3 ? vals[3] : "";
    e.event_type    = vals.size() > 4 ? vals[4] : "";
    e.entity_type   = vals.size() > 5 ? vals[5] : "";
    e.entity_id     = safeInt(6);
    e.description   = vals.size() > 7 ? vals[7] : "";
    e.session_id    = vals.size() > 8 ? vals[8] : "";
    return e;
}

void AuditRepository::Append(const AuditEvent& event) {
    // This is the ONLY SQL statement issued against audit_events.
    // No UPDATE or DELETE is ever emitted from this class.
    auto guard = db_.Acquire();
    guard->Exec(
        "INSERT INTO audit_events"
        "(occurred_at, actor_user_id, actor_role, event_type,"
        " entity_type, entity_id, description, session_id)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        {std::to_string(event.occurred_at),
         event.actor_user_id > 0 ? std::to_string(event.actor_user_id) : "",
         event.actor_role,
         event.event_type,
         event.entity_type,
         event.entity_id > 0 ? std::to_string(event.entity_id) : "",
         event.description,
         event.session_id});
}

std::vector<AuditEvent> AuditRepository::Query(
        const AuditQueryFilter& filter) const {
    std::string sql =
        "SELECT event_id, occurred_at, COALESCE(actor_user_id,0), COALESCE(actor_role,''),"
        "       event_type, COALESCE(entity_type,''), COALESCE(entity_id,0),"
        "       description, COALESCE(session_id,'')"
        " FROM audit_events WHERE 1=1";

    std::vector<std::string> params;

    if (filter.from_unix > 0) {
        sql += " AND occurred_at >= ?";
        params.push_back(std::to_string(filter.from_unix));
    }
    if (filter.to_unix > 0) {
        sql += " AND occurred_at <= ?";
        params.push_back(std::to_string(filter.to_unix));
    }
    if (filter.actor_user_id > 0) {
        sql += " AND actor_user_id = ?";
        params.push_back(std::to_string(filter.actor_user_id));
    }
    if (!filter.event_type.empty()) {
        sql += " AND event_type = ?";
        params.push_back(filter.event_type);
    }
    if (!filter.entity_type.empty()) {
        sql += " AND entity_type = ?";
        params.push_back(filter.entity_type);
    }
    if (filter.entity_id > 0) {
        sql += " AND entity_id = ?";
        params.push_back(std::to_string(filter.entity_id));
    }

    sql += " ORDER BY occurred_at ASC LIMIT ? OFFSET ?";
    params.push_back(std::to_string(filter.limit));
    params.push_back(std::to_string(filter.offset));

    std::vector<AuditEvent> results;
    auto guard = db_.Acquire();
    guard->Query(sql, params,
                 [&results](const auto&, const auto& vals) {
                     results.push_back(RowToEvent(vals));
                 });
    return results;
}

void AuditRepository::ExportCsv(
        const AuditQueryFilter& filter,
        MaskFn masker,
        std::function<void(const std::string&)> line_callback) const {
    // Header
    line_callback("occurred_at,actor_user_id,actor_role,event_type,"
                  "entity_type,entity_id,description");

    auto events = Query(filter);
    for (const auto& e : events) {
        std::ostringstream row;
        row << e.occurred_at        << ","
            << e.actor_user_id      << ","
            << masker("actor_role",  e.actor_role)    << ","
            << e.event_type         << ","
            << e.entity_type        << ","
            << e.entity_id          << ","
            << masker("description", e.description);
        line_callback(row.str());
    }
}

} // namespace shelterops::repositories
