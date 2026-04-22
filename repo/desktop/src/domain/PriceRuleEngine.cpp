#include "shelterops/domain/PriceRuleEngine.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace shelterops::domain {

static bool RuleMatchesContext(const PriceRule& rule, const PriceContext& ctx,
                                int64_t now_unix) {
    if (!rule.is_active) return false;
    if (rule.valid_from > 0 && now_unix < rule.valid_from) return false;
    if (rule.valid_to   > 0 && now_unix > rule.valid_to)   return false;

    // Parse condition_json for optional species/zone/nights conditions.
    if (rule.condition_json.empty() || rule.condition_json == "{}") return true;

    try {
        auto cond = nlohmann::json::parse(rule.condition_json);

        if (cond.contains("species")) {
            auto expected_species = cond["species"].get<std::string>();
            auto species_str = [&]() -> std::string {
                switch (ctx.species) {
                case AnimalSpecies::Dog:         return "dog";
                case AnimalSpecies::Cat:         return "cat";
                case AnimalSpecies::Rabbit:      return "rabbit";
                case AnimalSpecies::Bird:        return "bird";
                case AnimalSpecies::Reptile:     return "reptile";
                case AnimalSpecies::SmallAnimal: return "small_animal";
                default:                          return "other";
                }
            }();
            if (species_str != expected_species) return false;
        }

        if (cond.contains("zone_id")) {
            auto expected_zone = cond["zone_id"].get<int64_t>();
            if (ctx.zone_id != expected_zone) return false;
        }

        if (cond.contains("days_gt")) {
            auto min_nights = cond["days_gt"].get<int64_t>();
            if (ctx.nights <= min_nights) return false;
        }

        if (cond.contains("is_adoption") && ctx.is_adoption != cond["is_adoption"].get<bool>()) {
            return false;
        }
    } catch (...) {
        // Unparseable condition — treat as no match.
        return false;
    }

    return true;
}

AdjustedPrice ApplyPriceRules(int base_price_cents,
                               const std::vector<PriceRule>& rules,
                               const PriceContext& context,
                               int64_t now_unix) {
    AdjustedPrice result;
    int price = base_price_cents;

    // Separate discounts and surcharges; apply discounts first.
    std::vector<const PriceRule*> discounts, surcharges;
    for (const auto& r : rules) {
        if (!RuleMatchesContext(r, context, now_unix)) continue;
        if (r.adjustment_type == PriceAdjustmentType::FixedDiscountCents ||
            r.adjustment_type == PriceAdjustmentType::PercentDiscount) {
            discounts.push_back(&r);
        } else {
            surcharges.push_back(&r);
        }
    }

    // Sort by rule_id for deterministic order.
    auto by_id = [](const PriceRule* a, const PriceRule* b) {
        return a->rule_id < b->rule_id;
    };
    std::sort(discounts.begin(),  discounts.end(),  by_id);
    std::sort(surcharges.begin(), surcharges.end(), by_id);

    auto apply_rule = [&](const PriceRule& r) {
        int delta = 0;
        switch (r.adjustment_type) {
        case PriceAdjustmentType::FixedDiscountCents:
            delta = -static_cast<int>(r.amount);
            break;
        case PriceAdjustmentType::PercentDiscount:
            delta = -static_cast<int>(std::round(price * r.amount / 100.0));
            break;
        case PriceAdjustmentType::FixedSurchargeCents:
            delta = static_cast<int>(r.amount);
            break;
        case PriceAdjustmentType::PercentSurcharge:
            delta = static_cast<int>(std::round(price * r.amount / 100.0));
            break;
        }
        price += delta;
        if (price < 0) price = 0;

        result.applied_rule_ids.push_back(r.rule_id);
        AppliedRuleDetail detail;
        detail.rule_id     = r.rule_id;
        detail.delta_cents = delta;
        detail.adjustment  = (delta < 0 ? "" : "+") + std::to_string(delta) + " cents";
        result.breakdown.push_back(detail);
    };

    for (auto* r : discounts)  apply_rule(*r);
    for (auto* r : surcharges) apply_rule(*r);

    result.final_cents = std::max(0, price);
    return result;
}

} // namespace shelterops::domain
