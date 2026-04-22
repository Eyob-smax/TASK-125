#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

namespace shelterops::domain {

struct SchedulerEdge {
    int64_t job_id          = 0;
    int64_t depends_on_job_id = 0;
};

// Returns true if adding an edge from job_id → to_id would introduce a cycle.
// (i.e. to_id is already reachable from job_id through existing edges.)
// Also returns true for self-edges (job_id == to_id).
bool HasCircularDependency(const std::vector<SchedulerEdge>& edges,
                            int64_t job_id,
                            int64_t to_id);

// Returns job_ids in topological order starting from root_id.
// root_id itself appears first; jobs with no outgoing edges appear last.
// Returns empty vector if root_id has no edges.
std::vector<int64_t> TopologicalOrder(const std::vector<SchedulerEdge>& edges,
                                       int64_t root_id);

// Returns job_ids whose all prerequisites have a 'completed' status entry.
// job_statuses maps job_id → status string ("queued","running","completed","failed").
// A job with no prerequisites is immediately ready.
std::vector<int64_t> NextReadyJobs(
    const std::vector<SchedulerEdge>& edges,
    const std::unordered_map<int64_t, std::string>& job_statuses);

} // namespace shelterops::domain
