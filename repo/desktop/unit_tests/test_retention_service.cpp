#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AnimalRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/RetentionService.h"
#include "shelterops/services/AuditService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using shelterops::domain::UserRole;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL, "
            "last_login_at INTEGER, consent_given INTEGER NOT NULL DEFAULT 0, "
            "anonymized_at INTEGER, failed_login_attempts INTEGER NOT NULL DEFAULT 0, locked_until INTEGER)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1,NULL,0,NULL,0,NULL)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
    g->Exec("CREATE TABLE zones(zone_id INTEGER PRIMARY KEY, name TEXT NOT NULL, building TEXT NOT NULL, "
            "row_label TEXT, x_coord_ft REAL DEFAULT 0, y_coord_ft REAL DEFAULT 0, description TEXT, is_active INTEGER DEFAULT 1)");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER, name TEXT NOT NULL, "
            "capacity INTEGER DEFAULT 1, current_purpose TEXT DEFAULT 'empty', "
            "nightly_price_cents INTEGER DEFAULT 0, rating REAL, is_active INTEGER DEFAULT 1, notes TEXT)");
    g->Exec("CREATE TABLE animals(animal_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "species TEXT NOT NULL, breed TEXT, age_years REAL, weight_lbs REAL, "
            "color TEXT, sex TEXT, microchip_id TEXT, is_aggressive INTEGER DEFAULT 0, "
            "is_large_dog INTEGER DEFAULT 0, intake_at INTEGER NOT NULL, "
            "intake_type TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'intake', "
            "notes TEXT, created_by INTEGER, anonymized_at INTEGER)");
    g->Exec("CREATE TABLE adoptable_listings(listing_id INTEGER PRIMARY KEY, "
            "animal_id INTEGER NOT NULL, kennel_id INTEGER, listing_date INTEGER NOT NULL, "
            "adoption_fee_cents INTEGER NOT NULL DEFAULT 0, description TEXT, "
            "rating REAL, status TEXT NOT NULL DEFAULT 'active', created_by INTEGER, adopted_at INTEGER)");
    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("INSERT INTO inventory_categories VALUES(1,'Food','bag',7,14,1)");
    g->Exec("CREATE TABLE inventory_items(item_id INTEGER PRIMARY KEY, "
            "category_id INTEGER NOT NULL, name TEXT NOT NULL, description TEXT, "
            "storage_location TEXT, quantity INTEGER NOT NULL DEFAULT 0, "
            "unit_cost_cents INTEGER NOT NULL DEFAULT 0, expiration_date INTEGER, "
            "serial_number TEXT UNIQUE, barcode TEXT, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, anonymized_at INTEGER)");
    g->Exec("CREATE TABLE item_usage_history(usage_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, period_date INTEGER NOT NULL, "
            "quantity_used INTEGER NOT NULL DEFAULT 0, UNIQUE(item_id,period_date))");
    g->Exec("CREATE TABLE inbound_records(record_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, quantity INTEGER NOT NULL, "
            "received_at INTEGER NOT NULL, received_by INTEGER NOT NULL, "
            "vendor TEXT, unit_cost_cents INTEGER NOT NULL DEFAULT 0, lot_number TEXT, notes TEXT)");
    g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER, animal_id INTEGER, "
            "guest_name TEXT NOT NULL, guest_phone_enc TEXT, guest_email_enc TEXT, "
            "check_in_at INTEGER, check_out_at INTEGER, status TEXT, "
            "nightly_price_cents INTEGER DEFAULT 0, total_price_cents INTEGER DEFAULT 0, "
            "special_requirements TEXT, created_by INTEGER, created_at INTEGER, "
            "approved_by INTEGER, approved_at INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE booking_approvals(approval_id INTEGER PRIMARY KEY, booking_id INTEGER NOT NULL, "
            "requested_by INTEGER NOT NULL, requested_at INTEGER NOT NULL, approver_id INTEGER, "
            "decision TEXT, decided_at INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE boarding_fees(fee_id INTEGER PRIMARY KEY, booking_id INTEGER NOT NULL, "
            "amount_cents INTEGER NOT NULL, due_at INTEGER NOT NULL, paid_at INTEGER, "
            "created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE outbound_records(record_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, quantity INTEGER NOT NULL, "
            "issued_at INTEGER NOT NULL, issued_by INTEGER NOT NULL, "
            "recipient TEXT, reason TEXT NOT NULL, booking_id INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE alert_states(alert_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, alert_type TEXT NOT NULL, "
            "triggered_at INTEGER NOT NULL, acknowledged_at INTEGER, acknowledged_by INTEGER, "
            "UNIQUE(item_id,alert_type,triggered_at))");
    g->Exec("CREATE TABLE maintenance_tickets(ticket_id INTEGER PRIMARY KEY, "
            "zone_id INTEGER, kennel_id INTEGER, title TEXT NOT NULL, description TEXT, "
            "priority TEXT NOT NULL DEFAULT 'normal', status TEXT NOT NULL DEFAULT 'open', "
            "created_at INTEGER NOT NULL, created_by INTEGER, assigned_to INTEGER, "
            "first_action_at INTEGER, resolved_at INTEGER)");
    g->Exec("CREATE TABLE maintenance_events(event_id INTEGER PRIMARY KEY, "
            "ticket_id INTEGER NOT NULL, actor_id INTEGER NOT NULL, event_type TEXT NOT NULL, "
            "old_status TEXT, new_status TEXT, notes TEXT, occurred_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, updated_by INTEGER, updated_at INTEGER NOT NULL)");
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
    g->Exec("INSERT INTO retention_policies(entity_type,retention_years,action,updated_at) "
            "VALUES('animals',1,'anonymize',1)");   // 1-year retention
    g->Exec("INSERT INTO retention_policies(entity_type,retention_years,action,updated_at) "
            "VALUES('inventory_items',1,'anonymize',1)");
    g->Exec("CREATE TABLE consent_records(consent_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL, entity_id INTEGER NOT NULL, "
            "consent_type TEXT NOT NULL, given_at INTEGER NOT NULL, withdrawn_at INTEGER)");
    g->Exec("CREATE TABLE export_permissions(permission_id INTEGER PRIMARY KEY, "
            "role TEXT NOT NULL, report_type TEXT NOT NULL, "
            "csv_allowed INTEGER NOT NULL DEFAULT 0, pdf_allowed INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(role,report_type))");
}

class RetentionSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_           = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        user_repo_    = std::make_unique<UserRepository>(*db_);
                booking_repo_ = std::make_unique<BookingRepository>(*db_);
        animal_repo_  = std::make_unique<AnimalRepository>(*db_);
        inv_repo_     = std::make_unique<InventoryRepository>(*db_);
        maint_repo_   = std::make_unique<MaintenanceRepository>(*db_);
        admin_repo_   = std::make_unique<AdminRepository>(*db_);
        audit_repo_   = std::make_unique<AuditRepository>(*db_);
        audit_svc_    = std::make_unique<AuditService>(*audit_repo_);
        svc_          = std::make_unique<RetentionService>(
                        *user_repo_, *booking_repo_, *animal_repo_, *inv_repo_, *maint_repo_, *admin_repo_, *audit_svc_);
        ctx_.user_id = 1; ctx_.role = UserRole::Administrator;
    }

    std::unique_ptr<Database>               db_;
    std::unique_ptr<UserRepository>         user_repo_;
        std::unique_ptr<BookingRepository>      booking_repo_;
    std::unique_ptr<AnimalRepository>       animal_repo_;
    std::unique_ptr<InventoryRepository>    inv_repo_;
    std::unique_ptr<MaintenanceRepository>  maint_repo_;
    std::unique_ptr<AdminRepository>        admin_repo_;
    std::unique_ptr<AuditRepository>        audit_repo_;
    std::unique_ptr<AuditService>           audit_svc_;
    std::unique_ptr<RetentionService>       svc_;
    UserContext                             ctx_;
};

TEST_F(RetentionSvcTest, OldAnimalGetsAnonymized) {
    // Policy: 1-year retention. Animal with intake 3 years ago should be anonymized.
    int64_t now = 1000000000LL;
    int64_t three_years_ago = now - 3LL * 365 * 86400;

    NewAnimalParams p;
        p.name = "OldDog"; p.species = shelterops::domain::AnimalSpecies::Dog; p.intake_type = "stray";
        p.created_by = 1;
        int64_t aid = animal_repo_->Insert(p, three_years_ago);

    auto report = svc_->Run(now, ctx_);
    EXPECT_GE(report.total_candidates, 1);

    auto a = animal_repo_->FindById(aid);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ("[anonymized]", a->name);
}

TEST_F(RetentionSvcTest, RecentAnimalNotAnonymized) {
    int64_t now = 1000000000LL;
    int64_t recent = now - 30 * 86400; // 30 days ago, well within 1-year policy

    NewAnimalParams p;
        p.name = "Puppy"; p.species = shelterops::domain::AnimalSpecies::Cat; p.intake_type = "stray";
        p.created_by = 1;
        int64_t aid = animal_repo_->Insert(p, recent);

    svc_->Run(now, ctx_);

    auto a = animal_repo_->FindById(aid);
    ASSERT_TRUE(a.has_value());
    EXPECT_NE("[anonymized]", a->name);
}

TEST_F(RetentionSvcTest, AuditEventsTableNeverTouched) {
    // Insert an audit event and verify it's never modified.
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO audit_events(event_type,description,occurred_at) "
                "VALUES('TEST','test event',100)");
    }

    int64_t now = 1000000000LL;
    svc_->Run(now, ctx_);

    int count = 0;
    {
        auto g = db_->Acquire();
        g->Query("SELECT COUNT(*) FROM audit_events", {},
                 [&count](const auto&, const auto& v) { count = std::stoi(v[0]); });
    }
    // Should have at least the original 1; RetentionService only adds, never removes.
    EXPECT_GE(count, 1);
}

TEST_F(RetentionSvcTest, OldUserGetsAnonymizedWhenPolicyConfigured) {
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO users(user_id,username,display_name,password_hash,role,is_active,created_at,consent_given,failed_login_attempts) "
                "VALUES(2,'legacy.user','Legacy User','hash','inventory_clerk',1,1,1,0)");
        g->Exec("INSERT OR REPLACE INTO retention_policies(entity_type,retention_years,action,updated_at) "
                "VALUES('users',1,'anonymize',1)");
    }

    int64_t now = 3LL * 365 * 86400;
    auto report = svc_->Run(now, ctx_);
    EXPECT_GE(report.total_candidates, 1);

    auto u = user_repo_->FindById(2);
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ("[anonymized]", u->display_name);
    EXPECT_FALSE(u->is_active);
}

TEST_F(RetentionSvcTest, OldBookingGetsAnonymizedWhenPolicyConfigured) {
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO bookings(booking_id,kennel_id,animal_id,guest_name,guest_phone_enc,guest_email_enc, "
                "check_in_at,check_out_at,status,nightly_price_cents,total_price_cents,special_requirements, "
                "created_by,created_at,approved_by,approved_at,notes) "
                "VALUES(77,1,NULL,'Jane Guest','enc-phone','enc-email',10,20,'completed',100,200,'none',1,1,NULL,NULL,'private')");
        g->Exec("INSERT OR REPLACE INTO retention_policies(entity_type,retention_years,action,updated_at) "
                "VALUES('bookings',1,'anonymize',1)");
    }

    int64_t now = 3LL * 365 * 86400;
    svc_->Run(now, ctx_);

    auto b = booking_repo_->FindById(77);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ("[anonymized]", b->guest_name);
    EXPECT_TRUE(b->guest_phone_enc.empty());
    EXPECT_TRUE(b->guest_email_enc.empty());
}

TEST_F(RetentionSvcTest, DeleteFailureFallsBackToBookingAnonymize) {
    {
        auto g = db_->Acquire();
        g->Exec("INSERT INTO bookings(booking_id,kennel_id,animal_id,guest_name,guest_phone_enc,guest_email_enc, "
                "check_in_at,check_out_at,status,nightly_price_cents,total_price_cents,special_requirements, "
                "created_by,created_at,approved_by,approved_at,notes) "
                "VALUES(88,1,NULL,'Delete Me','enc-phone','enc-email',10,20,'completed',100,200,'none',1,1,NULL,NULL,'private')");
        g->Exec("CREATE TRIGGER prevent_booking_delete BEFORE DELETE ON bookings "
                "BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        g->Exec("INSERT OR REPLACE INTO retention_policies(entity_type,retention_years,action,updated_at) "
                "VALUES('bookings',1,'delete',1)");
    }

    int64_t now = 3LL * 365 * 86400;
    svc_->Run(now, ctx_);

    auto b = booking_repo_->FindById(88);
    ASSERT_TRUE(b.has_value()) << "Delete should fail and fallback to anonymize";
    EXPECT_EQ("[anonymized]", b->guest_name);
}
