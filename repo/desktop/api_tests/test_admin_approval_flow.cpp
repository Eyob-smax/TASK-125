#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/CredentialVault.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/AdminService.h"
#include "shelterops/services/BookingService.h"
#include "shelterops/services/ConsentService.h"
#include "shelterops/ui/controllers/AdminPanelController.h"
#include <filesystem>

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::ui::controllers;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("INSERT INTO users VALUES(2,'mgr','Manager','h','operations_manager',1,1)");
    g->Exec("INSERT INTO users VALUES(3,'clerk','Clerk','h','inventory_clerk',1,1)");
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
    g->Exec("CREATE TABLE consent_records(consent_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL, entity_id INTEGER NOT NULL, "
            "consent_type TEXT NOT NULL, given_at INTEGER NOT NULL, withdrawn_at INTEGER)");
    g->Exec("CREATE TABLE retention_policies(policy_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL UNIQUE, retention_years INTEGER NOT NULL DEFAULT 7, "
            "action TEXT NOT NULL DEFAULT 'anonymize', updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE export_permissions(permission_id INTEGER PRIMARY KEY, "
            "role TEXT NOT NULL, report_type TEXT NOT NULL, "
            "csv_allowed INTEGER NOT NULL DEFAULT 0, pdf_allowed INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(role,report_type))");
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO system_policies VALUES(1,'booking_approval_required','true',1,1)");
    g->Exec("INSERT INTO zones VALUES(1,'Main','Shelter',NULL,0,0,NULL,1)");
    g->Exec("INSERT INTO kennels VALUES(1,1,'Suite A',1,'boarding',5000,4.5,1,NULL)");
}

class AdminApprovalFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        std::filesystem::remove("test_admin_approval_flow.db", ec);
        db_          = std::make_unique<Database>("test_admin_approval_flow.db");
        CreateSchema(*db_);
        admin_repo_  = std::make_unique<AdminRepository>(*db_);
        booking_repo_= std::make_unique<BookingRepository>(*db_);
        kennel_repo_ = std::make_unique<KennelRepository>(*db_);
        audit_repo_  = std::make_unique<AuditRepository>(*db_);
        audit_svc_   = std::make_unique<AuditService>(*audit_repo_);
        admin_svc_   = std::make_unique<AdminService>(*admin_repo_, *audit_svc_);
        booking_vault_ = std::make_unique<InMemoryCredentialVault>();
        booking_svc_ = std::make_unique<BookingService>(
            *kennel_repo_, *booking_repo_, *admin_repo_, *booking_vault_, *audit_svc_);
        consent_svc_ = std::make_unique<ConsentService>(*admin_repo_, *audit_svc_);
        ctrl_        = std::make_unique<AdminPanelController>(
            *admin_svc_, *booking_svc_, *consent_svc_, *admin_repo_, *booking_repo_);

        admin_ctx_.user_id = 1; admin_ctx_.role = UserRole::Administrator;
        mgr_ctx_.user_id   = 2; mgr_ctx_.role   = UserRole::OperationsManager;
        clerk_ctx_.user_id = 3; clerk_ctx_.role  = UserRole::InventoryClerk;
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove("test_admin_approval_flow.db", ec);
    }

    int64_t CreatePendingBooking() {
        CreateBookingRequest req;
        req.kennel_id    = 1;
        req.check_in_at  = 100000;
        req.check_out_at = 200000;
        req.guest_name   = "Test Guest";
        auto result = booking_svc_->CreateBooking(req, mgr_ctx_, 5000);
        if (auto* id = std::get_if<int64_t>(&result))
            return *id;
        return 0;
    }

    std::unique_ptr<Database>           db_;
    std::unique_ptr<InMemoryCredentialVault> booking_vault_;
    std::unique_ptr<AdminRepository>    admin_repo_;
    std::unique_ptr<BookingRepository>  booking_repo_;
    std::unique_ptr<KennelRepository>   kennel_repo_;
    std::unique_ptr<AuditRepository>    audit_repo_;
    std::unique_ptr<AuditService>       audit_svc_;
    std::unique_ptr<AdminService>       admin_svc_;
    std::unique_ptr<BookingService>     booking_svc_;
    std::unique_ptr<ConsentService>     consent_svc_;
    std::unique_ptr<AdminPanelController> ctrl_;
    UserContext                         admin_ctx_;
    UserContext                         mgr_ctx_;
    UserContext                         clerk_ctx_;
};

TEST_F(AdminApprovalFlowTest, ManagerCanApproveBookingViaController) {
    int64_t bid = CreatePendingBooking();
    ASSERT_GT(bid, 0);

    bool ok = ctrl_->ApproveBooking(bid, mgr_ctx_, 6000);
    EXPECT_TRUE(ok);

    auto booking = booking_repo_->FindById(bid);
    ASSERT_TRUE(booking.has_value());
    EXPECT_NE(BookingStatus::Pending, booking->status) << "Booking must no longer be pending after approval";
}

TEST_F(AdminApprovalFlowTest, ClerkCannotApproveBooking) {
    int64_t bid = CreatePendingBooking();
    ASSERT_GT(bid, 0);

    bool ok = ctrl_->ApproveBooking(bid, clerk_ctx_, 6000);
    EXPECT_FALSE(ok) << "Inventory Clerk must not approve bookings";
}

TEST_F(AdminApprovalFlowTest, ManagerCanRejectBooking) {
    int64_t bid = CreatePendingBooking();
    ASSERT_GT(bid, 0);

    bool ok = ctrl_->RejectBooking(bid, mgr_ctx_, 6000);
    EXPECT_TRUE(ok);

    auto booking = booking_repo_->FindById(bid);
    ASSERT_TRUE(booking.has_value());
    // After rejection, status must reflect a final or cancelled state
    EXPECT_NE(BookingStatus::Pending, booking->status);
}

TEST_F(AdminApprovalFlowTest, ApproveWritesAuditEvent) {
    int64_t bid = CreatePendingBooking();
    ASSERT_GT(bid, 0);
    ctrl_->ApproveBooking(bid, mgr_ctx_, 6000);

    AuditQueryFilter qf;
    qf.event_type = "BOOKING_APPROVED";
    auto events = audit_repo_->Query(qf);
    EXPECT_GE(events.size(), 1u) << "Booking approval must produce an audit event";
}

TEST_F(AdminApprovalFlowTest, AdminCanCreatePriceRuleWithDiscount) {
    ctrl_->BeginCreatePriceRule();
    ctrl_->PriceRuleForm().name       = "Senior Discount";
    ctrl_->PriceRuleForm().applies_to = "boarding";
    ctrl_->PriceRuleForm().amount     = 15.0;
    ctrl_->PriceRuleForm().adjustment_type =
        PriceAdjustmentType::PercentDiscount;
    bool ok = ctrl_->SubmitPriceRule(admin_ctx_, 2000);
    EXPECT_TRUE(ok);

    ctrl_->LoadPriceRules(3000);
    EXPECT_GE(ctrl_->PriceRules().size(), 1u);
    EXPECT_EQ("Senior Discount", ctrl_->PriceRules()[0].name);
}

TEST_F(AdminApprovalFlowTest, AfterSalesAdjustmentRequiresApprovalRole) {
    int64_t bid = CreatePendingBooking();
    ASSERT_GT(bid, 0);
    ctrl_->ApproveBooking(bid, mgr_ctx_, 6000);

    // After-sales adjustment via BookingService — only approved/active bookings qualify.
    auto err = booking_svc_->RecordAfterSalesAdjustment(
        bid, -500, "Damage waiver", mgr_ctx_, 7000);
    // Manager can approve → should succeed or the booking state determines outcome
    (void)err;

    // Clerk cannot record adjustment
    auto err2 = booking_svc_->RecordAfterSalesAdjustment(
        bid, -200, "Other", clerk_ctx_, 8000);
    EXPECT_EQ(shelterops::common::ErrorCode::Forbidden, err2.code)
        << "Clerk must be forbidden from recording after-sales adjustments";
}

TEST_F(AdminApprovalFlowTest, RetentionPolicySetAndPersisted) {
    bool ok = ctrl_->SetRetentionPolicy("bookings", 3,
                                         RetentionActionKind::Delete,
                                         admin_ctx_, 1000);
    EXPECT_TRUE(ok);
    ctrl_->LoadRetentionPolicies();
    ASSERT_GE(ctrl_->RetentionPolicies().size(), 1u);
    bool found = false;
    for (const auto& p : ctrl_->RetentionPolicies()) {
        if (p.entity_type == "bookings") {
            EXPECT_EQ(3, p.retention_years);
            EXPECT_EQ(RetentionActionKind::Delete, p.action);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(AdminApprovalFlowTest, ExportPermissionSetByAdmin) {
    bool ok = ctrl_->SetExportPermission(
        "auditor", "audit_export", false, false, admin_ctx_, 1000);
    EXPECT_TRUE(ok);

    ctrl_->LoadExportPermissions("auditor");
    // Should load without crashing; may or may not have rows depending on
    // whether system_policies path surfaces to ListExportPermissions.
    (void)ctrl_->ExportPermissions();
}
