#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/AnimalRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL, row_label TEXT, x_coord_ft REAL DEFAULT 0, "
            "y_coord_ft REAL DEFAULT 0, description TEXT, is_active INTEGER DEFAULT 1)");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER, "
            "name TEXT NOT NULL, capacity INTEGER DEFAULT 1, current_purpose TEXT DEFAULT 'boarding', "
            "nightly_price_cents INTEGER DEFAULT 0, rating REAL, is_active INTEGER DEFAULT 1, notes TEXT)");
    g->Exec("CREATE TABLE animals(animal_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "species TEXT NOT NULL, breed TEXT, age_years REAL, weight_lbs REAL, "
            "color TEXT, sex TEXT, microchip_id TEXT, is_aggressive INTEGER NOT NULL DEFAULT 0, "
            "is_large_dog INTEGER NOT NULL DEFAULT 0, intake_at INTEGER NOT NULL, "
            "intake_type TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'intake', "
            "notes TEXT, created_by INTEGER, anonymized_at INTEGER)");
    g->Exec("CREATE TABLE adoptable_listings(listing_id INTEGER PRIMARY KEY, "
            "animal_id INTEGER NOT NULL, kennel_id INTEGER, listing_date INTEGER NOT NULL, "
            "adoption_fee_cents INTEGER NOT NULL DEFAULT 0, description TEXT, "
            "rating REAL, status TEXT NOT NULL DEFAULT 'active', "
            "created_by INTEGER, adopted_at INTEGER)");
}

class AnimalRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_ = std::make_unique<AnimalRepository>(*db_);
    }
    std::unique_ptr<Database>          db_;
    std::unique_ptr<AnimalRepository>  repo_;
};

TEST_F(AnimalRepoTest, InsertAndFindById) {
    NewAnimalParams p;
    p.name = "Buddy"; p.species = AnimalSpecies::Dog; p.intake_type = "stray";
    p.created_by = 1;

    int64_t id = repo_->Insert(p, 1000);
    EXPECT_GT(id, 0);

    auto a = repo_->FindById(id);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ("Buddy", a->name);
    EXPECT_EQ(AnimalStatus::Intake, a->status);
}

TEST_F(AnimalRepoTest, UpdateStatus) {
    NewAnimalParams p;
    p.name = "Luna"; p.species = AnimalSpecies::Cat; p.intake_type = "surrender";
    p.created_by = 1;
    int64_t id = repo_->Insert(p, 500);

    repo_->UpdateStatus(id, AnimalStatus::Adoptable);
    auto a = repo_->FindById(id);
    EXPECT_EQ(AnimalStatus::Adoptable, a->status);
}

TEST_F(AnimalRepoTest, IntakeAtIsImmutable) {
    NewAnimalParams p;
    p.name = "Max"; p.species = AnimalSpecies::Dog; p.intake_type = "transfer";
    p.created_by = 1;
    int64_t id = repo_->Insert(p, 12345);

    repo_->UpdateStatus(id, AnimalStatus::Boarding);
    auto a = repo_->FindById(id);
    EXPECT_EQ(12345, a->intake_at);
}

TEST_F(AnimalRepoTest, AnonymizeNullsPii) {
    NewAnimalParams p;
    p.name = "Charlie"; p.species = AnimalSpecies::Rabbit; p.intake_type = "stray";
    p.created_by = 1;
    int64_t id = repo_->Insert(p, 100);

    repo_->Anonymize(id, 9000);
    auto a = repo_->FindById(id);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ("[anonymized]", a->name);
    EXPECT_NE(0, a->anonymized_at);
}

TEST_F(AnimalRepoTest, InsertListingAndListByKennel) {
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO zones(zone_id,name,building,x_coord_ft,y_coord_ft) VALUES(1,'Z','B',0,0)");
        g->Exec("INSERT INTO kennels(kennel_id,zone_id,name) VALUES(1,1,'K1')");
    }

    NewAnimalParams p;
    p.name = "Nala"; p.species = AnimalSpecies::Cat; p.intake_type = "stray";
    p.created_by = 1;
    int64_t aid = repo_->Insert(p, 100);

    repo_->InsertListing(aid, 1, 5000, 1000, 1);
    auto listings = repo_->ListAdoptableByKennel(1);
    ASSERT_EQ(1u, listings.size());
    EXPECT_EQ(aid, listings[0].animal_id);
}
