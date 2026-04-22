#include <gtest/gtest.h>
#include "shelterops/domain/BookingRules.h"
#include "shelterops/domain/Types.h"

using namespace shelterops::domain;

// ---------------------------------------------------------------------------
// DateRange overlap detection
// ---------------------------------------------------------------------------
TEST(DateRangeOverlap, AdjacentRangesDoNotOverlap) {
    DateRange a{0, 100};
    DateRange b{100, 200};
    EXPECT_FALSE(HasDateOverlap(a, b));
    EXPECT_FALSE(HasDateOverlap(b, a));
}

TEST(DateRangeOverlap, PartialOverlapIsDetected) {
    DateRange a{0,  150};
    DateRange b{100, 250};
    EXPECT_TRUE(HasDateOverlap(a, b));
    EXPECT_TRUE(HasDateOverlap(b, a));
}

TEST(DateRangeOverlap, ContainedRangeOverlaps) {
    DateRange outer{0, 500};
    DateRange inner{100, 300};
    EXPECT_TRUE(HasDateOverlap(outer, inner));
    EXPECT_TRUE(HasDateOverlap(inner, outer));
}

TEST(DateRangeOverlap, IdenticalRangesOverlap) {
    DateRange a{200, 400};
    EXPECT_TRUE(HasDateOverlap(a, a));
}

TEST(DateRangeOverlap, NoOverlapBeforeRange) {
    DateRange a{0,  50};
    DateRange b{100, 200};
    EXPECT_FALSE(HasDateOverlap(a, b));
}

TEST(DateRangeOverlap, NoOverlapAfterRange) {
    DateRange a{300, 500};
    DateRange b{0,   200};
    EXPECT_FALSE(HasDateOverlap(a, b));
}

// ---------------------------------------------------------------------------
// CountOverlappingBookings
// ---------------------------------------------------------------------------
TEST(CountOverlapping, EmptySetReturnsZero) {
    EXPECT_EQ(CountOverlappingBookings({}, 1, DateRange{100, 200}), 0);
}

TEST(CountOverlapping, ActiveOverlapCounted) {
    ExistingBooking b;
    b.kennel_id = 1;
    b.booking_id = 10;
    b.window = {50, 150};
    b.status = BookingStatus::Approved;
    EXPECT_EQ(CountOverlappingBookings({b}, 1, DateRange{100, 200}), 1);
}

TEST(CountOverlapping, CancelledNotCounted) {
    ExistingBooking b;
    b.kennel_id = 1;
    b.booking_id = 11;
    b.window = {50, 150};
    b.status = BookingStatus::Cancelled;
    EXPECT_EQ(CountOverlappingBookings({b}, 1, DateRange{100, 200}), 0);
}

TEST(CountOverlapping, NoShowNotCounted) {
    ExistingBooking b;
    b.kennel_id = 1;
    b.booking_id = 12;
    b.window = {50, 150};
    b.status = BookingStatus::NoShow;
    EXPECT_EQ(CountOverlappingBookings({b}, 1, DateRange{100, 200}), 0);
}

TEST(CountOverlapping, DifferentKennelNotCounted) {
    ExistingBooking b;
    b.kennel_id = 2;
    b.booking_id = 13;
    b.window = {50, 150};
    b.status = BookingStatus::Active;
    EXPECT_EQ(CountOverlappingBookings({b}, 1, DateRange{100, 200}), 0);
}

TEST(CountOverlapping, MultipleActiveCount) {
    std::vector<ExistingBooking> bookings;
    for (int i = 0; i < 3; ++i) {
        ExistingBooking b;
        b.kennel_id  = 1;
        b.booking_id = i;
        b.window     = {50, 300};
        b.status     = BookingStatus::Active;
        bookings.push_back(b);
    }
    EXPECT_EQ(CountOverlappingBookings(bookings, 1, DateRange{100, 200}), 3);
}

// ---------------------------------------------------------------------------
// CheckRestrictions
// ---------------------------------------------------------------------------
static KennelInfo MakeKennel(std::vector<KennelRestrictionType> restrictions) {
    KennelInfo k;
    k.kennel_id   = 1;
    k.capacity    = 1;
    k.purpose     = KennelPurpose::Boarding;
    k.is_active   = true;
    k.restrictions = std::move(restrictions);
    return k;
}

static BookingRequest MakeRequest(AnimalSpecies sp, bool aggressive = false, bool large = false) {
    BookingRequest r;
    r.kennel_id    = 1;
    r.window       = {1000, 2000};
    r.pet_species  = sp;
    r.is_aggressive = aggressive;
    r.is_large_dog  = large;
    return r;
}

TEST(Restrictions, NoCatsBlocksCat) {
    auto k = MakeKennel({KennelRestrictionType::NoCats});
    auto v = CheckRestrictions(k, MakeRequest(AnimalSpecies::Cat));
    EXPECT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].type, KennelRestrictionType::NoCats);
}

TEST(Restrictions, NoCatsAllowsDog) {
    auto k = MakeKennel({KennelRestrictionType::NoCats});
    EXPECT_TRUE(CheckRestrictions(k, MakeRequest(AnimalSpecies::Dog)).empty());
}

TEST(Restrictions, NoLargeDogsBlocksLargeDog) {
    auto k = MakeKennel({KennelRestrictionType::NoLargeDogs});
    EXPECT_EQ(CheckRestrictions(k, MakeRequest(AnimalSpecies::Dog, false, true)).size(), 1u);
}

TEST(Restrictions, NoLargeDogsAllowsSmallDog) {
    auto k = MakeKennel({KennelRestrictionType::NoLargeDogs});
    EXPECT_TRUE(CheckRestrictions(k, MakeRequest(AnimalSpecies::Dog, false, false)).empty());
}

TEST(Restrictions, NoAggressiveBlocksAggressiveDog) {
    auto k = MakeKennel({KennelRestrictionType::NoAggressive});
    EXPECT_EQ(CheckRestrictions(k, MakeRequest(AnimalSpecies::Dog, true)).size(), 1u);
}

TEST(Restrictions, DogsOnlyBlocksCat) {
    auto k = MakeKennel({KennelRestrictionType::DogsOnly});
    EXPECT_EQ(CheckRestrictions(k, MakeRequest(AnimalSpecies::Cat)).size(), 1u);
}

TEST(Restrictions, DogsOnlyAllowsDog) {
    auto k = MakeKennel({KennelRestrictionType::DogsOnly});
    EXPECT_TRUE(CheckRestrictions(k, MakeRequest(AnimalSpecies::Dog)).empty());
}

TEST(Restrictions, NoRestrictionsAllowsAnything) {
    auto k = MakeKennel({});
    EXPECT_TRUE(CheckRestrictions(k, MakeRequest(AnimalSpecies::Cat)).empty());
    EXPECT_TRUE(CheckRestrictions(k, MakeRequest(AnimalSpecies::Dog, true, true)).empty());
}

// ---------------------------------------------------------------------------
// EvaluateBookability
// ---------------------------------------------------------------------------
TEST(Bookability, AvailableKennelIsBookable) {
    auto k = MakeKennel({});
    BookingRequest r = MakeRequest(AnimalSpecies::Dog);
    auto result = EvaluateBookability(k, r, {});
    EXPECT_TRUE(result.is_bookable);
    EXPECT_TRUE(result.conflicts.empty());
    EXPECT_TRUE(result.restriction_violations.empty());
    EXPECT_FALSE(result.capacity_exceeded);
}

TEST(Bookability, CapacityExceededMakesUnbookable) {
    auto k = MakeKennel({});
    k.capacity = 1;
    ExistingBooking b;
    b.kennel_id  = 1;
    b.booking_id = 99;
    b.window     = {500, 3000};
    b.status     = BookingStatus::Active;
    BookingRequest r = MakeRequest(AnimalSpecies::Dog);
    r.window = {1000, 2000};
    auto result = EvaluateBookability(k, r, {b});
    EXPECT_FALSE(result.is_bookable);
    EXPECT_TRUE(result.capacity_exceeded);
}

TEST(Bookability, RestrictionViolationMakesUnbookable) {
    auto k = MakeKennel({KennelRestrictionType::NoCats});
    BookingRequest r = MakeRequest(AnimalSpecies::Cat);
    auto result = EvaluateBookability(k, r, {});
    EXPECT_FALSE(result.is_bookable);
    EXPECT_EQ(result.restriction_violations.size(), 1u);
}

TEST(Bookability, InactiveKennelIsUnbookable) {
    auto k = MakeKennel({});
    k.is_active = false;
    auto result = EvaluateBookability(k, MakeRequest(AnimalSpecies::Dog), {});
    EXPECT_FALSE(result.is_bookable);
}

TEST(Bookability, AdoptionPurposeIsUnbookable) {
    auto k = MakeKennel({});
    k.purpose = KennelPurpose::Adoption;
    auto result = EvaluateBookability(k, MakeRequest(AnimalSpecies::Dog), {});
    EXPECT_FALSE(result.is_bookable);
}

TEST(Bookability, Capacity2AllowsSecondBooking) {
    auto k = MakeKennel({});
    k.capacity = 2;
    ExistingBooking b;
    b.kennel_id  = 1;
    b.booking_id = 1;
    b.window     = {500, 3000};
    b.status     = BookingStatus::Active;
    BookingRequest r = MakeRequest(AnimalSpecies::Dog);
    r.window = {1000, 2000};
    auto result = EvaluateBookability(k, r, {b});
    EXPECT_TRUE(result.is_bookable);
}

// ---------------------------------------------------------------------------
// RankKennels
// ---------------------------------------------------------------------------
TEST(Ranking, OnlyBookableKennelsAppear) {
    KennelInfo k1 = MakeKennel({});
    k1.kennel_id = 1;
    KennelInfo k2 = MakeKennel({KennelRestrictionType::NoCats});
    k2.kennel_id = 2;

    BookingRequest r = MakeRequest(AnimalSpecies::Cat);
    r.kennel_id = 0;
    auto ranked = RankKennels({k1, k2}, r, {}, ZoneCoord{});
    ASSERT_EQ(ranked.size(), 1u);
    EXPECT_EQ(ranked[0].kennel_id, 1);
}

TEST(Ranking, CloserKennelRanksHigher) {
    KennelInfo k1 = MakeKennel({});
    k1.kennel_id = 1;
    k1.coord = {500.0f, 0.0f};   // 500 ft away

    KennelInfo k2 = MakeKennel({});
    k2.kennel_id = 2;
    k2.coord = {100.0f, 0.0f};   // 100 ft away — should rank higher

    BookingRequest r = MakeRequest(AnimalSpecies::Dog);
    auto ranked = RankKennels({k1, k2}, r, {}, ZoneCoord{0, 0});
    ASSERT_EQ(ranked.size(), 2u);
    EXPECT_EQ(ranked[0].kennel_id, 2);
}

TEST(Ranking, ResultsAreSortedByRank) {
    std::vector<KennelInfo> pool;
    for (int i = 1; i <= 5; ++i) {
        auto k = MakeKennel({});
        k.kennel_id = i;
        pool.push_back(k);
    }
    auto ranked = RankKennels(pool, MakeRequest(AnimalSpecies::Dog), {}, ZoneCoord{});
    for (int i = 0; i < static_cast<int>(ranked.size()); ++i)
        EXPECT_EQ(ranked[i].rank, i + 1);
}
