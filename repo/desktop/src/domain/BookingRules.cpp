#include "shelterops/domain/BookingRules.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace shelterops::domain {

bool HasDateOverlap(const DateRange& a, const DateRange& b) noexcept {
    return a.from_unix < b.to_unix && a.to_unix > b.from_unix;
}

int CountOverlappingBookings(
    const std::vector<ExistingBooking>& existing,
    int64_t kennel_id,
    const DateRange& window) noexcept
{
    int count = 0;
    for (const auto& b : existing) {
        if (b.kennel_id != kennel_id) continue;
        if (b.status == BookingStatus::Cancelled ||
            b.status == BookingStatus::NoShow)    continue;
        if (HasDateOverlap(window, b.window)) ++count;
    }
    return count;
}

std::vector<RestrictionViolation> CheckRestrictions(
    const KennelInfo&    kennel,
    const BookingRequest& request)
{
    std::vector<RestrictionViolation> violations;
    for (auto rt : kennel.restrictions) {
        switch (rt) {
        case KennelRestrictionType::NoCats:
            if (request.pet_species == AnimalSpecies::Cat)
                violations.push_back({rt, "Kennel does not accept cats"});
            break;
        case KennelRestrictionType::NoDogs:
            if (request.pet_species == AnimalSpecies::Dog)
                violations.push_back({rt, "Kennel does not accept dogs"});
            break;
        case KennelRestrictionType::NoLargeDogs:
            if (request.pet_species == AnimalSpecies::Dog && request.is_large_dog)
                violations.push_back({rt, "Kennel does not accept large dogs (>50 lbs)"});
            break;
        case KennelRestrictionType::DogsOnly:
            if (request.pet_species != AnimalSpecies::Dog)
                violations.push_back({rt, "Kennel accepts dogs only"});
            break;
        case KennelRestrictionType::CatsOnly:
            if (request.pet_species != AnimalSpecies::Cat)
                violations.push_back({rt, "Kennel accepts cats only"});
            break;
        case KennelRestrictionType::SmallAnimalsOnly:
            if (request.pet_species != AnimalSpecies::Rabbit &&
                request.pet_species != AnimalSpecies::Bird   &&
                request.pet_species != AnimalSpecies::Reptile &&
                request.pet_species != AnimalSpecies::SmallAnimal)
                violations.push_back({rt, "Kennel accepts small animals only"});
            break;
        case KennelRestrictionType::NoAggressive:
            if (request.is_aggressive)
                violations.push_back({rt, "Kennel does not accept animals flagged aggressive"});
            break;
        case KennelRestrictionType::MedicalOnly:
            violations.push_back({rt, "Kennel is reserved for medical cases only"});
            break;
        case KennelRestrictionType::QuarantineOnly:
            violations.push_back({rt, "Kennel is reserved for quarantine only"});
            break;
        }
    }
    return violations;
}

BookabilityResult EvaluateBookability(
    const KennelInfo&                   kennel,
    const BookingRequest&               request,
    const std::vector<ExistingBooking>& all_bookings)
{
    BookabilityResult result;

    if (!kennel.is_active) {
        result.explanation = "Kennel is not active";
        return result;
    }
    if (kennel.purpose != KennelPurpose::Boarding &&
        kennel.purpose != KennelPurpose::Empty) {
        result.explanation = "Kennel is not available for boarding";
        return result;
    }

    result.restriction_violations = CheckRestrictions(kennel, request);
    if (!result.restriction_violations.empty()) {
        result.explanation = result.restriction_violations[0].description;
        return result;
    }

    int overlapping = CountOverlappingBookings(
        all_bookings, kennel.kennel_id, request.window);

    if (overlapping >= kennel.capacity) {
        result.capacity_exceeded = true;
        result.explanation = "Kennel is fully booked for the requested dates";
        return result;
    }

    result.is_bookable = true;
    result.explanation = FormatBookabilityExplanation(kennel, result, request);
    return result;
}

// ---------------------------------------------------------------------------
// Scoring weights
static constexpr float kWeightRestrictionMet = 10.0f;  // per met restriction
static constexpr float kWeightDistancePenalty = 1.0f / 100.0f; // per foot
static constexpr float kWeightDistanceMax     = 50.0f;
static constexpr float kWeightPriceBonus      = 30.0f;
static constexpr float kWeightRating          = 10.0f;

std::vector<RankedKennel> RankKennels(
    const std::vector<KennelInfo>&      candidates,
    const BookingRequest&               request,
    const std::vector<ExistingBooking>& all_bookings,
    ZoneCoord                           reference_coord,
    float                               max_distance_ft)
{
    // Find min/max price among bookable candidates for normalization
    float min_price = std::numeric_limits<float>::max();
    float max_price = 0.0f;

    struct Scored { KennelInfo info; BookabilityResult bookability; float score; std::vector<RankReason> reasons; };
    std::vector<Scored> pool;

    for (const auto& k : candidates) {
        auto bookability = EvaluateBookability(k, request, all_bookings);
        if (!bookability.is_bookable) continue;

        float dist = k.coord.DistanceTo(reference_coord);
        if (max_distance_ft > 0.0f && dist > max_distance_ft) continue;

        float p = static_cast<float>(k.nightly_price_cents);
        if (p < min_price) min_price = p;
        if (p > max_price) max_price = p;
        pool.push_back({k, std::move(bookability), 0.0f, {}});
    }

    float price_range = max_price - min_price;

    for (auto& s : pool) {
        float score = 0.0f;

        // Restrictions bonus: each restriction the animal does NOT violate
        int met = static_cast<int>(s.info.restrictions.size());
        score += static_cast<float>(met) * kWeightRestrictionMet;

        // Distance penalty
        float dist = s.info.coord.DistanceTo(reference_coord);
        score -= std::min(dist * kWeightDistancePenalty, kWeightDistanceMax);

        // Price bonus (cheaper = better)
        if (price_range > 0.0f) {
            float p = static_cast<float>(s.info.nightly_price_cents);
            score += ((max_price - p) / price_range) * kWeightPriceBonus;
        }

        // Rating bonus
        score += s.info.rating * kWeightRating;

        s.score = score;

        // Build human-readable reasons for UI and audit trail
        if (met > 0)
            s.reasons.push_back({"RESTRICTION_MET",
                std::to_string(met) + " restriction(s) compatible with request"});

        float dist_ft = s.info.coord.DistanceTo(reference_coord);
        if (dist_ft < 1.0f) {
            s.bookability.explanation += "; same zone as reference";
            s.reasons.push_back({"DISTANCE_SAME_ZONE", "In the same zone as reference"});
        } else {
            std::ostringstream os;
            os << "; " << std::fixed << std::setprecision(0) << dist_ft << " ft from reference zone";
            s.bookability.explanation += os.str();
            std::ostringstream or2;
            or2 << std::fixed << std::setprecision(0) << dist_ft << " ft from reference zone";
            s.reasons.push_back({"DISTANCE_OK", or2.str()});
        }
        if (s.info.rating >= 4.0f) {
            std::ostringstream or3;
            or3 << std::fixed << std::setprecision(1) << s.info.rating;
            s.reasons.push_back({"HIGH_RATING", "Rating " + or3.str()});
        }
        if (price_range > 0.0f) {
            float p = static_cast<float>(s.info.nightly_price_cents);
            if ((max_price - p) / price_range > 0.5f)
                s.reasons.push_back({"COMPETITIVE_PRICE", "Price below median for available kennels"});
        }
    }

    std::sort(pool.begin(), pool.end(), [](const Scored& a, const Scored& b) {
        return a.score > b.score;
    });

    std::vector<RankedKennel> result;
    result.reserve(pool.size());
    int rank = 1;
    for (auto& s : pool) {
        RankedKennel rk;
        rk.kennel_id   = s.info.kennel_id;
        rk.kennel      = s.info;                    // full KennelInfo for UI and API serialization
        rk.score       = s.score;
        rk.rank        = rank++;
        rk.bookability = std::move(s.bookability);
        rk.reasons     = std::move(s.reasons);      // explainability for UI and audit trail
        result.push_back(std::move(rk));
    }
    return result;
}

static std::string FormatUnixDate(int64_t unix_ts) {
    std::time_t t = static_cast<std::time_t>(unix_ts);
    std::tm tm_val{};
#if defined(_WIN32)
    gmtime_s(&tm_val, &t);
#else
    gmtime_r(&t, &tm_val);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %I:%M %p", &tm_val);
    return std::string(buf);
}

std::string FormatBookabilityExplanation(
    const KennelInfo&       kennel,
    const BookabilityResult& result,
    const BookingRequest&   request)
{
    if (!result.is_bookable) {
        if (!result.restriction_violations.empty())
            return result.restriction_violations[0].description;
        if (result.capacity_exceeded)
            return "Fully booked for requested dates";
        return "Not available";
    }

    std::ostringstream os;

    if (!kennel.restrictions.empty()) {
        os << "Meets all restrictions";
    }

    os << (os.str().empty() ? "" : "; ")
       << "available "
       << FormatUnixDate(request.window.from_unix)
       << " \xE2\x80\x93 "
       << FormatUnixDate(request.window.to_unix);

    return os.str();
}

} // namespace shelterops::domain
