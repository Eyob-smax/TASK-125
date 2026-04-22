#pragma once
#include "shelterops/domain/Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace shelterops::domain {

struct RetentionRule {
    std::string        entity_type;
    int                retention_years = 7;
    RetentionActionKind action         = RetentionActionKind::Anonymize;
};

struct RetentionCandidate {
    int64_t     entity_id       = 0;
    std::string entity_type;
    int64_t     created_at      = 0;    // Unix timestamp
    bool        already_anonymized = false;
};

struct RetentionDecision {
    int64_t             entity_id = 0;
    std::string         entity_type;
    RetentionActionKind action;
    std::string         reason;         // human-readable; no PII
};

// Returns true if a record with the given created_at timestamp has exceeded
// the retention period defined by the rule.
bool NeedsRetention(
    const RetentionRule& rule,
    int64_t              created_at_unix,
    int64_t              now_unix) noexcept;

// Evaluates a list of candidates against their applicable rules and returns
// decisions for records that are past their retention deadline and have not
// already been anonymized.
std::vector<RetentionDecision> EvaluateRetention(
    const std::vector<RetentionCandidate>& candidates,
    const std::vector<RetentionRule>&      rules,
    int64_t                                now_unix);

} // namespace shelterops::domain
