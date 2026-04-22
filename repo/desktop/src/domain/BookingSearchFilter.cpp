#include "shelterops/domain/BookingSearchFilter.h"
#include <sodium.h>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace shelterops::domain {

std::string ComputeQueryHash(const BookingSearchFilter& filter) {
    // Deterministic serialization: sorted fields, fixed format.
    std::ostringstream ss;
    ss << "from=" << filter.window.from_unix
       << ",to=" << filter.window.to_unix
       << ",species=" << static_cast<int>(filter.species)
       << ",aggressive=" << (filter.is_aggressive ? 1 : 0)
       << ",large_dog=" << (filter.is_large_dog ? 1 : 0)
       << ",max_dist=" << std::fixed << std::setprecision(2) << filter.max_distance_ft
       << ",min_rating=" << filter.min_rating
       << ",max_price=" << filter.max_nightly_price_cents
       << ",bookable=" << (filter.only_bookable ? 1 : 0);

    // Sort zone_ids for determinism.
    std::vector<int64_t> sorted_zones = filter.zone_ids;
    std::sort(sorted_zones.begin(), sorted_zones.end());
    ss << ",zones=";
    for (auto z : sorted_zones) ss << z << ";";

    const std::string input = ss.str();

    // SHA-256 via libsodium.
    static_assert(crypto_hash_sha256_BYTES == 32, "unexpected hash size");
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash,
                       reinterpret_cast<const unsigned char*>(input.data()),
                       input.size());

    std::string hex;
    hex.reserve(64);
    for (auto b : hash) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", static_cast<unsigned>(b));
        hex += buf;
    }
    return hex;
}

std::vector<KennelInfo> FilterKennelsByHardConstraints(
    const std::vector<KennelInfo>& kennels,
    const BookingSearchFilter& filter) {

    std::vector<KennelInfo> result;
    result.reserve(kennels.size());

    for (const auto& k : kennels) {
        // Zone filter.
        if (!filter.zone_ids.empty()) {
            bool in_zone = false;
            for (auto z : filter.zone_ids) {
                if (k.zone_id == z) { in_zone = true; break; }
            }
            if (!in_zone) continue;
        }

        // Purpose filter (must be Boarding when only_bookable is true).
        if (filter.only_bookable && k.purpose != KennelPurpose::Boarding) continue;

        // Rating filter.
        if (filter.min_rating > 0.0f && k.rating < filter.min_rating) continue;

        // Price filter.
        if (filter.max_nightly_price_cents > 0 &&
            k.nightly_price_cents > filter.max_nightly_price_cents) continue;

        result.push_back(k);
    }
    return result;
}

BookingRequest FilterToRequest(const BookingSearchFilter& filter, int64_t kennel_id) {
    BookingRequest req;
    req.kennel_id    = kennel_id;
    req.window       = filter.window;
    req.pet_species  = filter.species;
    req.is_aggressive = filter.is_aggressive;
    req.is_large_dog  = filter.is_large_dog;
    return req;
}

} // namespace shelterops::domain
