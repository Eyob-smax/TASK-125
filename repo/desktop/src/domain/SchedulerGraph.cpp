#include "shelterops/domain/SchedulerGraph.h"
#include <unordered_set>
#include <stack>
#include <queue>

namespace shelterops::domain {

bool HasCircularDependency(const std::vector<SchedulerEdge>& edges,
                            int64_t job_id, int64_t to_id) {
    // Self-edge is a cycle.
    if (job_id == to_id) return true;

    // DFS from to_id through existing edges; if we reach job_id, it's a cycle.
    // "from job_id → to_id" would be the new edge; we check if to_id already
    // reaches job_id (which would make the new edge close a cycle).
    std::unordered_set<int64_t> visited;
    std::stack<int64_t> stack;
    stack.push(to_id);

    while (!stack.empty()) {
        int64_t current = stack.top();
        stack.pop();
        if (current == job_id) return true;
        if (visited.count(current)) continue;
        visited.insert(current);

        // Follow edges from current (current depends on these).
        for (const auto& e : edges) {
            if (e.job_id == current && !visited.count(e.depends_on_job_id)) {
                stack.push(e.depends_on_job_id);
            }
        }
    }
    return false;
}

std::vector<int64_t> TopologicalOrder(const std::vector<SchedulerEdge>& edges,
                                       int64_t root_id) {
    // Kahn's algorithm starting from root_id.
    // Build adjacency: successors of each node.
    std::unordered_map<int64_t, std::vector<int64_t>> successors;
    std::unordered_map<int64_t, int> in_degree;

    // Collect all nodes reachable from root_id following dependency chain.
    std::unordered_set<int64_t> reachable;
    {
        std::stack<int64_t> s;
        s.push(root_id);
        while (!s.empty()) {
            int64_t cur = s.top(); s.pop();
            if (!reachable.insert(cur).second) continue;
            for (const auto& e : edges) {
                if (e.job_id == cur && !reachable.count(e.depends_on_job_id)) {
                    s.push(e.depends_on_job_id);
                }
            }
        }
    }

    for (int64_t node : reachable) {
        in_degree[node] = 0;
        successors[node]; // ensure entry
    }

    for (const auto& e : edges) {
        if (reachable.count(e.job_id) && reachable.count(e.depends_on_job_id)) {
            successors[e.job_id].push_back(e.depends_on_job_id);
            in_degree[e.depends_on_job_id]++;
        }
    }

    std::queue<int64_t> q;
    for (auto& [id, deg] : in_degree) {
        if (deg == 0) q.push(id);
    }

    std::vector<int64_t> result;
    while (!q.empty()) {
        int64_t cur = q.front(); q.pop();
        result.push_back(cur);
        for (int64_t succ : successors[cur]) {
            if (--in_degree[succ] == 0) q.push(succ);
        }
    }
    return result;
}

std::vector<int64_t> NextReadyJobs(
    const std::vector<SchedulerEdge>& edges,
    const std::unordered_map<int64_t, std::string>& job_statuses) {

    // Collect all job ids in statuses.
    std::vector<int64_t> ready;
    for (const auto& [job_id, status] : job_statuses) {
        // Skip non-queued jobs.
        if (status != "queued") continue;

        // Check all prerequisites are completed.
        bool all_prereqs_done = true;
        for (const auto& e : edges) {
            if (e.job_id == job_id) {
                auto it = job_statuses.find(e.depends_on_job_id);
                if (it == job_statuses.end() || it->second != "completed") {
                    all_prereqs_done = false;
                    break;
                }
            }
        }
        if (all_prereqs_done) ready.push_back(job_id);
    }
    return ready;
}

} // namespace shelterops::domain
