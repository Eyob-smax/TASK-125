#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/infrastructure/CredentialVault.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include "shelterops/services/BookingService.h"
#include "shelterops/services/AuditService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::domain;
using namespace shelterops::common;

static std::vector<uint8_t> HexDecode(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2U);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "event_type TEXT NOT NULL, description TEXT NOT NULL, actor_id INTEGER, "
            "occurred_at INTEGER NOT NULL, entity_type TEXT, entity_id INTEGER)");
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL, row_label TEXT, x_coord_ft REAL DEFAULT 0, "
            "y_coord_ft REAL DEFAULT 0, description TEXT, is_active INTEGER DEFAULT 1)");
    g->Exec("INSERT INTO zones VALUES(1,'Zone A','B',NULL,0,0,NULL,1)");
    g->Exec("CREATE TABLE zone_distance_cache(from_zone_id INTEGER NOT NULL, "
            "to_zone_id INTEGER NOT NULL, distance_ft REAL NOT NULL, PRIMARY KEY(from_zone_id,to_zone_id))");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'boarding', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL DEFAULT 0, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("INSERT INTO kennels VALUES(1,1,'K1',1,'boarding',3000,4.5,1,NULL)");
    g->Exec("CREATE TABLE kennel_restrictions(restriction_id INTEGER PRIMARY KEY, "
            "kennel_id INTEGER NOT NULL, restriction_type TEXT NOT NULL, notes TEXT, "
            "UNIQUE(kennel_id,restriction_type))");
    g->Exec("CREATE TABLE animals(animal_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "species TEXT NOT NULL, intake_at INTEGER NOT NULL, intake_type TEXT NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'intake', is_aggressive INTEGER DEFAULT 0, "
            "is_large_dog INTEGER DEFAULT 0, breed TEXT, age_years REAL, weight_lbs REAL, "
            "color TEXT, sex TEXT, microchip_id TEXT, notes TEXT, created_by INTEGER, anonymized_at INTEGER)");
    g->Exec("CREATE TABLE adoptable_listings(listing_id INTEGER PRIMARY KEY, "
            "animal_id INTEGER NOT NULL, kennel_id INTEGER, listing_date INTEGER NOT NULL, "
            "adoption_fee_cents INTEGER NOT NULL DEFAULT 0, description TEXT, "
            "rating REAL, status TEXT NOT NULL DEFAULT 'active', "
            "created_by INTEGER, adopted_at INTEGER)");
    g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER NOT NULL, "
            "animal_id INTEGER, guest_name TEXT, guest_phone_enc TEXT, guest_email_enc TEXT, "
            "check_in_at INTEGER NOT NULL, check_out_at INTEGER NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'pending', nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "total_price_cents INTEGER NOT NULL DEFAULT 0, special_requirements TEXT, "
            "created_by INTEGER, created_at INTEGER NOT NULL, "
            "approved_by INTEGER, approved_at INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE booking_approvals(approval_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER NOT NULL, requested_by INTEGER NOT NULL, "
            "requested_at INTEGER NOT NULL, approver_id INTEGER, "
            "decision TEXT, decided_at INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE boarding_fees(fee_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER NOT NULL, amount_cents INTEGER NOT NULL, "
            "due_at INTEGER NOT NULL, paid_at INTEGER, "
            "payment_method TEXT, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE recommendation_results(result_id INTEGER PRIMARY KEY, "
            "query_hash TEXT NOT NULL, kennel_id INTEGER NOT NULL, rank_position INTEGER NOT NULL, "
            "score REAL NOT NULL, reason_json TEXT NOT NULL, generated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO system_policies(key,value,updated_at) "
            "VALUES('booking_approval_required','false',1)");
    g->Exec("CREATE TABLE product_catalog(entry_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, category_id INTEGER, default_unit_cost_cents INTEGER DEFAULT 0, "
            "vendor TEXT, sku TEXT UNIQUE, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE price_rules(rule_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, applies_to TEXT NOT NULL, condition_json TEXT NOT NULL DEFAULT '{}', "
            "adjustment_type TEXT NOT NULL, amount REAL NOT NULL, "
            "valid_from INTEGER, valid_to INTEGER, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE after_sales_adjustments(adjustment_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER, amount_cents INTEGER NOT NULL, reason TEXT NOT NULL, "
            "approved_by INTEGER, created_by INTEGER NOT NULL, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE retention_policies(policy_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL UNIQUE, retention_years INTEGER NOT NULL DEFAULT 7, "
            "action TEXT NOT NULL DEFAULT 'anonymize', updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE consent_records(consent_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL, entity_id INTEGER NOT NULL, "
            "consent_type TEXT NOT NULL, given_at INTEGER NOT NULL, withdrawn_at INTEGER)");
    g->Exec("CREATE TABLE export_permissions(permission_id INTEGER PRIMARY KEY, "
            "role TEXT NOT NULL, report_type TEXT NOT NULL, "
            "csv_allowed INTEGER NOT NULL DEFAULT 0, pdf_allowed INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(role,report_type))");
}

class BookingSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        CryptoHelper::Init();
        db_           = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        kennel_repo_  = std::make_unique<KennelRepository>(*db_);
        booking_repo_ = std::make_unique<BookingRepository>(*db_);
        admin_repo_   = std::make_unique<AdminRepository>(*db_);
        audit_repo_   = std::make_unique<AuditRepository>(*db_);
        audit_svc_    = std::make_unique<AuditService>(*audit_repo_);
        vault_        = std::make_unique<InMemoryCredentialVault>();
        svc_          = std::make_unique<BookingService>(
            *kennel_repo_, *booking_repo_, *admin_repo_, *vault_, *audit_svc_);
        ctx_.user_id = 1; ctx_.role = UserRole::Administrator;
    }

    std::unique_ptr<Database>              db_;
    std::unique_ptr<KennelRepository>      kennel_repo_;
    std::unique_ptr<BookingRepository>     booking_repo_;
    std::unique_ptr<AdminRepository>       admin_repo_;
    std::unique_ptr<AuditRepository>       audit_repo_;
    std::unique_ptr<AuditService>          audit_svc_;
    std::unique_ptr<InMemoryCredentialVault> vault_;
    std::unique_ptr<BookingService>        svc_;
    UserContext                            ctx_;
};

TEST_F(BookingSvcTest, SearchAndRankReturnsBookableKennels) {
    BookingSearchFilter filter;
    filter.window.from_unix  = 1000;
    filter.window.to_unix    = 2000;
    filter.only_bookable     = true;

    auto ranked = svc_->SearchAndRank(filter, ctx_, 100);
    ASSERT_EQ(1u, ranked.size());
    EXPECT_EQ(1, ranked[0].kennel.kennel_id);
}

TEST_F(BookingSvcTest, RankedKennelCarriesFullMetadataAndReasons) {
    BookingSearchFilter filter;
    filter.window.from_unix  = 1000;
    filter.window.to_unix    = 2000;
    filter.only_bookable     = true;

    auto ranked = svc_->SearchAndRank(filter, ctx_, 100);
    ASSERT_GE(ranked.size(), 1u);

    const auto& rk = ranked[0];
    // Full KennelInfo must be populated (not default-initialised).
    EXPECT_EQ(1,      rk.kennel.kennel_id);
    EXPECT_EQ("K1",   rk.kennel.name);
    EXPECT_EQ(1,      rk.kennel.zone_id);
    EXPECT_EQ(3000,   rk.kennel.nightly_price_cents);
    EXPECT_GT(rk.kennel.rating, 0.0f);

    // Reasons must be non-empty so the UI and audit trail have explainability.
    EXPECT_FALSE(rk.reasons.empty());
    // Every reason must have a non-empty code and detail.
    for (const auto& r : rk.reasons) {
        EXPECT_FALSE(r.code.empty());
        EXPECT_FALSE(r.detail.empty());
    }
}

TEST_F(BookingSvcTest, SearchPersistsRecommendationResults) {
    BookingSearchFilter filter;
    filter.window.from_unix = 1000;
    filter.window.to_unix   = 2000;

    svc_->SearchAndRank(filter, ctx_, 100);
    std::string hash = ComputeQueryHash(filter);
    auto recs = booking_repo_->ListRecommendationsFor(hash);
    EXPECT_GE(recs.size(), 1u);
}

TEST_F(BookingSvcTest, CreateBookingHappyPath) {
    CreateBookingRequest req;
    req.kennel_id   = 1;
    req.check_in_at  = 1000;
    req.check_out_at = 2000;

    auto result = svc_->CreateBooking(req, ctx_, 500);
    ASSERT_TRUE(std::holds_alternative<int64_t>(result));
    EXPECT_GT(std::get<int64_t>(result), 0);

    auto booking = booking_repo_->FindById(std::get<int64_t>(result));
    ASSERT_TRUE(booking.has_value());
    EXPECT_EQ(BookingStatus::Pending, booking->status);
}

TEST_F(BookingSvcTest, CreateBookingOverlapFails) {
    CreateBookingRequest req;
    req.kennel_id   = 1;
    req.check_in_at  = 1000;
    req.check_out_at = 2000;

    auto r1 = svc_->CreateBooking(req, ctx_, 500);
    ASSERT_TRUE(std::holds_alternative<int64_t>(r1));

    // Try to create overlapping booking.
    req.check_in_at  = 1500;
    req.check_out_at = 2500;
    auto r2 = svc_->CreateBooking(req, ctx_, 501);
    ASSERT_TRUE(std::holds_alternative<ErrorEnvelope>(r2));
    EXPECT_EQ(ErrorCode::BookingConflict,
              std::get<ErrorEnvelope>(r2).code);
}

TEST_F(BookingSvcTest, ApproveBookingByAdminSucceeds) {
    CreateBookingRequest req;
    req.kennel_id = 1; req.check_in_at = 3000; req.check_out_at = 4000;
    auto cr = svc_->CreateBooking(req, ctx_, 500);
    int64_t bid = std::get<int64_t>(cr);

    auto result = svc_->ApproveBooking(bid, ctx_, 600);
    ASSERT_TRUE(std::holds_alternative<int64_t>(result));
    auto b = booking_repo_->FindById(bid);
    EXPECT_EQ(BookingStatus::Approved, b->status);
}

TEST_F(BookingSvcTest, ApproveBookingByInventoryClerkFails) {
    CreateBookingRequest req;
    req.kennel_id = 1; req.check_in_at = 5000; req.check_out_at = 6000;
    auto cr = svc_->CreateBooking(req, ctx_, 500);
    int64_t bid = std::get<int64_t>(cr);

    UserContext clerk_ctx;
    clerk_ctx.user_id = 1;
    clerk_ctx.role = UserRole::InventoryClerk;

    auto result = svc_->ApproveBooking(bid, clerk_ctx, 600);
    ASSERT_TRUE(std::holds_alternative<ErrorEnvelope>(result));
}

TEST_F(BookingSvcTest, AuditRowWrittenOnCreate) {
    CreateBookingRequest req;
    req.kennel_id = 1; req.check_in_at = 7000; req.check_out_at = 8000;
    svc_->CreateBooking(req, ctx_, 500);

    bool found = false;
    {
        auto g = db_->Acquire();
        g->Query("SELECT event_type FROM audit_events WHERE event_type='BOOKING_CREATED'",
                 {}, [&found](const auto&, const auto&) { found = true; });
    }
    EXPECT_TRUE(found);
}

TEST_F(BookingSvcTest, ContactFieldsAreEncryptedBeforePersistence) {
    CreateBookingRequest req;
    req.kennel_id = 1;
    req.check_in_at = 9000;
    req.check_out_at = 10000;
    req.guest_phone_enc = "5551239876";
    req.guest_email_enc = "guest@example.org";

    auto result = svc_->CreateBooking(req, ctx_, 500);
    ASSERT_TRUE(std::holds_alternative<int64_t>(result));
    auto booking = booking_repo_->FindById(std::get<int64_t>(result));
    ASSERT_TRUE(booking.has_value());
    EXPECT_NE(req.guest_phone_enc, booking->guest_phone_enc);
    EXPECT_NE(req.guest_email_enc, booking->guest_email_enc);

    auto key_entry = vault_->Load(kVaultKeyDataKey);
    ASSERT_TRUE(key_entry.has_value());
    EXPECT_EQ(req.guest_phone_enc,
              CryptoHelper::Decrypt(HexDecode(booking->guest_phone_enc), key_entry->data));
    EXPECT_EQ(req.guest_email_enc,
              CryptoHelper::Decrypt(HexDecode(booking->guest_email_enc), key_entry->data));
}
