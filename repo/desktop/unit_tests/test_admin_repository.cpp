#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/AdminRepository.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, "
            "updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
    g->Exec("CREATE TABLE product_catalog(entry_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, category_id INTEGER, default_unit_cost_cents INTEGER DEFAULT 0, "
            "vendor TEXT, sku TEXT UNIQUE, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER, "
            "check_in_at INTEGER, check_out_at INTEGER, status TEXT, "
            "nightly_price_cents INTEGER DEFAULT 0, total_price_cents INTEGER DEFAULT 0, "
            "created_at INTEGER, created_by INTEGER)");
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
    g->Exec("INSERT INTO export_permissions(role,report_type,csv_allowed,pdf_allowed) "
            "VALUES('administrator','occupancy',1,1)");
}

class AdminRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        repo_ = std::make_unique<AdminRepository>(*db_);
    }
    std::unique_ptr<Database>          db_;
    std::unique_ptr<AdminRepository>   repo_;
};

TEST_F(AdminRepoTest, PriceRuleInsertAndListActive) {
    PriceRuleRecord r;
    r.name = "Dog Discount"; r.applies_to = "boarding";
    r.condition_json = "{}"; r.adjustment_type = PriceAdjustmentType::PercentDiscount;
    r.amount = 10.0; r.valid_from = 0; r.valid_to = 9999999999LL;
    r.is_active = true; r.created_by = 1; r.created_at = 1000;

    repo_->InsertPriceRule(r);
    auto rules = repo_->ListActivePriceRules(5000);
    ASSERT_EQ(1u, rules.size());
    EXPECT_EQ("Dog Discount", rules[0].name);
}

TEST_F(AdminRepoTest, PriceRuleInactiveNotListed) {
    PriceRuleRecord r;
    r.name = "Inactive Rule"; r.applies_to = "boarding";
    r.condition_json = "{}"; r.adjustment_type = PriceAdjustmentType::FixedDiscountCents;
    r.amount = 500.0; r.valid_from = 0; r.valid_to = 9999999999LL;
    r.is_active = false; r.created_by = 1; r.created_at = 1000;

    repo_->InsertPriceRule(r);
    auto rules = repo_->ListActivePriceRules(5000);
    EXPECT_TRUE(rules.empty());
}

TEST_F(AdminRepoTest, RetentionPolicyUpsert) {
    repo_->UpsertRetentionPolicy("animals", 5, RetentionActionKind::Delete, 1, 1000);
    auto policies = repo_->ListRetentionPolicies();
    bool found = false;
    for (const auto& p : policies) {
        if (p.entity_type == "animals") {
            found = true;
            EXPECT_EQ(5, p.retention_years);
            EXPECT_EQ(RetentionActionKind::Delete, p.action);
        }
    }
    EXPECT_TRUE(found);

    // Upsert again; should update.
    repo_->UpsertRetentionPolicy("animals", 10, RetentionActionKind::Anonymize, 1, 2000);
    policies = repo_->ListRetentionPolicies();
    for (const auto& p : policies) {
        if (p.entity_type == "animals") {
            EXPECT_EQ(10, p.retention_years);
        }
    }
}

TEST_F(AdminRepoTest, ConsentGrantAndWithdraw) {
    int64_t cid = repo_->InsertConsent("users", 42, "data_processing", 1000);
    EXPECT_GT(cid, 0);

    auto list = repo_->ListConsentsFor("users", 42);
    ASSERT_EQ(1u, list.size());
    EXPECT_EQ(0, list[0].withdrawn_at);

    repo_->WithdrawConsent(cid, 2000);
    list = repo_->ListConsentsFor("users", 42);
    EXPECT_NE(0, list[0].withdrawn_at);
}

TEST_F(AdminRepoTest, CanExportChecksPermissions) {
    EXPECT_TRUE(repo_->CanExport("administrator", "occupancy", "csv"));
    EXPECT_TRUE(repo_->CanExport("administrator", "occupancy", "pdf"));
    EXPECT_FALSE(repo_->CanExport("inventory_clerk", "occupancy", "csv"));
}
