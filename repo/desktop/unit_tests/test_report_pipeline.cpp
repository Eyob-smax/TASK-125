#include <gtest/gtest.h>
#include "shelterops/domain/ReportPipeline.h"
#include <cmath>
#include <limits>

using namespace shelterops::domain;

// ---------------------------------------------------------------------------
// Occupancy rate
// ---------------------------------------------------------------------------
TEST(OccupancyRate, FullOccupancyIs100) {
    EXPECT_NEAR(ComputeOccupancyRate(10, 10), 100.0, 1e-9);
}

TEST(OccupancyRate, HalfOccupancyIs50) {
    EXPECT_NEAR(ComputeOccupancyRate(5, 10), 50.0, 1e-9);
}

TEST(OccupancyRate, ZeroTotalReturnsZero) {
    EXPECT_NEAR(ComputeOccupancyRate(0, 0), 0.0, 1e-9);
}

TEST(OccupancyRate, ZeroOccupiedIsZero) {
    EXPECT_NEAR(ComputeOccupancyRate(0, 10), 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Average occupancy
// ---------------------------------------------------------------------------
TEST(AvgOccupancy, EmptySeriesReturnsZero) {
    EXPECT_NEAR(ComputeAverageOccupancyRate({}), 0.0, 1e-9);
}

TEST(AvgOccupancy, SinglePoint) {
    OccupancyPoint p{0, 10, 4};
    EXPECT_NEAR(ComputeAverageOccupancyRate({p}), 40.0, 1e-9);
}

TEST(AvgOccupancy, MultiplePoints) {
    std::vector<OccupancyPoint> series = {
        {0, 10, 5},  // 50%
        {0, 10, 8},  // 80%
        {0, 10, 3},  // 30%
    };
    EXPECT_NEAR(ComputeAverageOccupancyRate(series), (50.0 + 80.0 + 30.0) / 3.0, 1e-6);
}

// ---------------------------------------------------------------------------
// Turnover rate
// ---------------------------------------------------------------------------
TEST(TurnoverRate, ZeroAvgOccupiedReturnsZero) {
    TurnoverPoint p{0, 5, 5};
    EXPECT_NEAR(ComputeTurnoverRate({p}, 0.0), 0.0, 1e-9);
}

TEST(TurnoverRate, CorrectCalculation) {
    std::vector<TurnoverPoint> series = {{0, 10, 8}, {0, 5, 7}};
    // total checkouts = 15, avg_occupied = 5.0 → rate = 3.0
    EXPECT_NEAR(ComputeTurnoverRate(series, 5.0), 3.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Maintenance response time
// ---------------------------------------------------------------------------
TEST(MaintenanceResponse, NoFirstActionIsNaN) {
    EXPECT_TRUE(std::isnan(ComputeMaintenanceResponseHours(1000, std::nullopt)));
}

TEST(MaintenanceResponse, OneHourResponse) {
    EXPECT_NEAR(ComputeMaintenanceResponseHours(1000, 1000 + 3600), 1.0, 1e-9);
}

TEST(MaintenanceResponse, ThirtyMinuteResponse) {
    EXPECT_NEAR(ComputeMaintenanceResponseHours(0, 1800), 0.5, 1e-9);
}

TEST(AvgMaintenanceResponse, AllNaNReturnsNaN) {
    std::vector<MaintenanceResponsePoint> tickets = {
        {1, 1000, std::nullopt, std::nullopt},
        {2, 2000, std::nullopt, std::nullopt},
    };
    int unack = 0;
    EXPECT_TRUE(std::isnan(ComputeAvgMaintenanceResponseHours(tickets, &unack)));
    EXPECT_EQ(unack, 2);
}

TEST(AvgMaintenanceResponse, MixedNaNAndValid) {
    std::vector<MaintenanceResponsePoint> tickets = {
        {1, 1000, 1000 + 3600, std::nullopt},  // 1 hour
        {2, 2000, std::nullopt, std::nullopt},   // unacknowledged
        {3, 3000, 3000 + 7200, std::nullopt},   // 2 hours
    };
    int unack = 0;
    double avg = ComputeAvgMaintenanceResponseHours(tickets, &unack);
    EXPECT_NEAR(avg, 1.5, 1e-6);
    EXPECT_EQ(unack, 1);
}

// ---------------------------------------------------------------------------
// Overdue fee distribution
// ---------------------------------------------------------------------------
TEST(OverdueFees, FutureFeesExcluded) {
    OverdueFeePoint f{1, 5000, 9999999LL};  // due far in future
    auto buckets = ComputeOverdueFeeDistribution({f}, 1000000LL);
    int64_t total = 0;
    for (auto& b : buckets) total += b.count;
    EXPECT_EQ(total, 0);
}

TEST(OverdueFees, FiveHoursOldGoesIn0To30Bucket) {
    int64_t now = 1000000;
    int64_t due_at = now - 5 * 3600;  // 5 hours ago < 1 day
    OverdueFeePoint f{1, 9900, due_at};
    auto buckets = ComputeOverdueFeeDistribution({f}, now);
    EXPECT_EQ(buckets[0].count, 1);        // 0–30 day bucket
    EXPECT_EQ(buckets[0].total_cents, 9900);
}

TEST(OverdueFees, FortyDaysOldGoesIn31To60Bucket) {
    int64_t now = 0;
    int64_t due_at = now - 40 * 86400;
    OverdueFeePoint f{1, 5000, due_at};
    auto buckets = ComputeOverdueFeeDistribution({f}, now);
    EXPECT_EQ(buckets[1].count, 1);        // 31–60 day bucket
}

TEST(OverdueFees, TwoHundredDaysOldGoesIn181PlusBucket) {
    int64_t now = 0;
    int64_t due_at = now - 200 * 86400;
    OverdueFeePoint f{1, 1000, due_at};
    auto buckets = ComputeOverdueFeeDistribution({f}, now);
    EXPECT_EQ(buckets[4].count, 1);        // 181+ day bucket
}

// ---------------------------------------------------------------------------
// Version delta
// ---------------------------------------------------------------------------
TEST(VersionDelta, CommonMetricsProduceDelta) {
    std::vector<std::pair<std::string, double>> before = {
        {"occupancy_rate", 60.0},
        {"turnover_rate",   2.5}
    };
    std::vector<std::pair<std::string, double>> after = {
        {"occupancy_rate", 75.0},
        {"turnover_rate",   3.0}
    };
    auto deltas = ComputeVersionDelta(before, after);
    ASSERT_EQ(deltas.size(), 2u);

    auto it = std::find_if(deltas.begin(), deltas.end(),
        [](const MetricDelta& d){ return d.metric_name == "occupancy_rate"; });
    ASSERT_NE(it, deltas.end());
    EXPECT_NEAR(it->delta_absolute, 15.0, 1e-9);
    EXPECT_NEAR(it->delta_pct,      25.0, 1e-6);
}

TEST(VersionDelta, ZeroBeforeValueProducesNaNPct) {
    std::vector<std::pair<std::string, double>> before = {{"metric", 0.0}};
    std::vector<std::pair<std::string, double>> after  = {{"metric", 5.0}};
    auto deltas = ComputeVersionDelta(before, after);
    ASSERT_EQ(deltas.size(), 1u);
    EXPECT_TRUE(std::isnan(deltas[0].delta_pct));
}

TEST(VersionDelta, MetricOnlyInAfterNotIncluded) {
    std::vector<std::pair<std::string, double>> before = {{"a", 1.0}};
    std::vector<std::pair<std::string, double>> after  = {{"a", 2.0}, {"b", 3.0}};
    auto deltas = ComputeVersionDelta(before, after);
    EXPECT_EQ(deltas.size(), 1u);
    EXPECT_EQ(deltas[0].metric_name, "a");
}
