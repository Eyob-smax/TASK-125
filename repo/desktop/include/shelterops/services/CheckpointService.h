#pragma once
#include "shelterops/infrastructure/CrashCheckpoint.h"
#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace shelterops::services {

// Describes which windows are open and their current state.
struct WindowInventory {
    std::vector<std::string> open_window_ids;   // e.g. "kennel_board", "item_ledger"
    std::string              active_window_id;
};

// Unsaved form and filter state for active windows.
// Must not contain PII (enforced by CrashCheckpoint::ContainsPiiMarker).
struct FormSnapshot {
    std::string window_id;
    std::string filter_json;        // active filter state
    std::string selected_row_key;   // e.g. serialized row ID
    std::string draft_text;         // unsaved text fields (non-PII)
};

struct CheckpointPayload {
    WindowInventory           windows;
    std::vector<FormSnapshot> forms;
    int64_t                   saved_at = 0;
};

// Wraps CrashCheckpoint with structured payload serialization.
// PII scrubber in CrashCheckpoint rejects payloads containing password/token/email markers.
class CheckpointService {
public:
    explicit CheckpointService(infrastructure::CrashCheckpoint& checkpoint);

    // Serialize and save the current UI state. Returns false if PII guard rejects.
    bool CaptureState(const WindowInventory& windows,
                      const std::vector<FormSnapshot>& forms,
                      int64_t now_unix);

    // Returns the last saved checkpoint, if any.
    std::optional<CheckpointPayload> RestoreState();

private:
    infrastructure::CrashCheckpoint& checkpoint_;
};

} // namespace shelterops::services
