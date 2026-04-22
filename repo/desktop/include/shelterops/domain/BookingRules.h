#pragma once
#include "shelterops/domain/Types.h"
#include <vector>
#include <string>
#include <cstdint>

namespace shelterops::domain {
// KennelInfo is defined in Types.h and embedded in RankedKennel.

struct BookingRequest {
    int64_t      kennel_id  = 0;
    DateRange    window;
    AnimalSpecies pet_species  = AnimalSpecies::Other;
    bool          is_aggressive = false;
    bool          is_large_dog  = false;
};

struct ExistingBooking {
    int64_t        booking_id = 0;
    int64_t        kennel_id  = 0;
    DateRange      window;
    BookingStatus  status     = BookingStatus::Pending;
};

// Returns true when two date ranges share at least one second.
bool HasDateOverlap(const DateRange& a, const DateRange& b) noexcept;

// Counts active bookings for the same kennel that overlap the requested window.
// Cancelled and NoShow bookings are excluded from the count.
int CountOverlappingBookings(
    const std::vector<ExistingBooking>& existing,
    int64_t kennel_id,
    const DateRange& window) noexcept;

// Returns all restriction violations for the requested animal type and traits.
std::vector<RestrictionViolation> CheckRestrictions(
    const KennelInfo&    kennel,
    const BookingRequest& request);

// Full bookability evaluation: combines overlap, capacity, and restriction checks.
BookabilityResult EvaluateBookability(
    const KennelInfo&                   kennel,
    const BookingRequest&               request,
    const std::vector<ExistingBooking>& all_bookings);

// Ranks a list of kennels for a given request.
// Only kennels where EvaluateBookability returns is_bookable=true are included.
// Scoring: restriction compliance (+10/restriction), distance penalty (-1/100ft),
// price bonus (cheaper = +30 max), rating bonus (rating * 10).
std::vector<RankedKennel> RankKennels(
    const std::vector<KennelInfo>&      candidates,
    const BookingRequest&               request,
    const std::vector<ExistingBooking>& all_bookings,
    ZoneCoord                           reference_coord,
    float                               max_distance_ft = 0.0f);

// Formats the bookability result into a single operator-visible sentence.
// E.g. "Meets 'no cats' restriction; available 2026-03-27 – 2026-04-02; within 150 ft"
std::string FormatBookabilityExplanation(
    const KennelInfo&       kennel,
    const BookabilityResult& result,
    const BookingRequest&   request);

} // namespace shelterops::domain
