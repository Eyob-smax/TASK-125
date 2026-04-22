#pragma once
#include "shelterops/infrastructure/Database.h"
#include <string>
#include <optional>

namespace shelterops::infrastructure {

struct CheckpointData {
    int64_t     saved_at        = 0;   // Unix timestamp
    std::string window_state;          // JSON
    std::string form_state;            // JSON (sanitized — no PII)
};

// Saves and restores the application's window/form state for crash recovery.
// Payload is stored in the `crash_checkpoints` SQLite table.
// Payloads containing PII-marker patterns (password, token, email, @) are
// rejected with a logged error — the checkpoint is not written in that case.
class CrashCheckpoint {
public:
    explicit CrashCheckpoint(Database& db);

    // Persist current layout. Returns false if PII check rejects the payload.
    bool SaveCheckpoint(const std::string& window_state_json,
                        const std::string& sanitized_form_state_json);

    // Returns the most recently saved checkpoint, if any.
    std::optional<CheckpointData> LoadLatest();

    // Removes all but the most recent `keep_n` checkpoints.
    void Trim(int keep_n = 1);

private:
    static bool ContainsPiiMarker(const std::string& json);

    Database& db_;
};

} // namespace shelterops::infrastructure
