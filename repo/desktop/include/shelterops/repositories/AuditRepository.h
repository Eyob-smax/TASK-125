#pragma once
#include "shelterops/infrastructure/Database.h"
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace shelterops::repositories {

struct AuditEvent {
    int64_t     occurred_at    = 0;   // Unix timestamp
    int64_t     actor_user_id  = 0;   // 0 = system
    std::string actor_role;
    std::string event_type;           // e.g. "LOGIN", "BOOKING_CREATED"
    std::string entity_type;          // e.g. "user", "kennel"
    int64_t     entity_id      = 0;
    std::string description;          // no PII, no decrypted values
    std::string session_id;
};

struct AuditQueryFilter {
    int64_t     from_unix       = 0;
    int64_t     to_unix         = 0;    // 0 = no upper bound
    int64_t     actor_user_id   = 0;    // 0 = any
    std::string event_type;             // empty = any
    std::string entity_type;            // empty = any
    int64_t     entity_id       = 0;    // 0 = any
    int         limit           = 500;
    int         offset          = 0;
};

// Append-only audit log repository.
// No Update or Delete method is declared or implemented.
// Compile-time verification:
//   static_assert(!std::is_invocable_v<...>)  in test_audit_repository.cpp
class AuditRepository {
public:
    explicit AuditRepository(infrastructure::Database& db);

    // Insert one audit event. Never issues UPDATE or DELETE.
    void Append(const AuditEvent& event);

    // Read-only search. Returns events matching the filter in occurred_at ASC order.
    std::vector<AuditEvent> Query(const AuditQueryFilter& filter) const;

    // Streams masked CSV rows to callback(csv_line).
    // PII fields are masked according to the provided masking callback.
    using MaskFn = std::function<std::string(const std::string& field,
                                              const std::string& value)>;
    void ExportCsv(const AuditQueryFilter& filter,
                   MaskFn masker,
                   std::function<void(const std::string&)> line_callback) const;

private:
    static AuditEvent RowToEvent(const std::vector<std::string>& vals);
    infrastructure::Database& db_;
};

// Compile-time guard: AuditRepository must not expose Delete or Update.
// (Checked in test_audit_repository.cpp via negative std::is_invocable_v tests.)

} // namespace shelterops::repositories
