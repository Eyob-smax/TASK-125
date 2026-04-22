#pragma once
#include "shelterops/workers/JobQueue.h"
#include "shelterops/infrastructure/Database.h"
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace shelterops::ui::controllers {

struct WorkerStatus {
    int total_workers  = 0;
    int in_flight_jobs = 0;
    bool is_idle       = true;
};

struct CacheStats {
    std::string name;
    std::size_t current_size  = 0;
    std::size_t max_size      = 0;
};

struct DatabaseStats {
    int64_t page_count        = 0;
    int64_t page_size_bytes   = 0;
    int64_t freelist_pages    = 0;
    std::string journal_mode;
    std::string wal_status;
};

// Diagnostics controller — worker health, cache stats, DB info.
// Used by the debug diagnostics panel (available in Debug builds and to Administrators).
// Cross-platform: no ImGui dependency.
class DiagnosticsController {
public:
    DiagnosticsController(workers::JobQueue&         job_queue,
                           infrastructure::Database&  db);

    WorkerStatus   GetWorkerStatus()  const;
    DatabaseStats  GetDatabaseStats() const;

    // Returns stats for all registered caches (name→size/max).
    // Caches register themselves via RegisterCache().
    const std::vector<CacheStats>& GetCacheStats() const noexcept { return cache_stats_; }

    // Call once per frame (or on demand) to refresh all stats.
    void Refresh();

    // Register a named cache for size tracking.
    // Cache reporter is a callable that returns (current_size, max_size).
    using SizeReporter = std::function<std::pair<std::size_t, std::size_t>()>;
    void RegisterCache(const std::string& name, SizeReporter reporter);

#if defined(SHELTEROPS_DEBUG_LEAKS)
    // Returns true if leak-detection hooks reported any live allocation.
    static bool HasLeaks() noexcept;
#endif

private:
    workers::JobQueue&         job_queue_;
    infrastructure::Database&  db_;
    std::vector<CacheStats>    cache_stats_;

    struct CacheEntry {
        std::string  name;
        SizeReporter reporter;
    };
    std::vector<CacheEntry> registered_caches_;
};

} // namespace shelterops::ui::controllers
