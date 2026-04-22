#include "shelterops/domain/RetentionPolicy.h"
#include <algorithm>

namespace shelterops::domain {

static constexpr int64_t kSecondsPerYear = 365LL * 24 * 3600;

bool NeedsRetention(
    const RetentionRule& rule,
    int64_t              created_at_unix,
    int64_t              now_unix) noexcept
{
    if (rule.retention_years <= 0) return false;
    int64_t deadline = created_at_unix +
        static_cast<int64_t>(rule.retention_years) * kSecondsPerYear;
    return now_unix >= deadline;
}

std::vector<RetentionDecision> EvaluateRetention(
    const std::vector<RetentionCandidate>& candidates,
    const std::vector<RetentionRule>&      rules,
    int64_t                                now_unix)
{
    std::vector<RetentionDecision> decisions;

    for (const auto& c : candidates) {
        if (c.already_anonymized) continue;

        auto it = std::find_if(rules.begin(), rules.end(),
            [&](const RetentionRule& r) { return r.entity_type == c.entity_type; });
        if (it == rules.end()) continue;

        if (!NeedsRetention(*it, c.created_at, now_unix)) continue;

        RetentionDecision d;
        d.entity_id   = c.entity_id;
        d.entity_type = c.entity_type;
        d.action      = it->action;
        d.reason      = "Retention period of " +
                         std::to_string(it->retention_years) +
                         " year(s) exceeded";
        decisions.push_back(std::move(d));
    }
    return decisions;
}

} // namespace shelterops::domain
