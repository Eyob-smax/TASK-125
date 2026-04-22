#include "shelterops/ui/controllers/DiagnosticsController.h"
#include <spdlog/spdlog.h>

namespace shelterops::ui::controllers {

DiagnosticsController::DiagnosticsController(
    workers::JobQueue& job_queue,
    infrastructure::Database& db)
    : job_queue_(job_queue), db_(db)
{}

WorkerStatus DiagnosticsController::GetWorkerStatus() const {
    WorkerStatus ws;
    ws.is_idle = job_queue_.IsIdle();
    return ws;
}

DatabaseStats DiagnosticsController::GetDatabaseStats() const {
    DatabaseStats stats;
    auto guard = db_.Acquire();

    guard->Query("PRAGMA page_count", {},
        [&](const auto&, const auto& vals) {
            if (!vals.empty()) stats.page_count = std::stoll(vals[0]);
        });
    guard->Query("PRAGMA page_size", {},
        [&](const auto&, const auto& vals) {
            if (!vals.empty()) stats.page_size_bytes = std::stoll(vals[0]);
        });
    guard->Query("PRAGMA freelist_count", {},
        [&](const auto&, const auto& vals) {
            if (!vals.empty()) stats.freelist_pages = std::stoll(vals[0]);
        });
    guard->Query("PRAGMA journal_mode", {},
        [&](const auto&, const auto& vals) {
            if (!vals.empty()) stats.journal_mode = vals[0];
        });
    guard->Query("PRAGMA wal_checkpoint", {},
        [&](const auto&, const auto& vals) {
            if (!vals.empty()) stats.wal_status = "frames=" + vals[0];
        });
    return stats;
}

void DiagnosticsController::Refresh() {
    cache_stats_.clear();
    for (auto& ce : registered_caches_) {
        auto [cur, max] = ce.reporter();
        cache_stats_.push_back({ ce.name, cur, max });
    }
}

void DiagnosticsController::RegisterCache(
    const std::string& name, SizeReporter reporter)
{
    registered_caches_.push_back({ name, std::move(reporter) });
}

#if defined(SHELTEROPS_DEBUG_LEAKS)
bool DiagnosticsController::HasLeaks() noexcept {
    // On MSVC debug builds the CRT heap walk can detect live allocations.
    // We expose a simple boolean rather than dumping to stderr in a render loop.
#if defined(_WIN32) && defined(_DEBUG) && defined(_CRTDBG_MAP_ALLOC)
    _CrtMemState state;
    _CrtMemCheckpoint(&state);
    return state.lCounts[_NORMAL_BLOCK] > 0;
#else
    return false;
#endif
}
#endif

} // namespace shelterops::ui::controllers
