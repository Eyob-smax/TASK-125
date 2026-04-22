#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/KennelRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL, row_label TEXT, x_coord_ft REAL NOT NULL DEFAULT 0,"
            "y_coord_ft REAL NOT NULL DEFAULT 0, description TEXT, is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE zone_distance_cache(from_zone_id INTEGER NOT NULL, "
            "to_zone_id INTEGER NOT NULL, distance_ft REAL NOT NULL, "
            "PRIMARY KEY(from_zone_id, to_zone_id))");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'empty', nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("CREATE TABLE kennel_restrictions(restriction_id INTEGER PRIMARY KEY, "
            "kennel_id INTEGER NOT NULL, restriction_type TEXT NOT NULL, notes TEXT, "
            "UNIQUE(kennel_id, restriction_type))");
}

class KennelRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_ = std::make_unique<KennelRepository>(*db_);
    }
    std::unique_ptr<Database>          db_;
    std::unique_ptr<KennelRepository>  repo_;
};

TEST_F(KennelRepoTest, InsertAndListActive) {
    int64_t z = repo_->InsertZone("Zone A", "Main", "R1", 0.0f, 0.0f);
    EXPECT_GT(z, 0);
    int64_t k = repo_->InsertKennel(z, "K1", 1, 3000);
    EXPECT_GT(k, 0);

    auto kennels = repo_->ListActiveKennels();
    ASSERT_EQ(1u, kennels.size());
    EXPECT_EQ(k, kennels[0].kennel_id);
    EXPECT_EQ("K1", kennels[0].name);
}

TEST_F(KennelRepoTest, FindRestrictionsForKennel) {
    int64_t z = repo_->InsertZone("Zone B", "B", "", 0, 0);
    int64_t k = repo_->InsertKennel(z, "K2", 1, 0);
    repo_->InsertRestriction(k, KennelRestrictionType::NoCats);
    repo_->InsertRestriction(k, KennelRestrictionType::NoAggressive);

    auto restr = repo_->FindRestrictionsFor(k);
    EXPECT_EQ(2u, restr.size());
}

TEST_F(KennelRepoTest, ZoneDistanceCacheRoundTrip) {
    int64_t z1 = repo_->InsertZone("Z1", "B", "", 0.0f, 0.0f);
    int64_t z2 = repo_->InsertZone("Z2", "B", "", 30.0f, 40.0f);
    repo_->UpsertZoneDistance(z1, z2, 50.0f);
    float d = repo_->GetDistance(z1, z2);
    ASSERT_GE(d, 0.0f);
    EXPECT_FLOAT_EQ(50.0f, d);
}

TEST_F(KennelRepoTest, SetKennelPurpose) {
    int64_t z = repo_->InsertZone("Z3", "B", "", 0, 0);
    int64_t k = repo_->InsertKennel(z, "K3", 1, 0);
    repo_->SetKennelPurpose(k, KennelPurpose::Adoption);
    auto kennels = repo_->ListActiveKennels();
    ASSERT_EQ(1u, kennels.size());
    EXPECT_EQ(KennelPurpose::Adoption, kennels[0].purpose);
}

TEST_F(KennelRepoTest, FindByFilterZoneId) {
    int64_t z1 = repo_->InsertZone("Z4", "B", "", 0, 0);
    int64_t z2 = repo_->InsertZone("Z5", "B", "", 0, 0);
    repo_->InsertKennel(z1, "K4", 1, 0);
    repo_->InsertKennel(z2, "K5", 1, 0);

    KennelSearchParams params;
    params.zone_id = z1;
    auto result = repo_->FindByFilter(params);
    ASSERT_EQ(1u, result.size());
    EXPECT_EQ(z1, result[0].zone_id);
}
