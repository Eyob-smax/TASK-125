#include "shelterops/services/CheckpointService.h"
#include <nlohmann/json.hpp>

namespace shelterops::services {

CheckpointService::CheckpointService(infrastructure::CrashCheckpoint& checkpoint)
    : checkpoint_(checkpoint) {}

bool CheckpointService::CaptureState(const WindowInventory& windows,
                                      const std::vector<FormSnapshot>& forms,
                                      int64_t now_unix) {
    nlohmann::json win_json;
    win_json["open_window_ids"] = windows.open_window_ids;
    win_json["active_window_id"] = windows.active_window_id;

    nlohmann::json forms_json = nlohmann::json::array();
    for (const auto& f : forms) {
        nlohmann::json fj;
        fj["window_id"]       = f.window_id;
        fj["filter_json"]     = f.filter_json;
        fj["selected_row_key"] = f.selected_row_key;
        fj["draft_text"]      = f.draft_text;
        forms_json.push_back(fj);
    }

    nlohmann::json form_state_json;
    form_state_json["saved_at"] = now_unix;
    form_state_json["forms"]    = forms_json;

    return checkpoint_.SaveCheckpoint(win_json.dump(), form_state_json.dump());
}

std::optional<CheckpointPayload> CheckpointService::RestoreState() {
    auto raw = checkpoint_.LoadLatest();
    if (!raw) return std::nullopt;

    CheckpointPayload result;
    result.saved_at = raw->saved_at;

    try {
        auto win_json = nlohmann::json::parse(raw->window_state);
        if (win_json.contains("open_window_ids"))
            result.windows.open_window_ids = win_json["open_window_ids"]
                .get<std::vector<std::string>>();
        if (win_json.contains("active_window_id"))
            result.windows.active_window_id = win_json["active_window_id"].get<std::string>();

        auto form_state = nlohmann::json::parse(raw->form_state);
        if (form_state.contains("forms")) {
            for (const auto& fj : form_state["forms"]) {
                FormSnapshot f;
                f.window_id       = fj.value("window_id", "");
                f.filter_json     = fj.value("filter_json", "");
                f.selected_row_key = fj.value("selected_row_key", "");
                f.draft_text      = fj.value("draft_text", "");
                result.forms.push_back(f);
            }
        }
    } catch (...) {
        return std::nullopt;
    }

    return result;
}

} // namespace shelterops::services
