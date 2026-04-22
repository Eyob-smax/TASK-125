#pragma once
#include "shelterops/domain/Types.h"
#include <vector>
#include <string>
#include <cstdint>

namespace shelterops::domain {

struct PriceRule {
    int64_t             rule_id         = 0;
    PriceAdjustmentType adjustment_type = PriceAdjustmentType::FixedDiscountCents;
    double              amount          = 0.0;
    std::string         condition_json  = "{}";
    bool                is_active       = true;
    int64_t             valid_from      = 0;
    int64_t             valid_to        = 0;
};

struct PriceContext {
    AnimalSpecies species     = AnimalSpecies::Other;
    int64_t       zone_id     = 0;
    int64_t       nights      = 0;
    bool          is_adoption = false;
};

struct AppliedRuleDetail {
    int64_t     rule_id    = 0;
    std::string adjustment;    // e.g. "-500 cents (fixed discount)"
    int         delta_cents = 0;
};

struct AdjustedPrice {
    int final_cents = 0;
    std::vector<int64_t>          applied_rule_ids;
    std::vector<AppliedRuleDetail> breakdown;
};

// Pure price rule engine.
// Applies discounts first (in rule_id order), then surcharges.
// Final price is floored at 0.
// Rules with is_active=false or outside [valid_from, valid_to] are skipped.
// condition_json matching is simplified: species/zone conditions checked if present.
AdjustedPrice ApplyPriceRules(int base_price_cents,
                               const std::vector<PriceRule>& rules,
                               const PriceContext& context,
                               int64_t now_unix);

} // namespace shelterops::domain
