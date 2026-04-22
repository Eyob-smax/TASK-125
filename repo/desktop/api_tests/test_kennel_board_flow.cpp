#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/CredentialVault.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"
#include "shelterops/ui/controllers/KennelBoardController.h"
#include <filesystem>

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::ui::controllers;
using namespace shelterops::domain;

// Full integration scenario: filter → search → book → approve flow
// via the KennelBoardController (the same code path the view calls).

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'mgr','Manager','h','operations_manager',1,1)");
    g->Exec("INSERT INTO users VALUES(2,'clerk','Clerk','h','inventory_clerk',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "building TEXT NOT NULL DEFAULT '', row_label TEXT, "
            "x_coord_ft REAL NOT NULL DEFAULT 0, y_coord_ft REAL NOT NULL DEFAULT 0, "
            "description TEXT, is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE zone_distance_cache(from_zone_id INTEGER NOT NULL, "
            "to_zone_id INTEGER NOT NULL, distance_ft REAL NOT NULL, "
            "PRIMARY KEY(from_zone_id, to_zone_id))");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'boarding', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL DEFAULT 0, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
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
    g->Exec("INSERT INTO system_policies(key,value,updated_at) VALUES('booking_approval_required','false',1)");
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
    // Seed two kennels in the same zone
    g->Exec("INSERT INTO zones VALUES(1,'Main Wing','Shelter',NULL,0.0,0.0,NULL,1)");
    g->Exec("INSERT INTO kennels VALUES(1,1,'Suite A',1,'boarding',3000,4.5,1,NULL)");
    g->Exec("INSERT INTO kennels VALUES(2,1,'Suite B',2,'boarding',2500,4.0,1,NULL)");
}

class KennelBoardFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
                std::error_code ec;
                std::filesystem::remove("test_kennel_board_flow.db", ec);
                db_           = std::make_unique<Database>("test_kennel_board_flow.db");
        CreateSchema(*db_);
        kennel_repo_  = std::make_unique<KennelRepository>(*db_);
        booking_repo_ = std::make_unique<BookingRepository>(*db_);
        admin_repo_   = std::make_unique<AdminRepository>(*db_);
        audit_repo_   = std::make_unique<AuditRepository>(*db_);
        audit_svc_    = std::make_unique<AuditService>(*audit_repo_);
        booking_vault_ = std::make_unique<InMemoryCredentialVault>();
        booking_svc_  = std::make_unique<BookingService>(
            *kennel_repo_, *booking_repo_, *admin_repo_, *booking_vault_, *audit_svc_);
        ctrl_ = std::make_unique<KennelBoardController>(*booking_svc_, *kennel_repo_);

        mgr_ctx_.user_id = 1; mgr_ctx_.role = UserRole::OperationsManager;
        clerk_ctx_.user_id = 2; clerk_ctx_.role = UserRole::InventoryClerk;
    }

        void TearDown() override {
                std::error_code ec;
                std::filesystem::remove("test_kennel_board_flow.db", ec);
        }

    std::unique_ptr<Database>              db_;
    std::unique_ptr<InMemoryCredentialVault> booking_vault_;
    std::unique_ptr<KennelRepository>      kennel_repo_;
    std::unique_ptr<BookingRepository>     booking_repo_;
    std::unique_ptr<AdminRepository>       admin_repo_;
    std::unique_ptr<AuditRepository>       audit_repo_;
    std::unique_ptr<AuditService>          audit_svc_;
    std::unique_ptr<BookingService>        booking_svc_;
    std::unique_ptr<KennelBoardController> ctrl_;
    UserContext                            mgr_ctx_;
    UserContext                            clerk_ctx_;
};

TEST_F(KennelBoardFlowTest, FullSearchAndBookFlow) {
    // 1. Set filter and refresh
    KennelBoardFilter f;
    f.check_in_at  = 10000;
    f.check_out_at = 20000;
    f.only_bookable = true;
    ctrl_->SetFilter(f);
    ctrl_->Refresh(mgr_ctx_, 5000);
    ASSERT_EQ(KennelBoardState::Loaded, ctrl_->State());
    ASSERT_GE(ctrl_->Results().size(), 1u);

    // 2. Select kennel and begin booking
    int64_t kennel_id = ctrl_->Results()[0].kennel.kennel_id;
    ctrl_->BeginCreateBooking(kennel_id);
    EXPECT_EQ(KennelBoardState::CreatingBooking, ctrl_->State());

    // 3. Fill form and submit
    ctrl_->FormState().guest_name    = "Jane Doe";
    ctrl_->FormState().check_in_at   = 10000;
    ctrl_->FormState().check_out_at  = 20000;
    bool ok = ctrl_->SubmitBooking(mgr_ctx_, 5000);
    EXPECT_TRUE(ok);
    EXPECT_EQ(KennelBoardState::BookingSuccess, ctrl_->State());
}

TEST_F(KennelBoardFlowTest, ConflictOnDoubleBookSameKennel) {
    KennelBoardFilter f;
    f.check_in_at  = 10000;
    f.check_out_at = 20000;
    ctrl_->SetFilter(f);
    ctrl_->Refresh(mgr_ctx_, 5000);

    int64_t kennel_id = 1;  // Suite A (capacity=1)

    // First booking
    ctrl_->BeginCreateBooking(kennel_id);
    ctrl_->FormState().guest_name    = "Guest One";
    ctrl_->FormState().check_in_at   = 10000;
    ctrl_->FormState().check_out_at  = 20000;
    ASSERT_TRUE(ctrl_->SubmitBooking(mgr_ctx_, 5000));

    // Second booking — overlapping same kennel
    ctrl_->BeginCreateBooking(kennel_id);
    ctrl_->FormState().guest_name    = "Guest Two";
    ctrl_->FormState().check_in_at   = 12000;
    ctrl_->FormState().check_out_at  = 22000;
    bool ok = ctrl_->SubmitBooking(mgr_ctx_, 5001);
    EXPECT_FALSE(ok);
    EXPECT_EQ(KennelBoardState::BookingConflict, ctrl_->State());
}

TEST_F(KennelBoardFlowTest, Capacity2KennelAcceptsTwoConcurrentBookings) {
    // Suite B has capacity=2
    int64_t kennel_id = 2;

    KennelBoardFilter f;
    f.check_in_at  = 30000;
    f.check_out_at = 40000;
    ctrl_->SetFilter(f);
    ctrl_->Refresh(mgr_ctx_, 5000);

    // First booking
    ctrl_->BeginCreateBooking(kennel_id);
    ctrl_->FormState().guest_name   = "Guest Alpha";
    ctrl_->FormState().check_in_at  = 30000;
    ctrl_->FormState().check_out_at = 40000;
    ASSERT_TRUE(ctrl_->SubmitBooking(mgr_ctx_, 5000));

    // Second booking — same window, same kennel — capacity allows it
    ctrl_->BeginCreateBooking(kennel_id);
    ctrl_->FormState().guest_name   = "Guest Beta";
    ctrl_->FormState().check_in_at  = 30000;
    ctrl_->FormState().check_out_at = 40000;
    bool ok = ctrl_->SubmitBooking(mgr_ctx_, 5001);
    EXPECT_TRUE(ok) << "Capacity=2 kennel should accept a second concurrent booking";
}

TEST_F(KennelBoardFlowTest, OperationsManagerCanApproveBooking) {
    // Enable approval requirement to test approve path
    {
        auto g = db_->Acquire();
        g->Exec("UPDATE system_policies SET value='true' WHERE key='booking_approval_required'");
    }

    // Create a pending booking directly via service
    CreateBookingRequest req;
    req.kennel_id   = 1;
    req.check_in_at  = 50000;
    req.check_out_at = 60000;
    auto r = booking_svc_->CreateBooking(req, mgr_ctx_, 5000);
    ASSERT_TRUE(std::holds_alternative<int64_t>(r));
    int64_t bid = std::get<int64_t>(r);

    bool approved = ctrl_->ApproveBooking(bid, mgr_ctx_, 6000);
    EXPECT_TRUE(approved);

    auto booking = booking_repo_->FindById(bid);
    ASSERT_TRUE(booking.has_value());
        EXPECT_NE(BookingStatus::Pending, booking->status);
}

TEST_F(KennelBoardFlowTest, RecommendationResultsPersistedAfterSearch) {
    KennelBoardFilter f;
    f.check_in_at  = 70000;
    f.check_out_at = 80000;
    ctrl_->SetFilter(f);
    ctrl_->Refresh(mgr_ctx_, 5000);

    // Verify recommendation_results rows were written
    BookingSearchFilter sf;
    sf.window.from_unix = 70000;
    sf.window.to_unix   = 80000;
    std::string hash = ComputeQueryHash(sf);

    auto recs = booking_repo_->ListRecommendationsFor(hash);
    EXPECT_GE(recs.size(), 1u) << "SearchAndRank must persist recommendation_results";
}

TEST_F(KennelBoardFlowTest, CancelBookingTransitionsState) {
    CreateBookingRequest req;
    req.kennel_id   = 1;
    req.check_in_at  = 90000;
    req.check_out_at = 100000;
    auto r = booking_svc_->CreateBooking(req, mgr_ctx_, 5000);
    ASSERT_TRUE(std::holds_alternative<int64_t>(r));
    int64_t bid = std::get<int64_t>(r);

    bool ok = ctrl_->CancelBooking(bid, mgr_ctx_, 6000);
    EXPECT_TRUE(ok);

    auto booking = booking_repo_->FindById(bid);
    ASSERT_TRUE(booking.has_value());
        EXPECT_EQ(BookingStatus::Cancelled, booking->status);
}
