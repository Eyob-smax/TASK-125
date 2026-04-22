#pragma once
#include "shelterops/domain/Types.h"
#include "shelterops/domain/BookingRules.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::domain {

struct BookingSearchFilter {
    DateRange               window;
    AnimalSpecies           species       = AnimalSpecies::Other;
    bool                    is_aggressive = false;
    bool                    is_large_dog  = false;
    std::vector<int64_t>    zone_ids;       // empty = any zone
    float                   max_distance_ft        = 0.0f;   // 0 = no limit
    float                   min_rating             = 0.0f;
    int                     max_nightly_price_cents = 0;      // 0 = no limit
    bool                    only_bookable           = true;
};

// Returns a deterministic SHA-256 hex string over the serialized filter.
// Used as the query_hash for recommendation_results rows.
std::string ComputeQueryHash(const BookingSearchFilter& filter);

// Pre-narrow kennels before RankKennels:
//   - drops kennels not in zone_ids (if zone_ids is non-empty)
//   - drops kennels with rating < min_rating
//   - drops kennels with nightly_price_cents > max_nightly_price_cents (if limit set)
//   - drops kennels not in Boarding purpose when only_bookable is true
std::vector<KennelInfo> FilterKennelsByHardConstraints(
    const std::vector<KennelInfo>& kennels,
    const BookingSearchFilter& filter);

// Converts a BookingSearchFilter to a BookingRequest for use with EvaluateBookability.
BookingRequest FilterToRequest(const BookingSearchFilter& filter, int64_t kennel_id);

} // namespace shelterops::domain
