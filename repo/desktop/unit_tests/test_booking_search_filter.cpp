#include <gtest/gtest.h>
#include "shelterops/domain/BookingSearchFilter.h"
#include "shelterops/domain/Types.h"

using namespace shelterops::domain;

static BookingSearchFilter MakeFilter() {
    BookingSearchFilter f;
    f.window.from_unix       = 1700000000;
    f.window.to_unix         = 1700086400;
    f.species                = AnimalSpecies::Dog;
    f.is_aggressive          = false;
    f.is_large_dog           = false;
    f.zone_ids               = {3, 1, 2};
    f.max_distance_ft        = 100.0f;
    f.min_rating             = 4.0f;
    f.max_nightly_price_cents = 5000;
    f.only_bookable          = true;
    return f;
}

TEST(BookingSearchFilter, HashIsDeterministic) {
    auto f = MakeFilter();
    std::string h1 = ComputeQueryHash(f);
    std::string h2 = ComputeQueryHash(f);
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(64u, h1.size()); // SHA-256 hex = 64 chars
}

TEST(BookingSearchFilter, HashInsensitiveToZoneOrdering) {
    auto f1 = MakeFilter();
    f1.zone_ids = {1, 2, 3};
    auto f2 = MakeFilter();
    f2.zone_ids = {3, 1, 2};
    EXPECT_EQ(ComputeQueryHash(f1), ComputeQueryHash(f2));
}

TEST(BookingSearchFilter, DifferentFiltersProduceDifferentHashes) {
    auto f1 = MakeFilter();
    auto f2 = MakeFilter();
    f2.max_nightly_price_cents = 9999;
    EXPECT_NE(ComputeQueryHash(f1), ComputeQueryHash(f2));
}

TEST(BookingSearchFilter, FilterDropsWrongZone) {
    KennelInfo k1;
    k1.kennel_id = 1; k1.zone_id = 5;
    k1.purpose = KennelPurpose::Boarding;
    k1.rating = 4.5f;
    k1.nightly_price_cents = 3000;

    KennelInfo k2;
    k2.kennel_id = 2; k2.zone_id = 1;
    k2.purpose = KennelPurpose::Boarding;
    k2.rating = 4.5f;
    k2.nightly_price_cents = 3000;

    BookingSearchFilter f;
    f.zone_ids = {1, 2};
    f.only_bookable = false;

    auto result = FilterKennelsByHardConstraints({k1, k2}, f);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(2, result[0].kennel_id);
}

TEST(BookingSearchFilter, FilterDropsBelowMinRating) {
    KennelInfo k;
    k.kennel_id = 1; k.zone_id = 1;
    k.purpose = KennelPurpose::Boarding;
    k.rating = 3.0f;
    k.nightly_price_cents = 2000;

    BookingSearchFilter f;
    f.min_rating = 4.0f;
    f.only_bookable = false;

    auto result = FilterKennelsByHardConstraints({k}, f);
    EXPECT_TRUE(result.empty());
}

TEST(BookingSearchFilter, FilterDropsAboveMaxPrice) {
    KennelInfo k;
    k.kennel_id = 1; k.zone_id = 1;
    k.purpose = KennelPurpose::Boarding;
    k.rating = 5.0f;
    k.nightly_price_cents = 10000;

    BookingSearchFilter f;
    f.max_nightly_price_cents = 5000;
    f.only_bookable = false;

    auto result = FilterKennelsByHardConstraints({k}, f);
    EXPECT_TRUE(result.empty());
}

TEST(BookingSearchFilter, FilterDropsNonBoardingWhenOnlyBookable) {
    KennelInfo k;
    k.kennel_id = 1; k.zone_id = 1;
    k.purpose = KennelPurpose::Adoption;
    k.rating = 5.0f;
    k.nightly_price_cents = 1000;

    BookingSearchFilter f;
    f.only_bookable = true;

    auto result = FilterKennelsByHardConstraints({k}, f);
    EXPECT_TRUE(result.empty());
}
