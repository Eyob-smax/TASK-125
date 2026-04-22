#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AdminService.h"
#include "shelterops/services/AuditService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::domain;
using namespace shelterops::common;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("INSERT INTO users VALUES(2,'clerk','Clerk','h','inventory_clerk',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
    g->Exec("CREATE TABLE product_catalog(entry_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, category_id INTEGER, default_unit_cost_cents INTEGER DEFAULT 0, "
            "vendor TEXT, sku TEXT UNIQUE, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO product_catalog VALUES(1,'Bandage',NULL,100,NULL,'SKU-001',1,1,1000)");
    g->Exec("CREATE TABLE price_rules(rule_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, applies_to TEXT NOT NULL, condition_json TEXT NOT NULL DEFAULT '{}', "
            "adjustment_type TEXT NOT NULL, amount REAL NOT NULL, "
            "valid_from INTEGER, valid_to INTEGER, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE retention_policies(policy_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL UNIQUE, retention_years INTEGER NOT NULL DEFAULT 7, "
            "action TEXT NOT NULL DEFAULT 'anonymize', updated_by INTEGER, updated_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE export_permissions(permission_id INTEGER PRIMARY KEY, "
            "role TEXT NOT NULL, report_type TEXT NOT NULL, "
            "csv_allowed INTEGER NOT NULL DEFAULT 0, pdf_allowed INTEGER NOT NULL DEFAULT 0, "
            "UNIQUE(role,report_type))");
    g->Exec("CREATE TABLE after_sales_adjustments(adjustment_id INTEGER PRIMARY KEY, "
            "booking_id INTEGER, amount_cents INTEGER NOT NULL, reason TEXT NOT NULL, "
            "approved_by INTEGER, created_by INTEGER NOT NULL, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE consent_records(consent_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL, entity_id INTEGER NOT NULL, "
            "consent_type TEXT NOT NULL, given_at INTEGER NOT NULL, withdrawn_at INTEGER)");
    g->Exec("CREATE TABLE system_policies(policy_id INTEGER PRIMARY KEY, "
            "key TEXT NOT NULL UNIQUE, value TEXT NOT NULL, updated_by INTEGER, updated_at INTEGER NOT NULL)");
}

static bool AuditEventExists(Database& db, const std::string& event_type) {
    bool found = false;
    db.Acquire()->Query(
        "SELECT 1 FROM audit_events WHERE event_type=?",
        {event_type},
        [&found](const auto&, const auto&) { found = true; });
    return found;
}

class AdminSvcTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_         = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        admin_repo_ = std::make_unique<AdminRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_  = std::make_unique<AuditService>(*audit_repo_);
        svc_        = std::make_unique<AdminService>(*admin_repo_, *audit_svc_);

        admin_ctx_.user_id = 1;
        admin_ctx_.role    = UserRole::Administrator;
        clerk_ctx_.user_id = 2;
        clerk_ctx_.role    = UserRole::InventoryClerk;
    }

    std::unique_ptr<Database>         db_;
    std::unique_ptr<AdminRepository>  admin_repo_;
    std::unique_ptr<AuditRepository>  audit_repo_;
    std::unique_ptr<AuditService>     audit_svc_;
    std::unique_ptr<AdminService>     svc_;
    UserContext                       admin_ctx_;
    UserContext                       clerk_ctx_;
};

// ─── UpdateCatalogEntry ────────────────────────────────────────────────────

TEST_F(AdminSvcTest, UpdateCatalogEntryAdminSucceeds) {
    auto err = svc_->UpdateCatalogEntry(1, "Premium Bandage", 150, true,
                                         admin_ctx_, 2000);
    // Success convention: Internal code with empty message.
    EXPECT_EQ(ErrorCode::Internal, err.code);
    EXPECT_TRUE(err.message.empty());
}

TEST_F(AdminSvcTest, UpdateCatalogEntryAdminWritesAuditEvent) {
    svc_->UpdateCatalogEntry(1, "Updated", 200, true, admin_ctx_, 2000);
    EXPECT_TRUE(AuditEventExists(*db_, "CATALOG_ENTRY_UPDATED"));
}

TEST_F(AdminSvcTest, UpdateCatalogEntryClerkForbidden) {
    auto err = svc_->UpdateCatalogEntry(1, "Hack", 0, false, clerk_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Forbidden, err.code);
    EXPECT_FALSE(err.message.empty());
}

TEST_F(AdminSvcTest, UpdateCatalogEntryClerkNoAuditRow) {
    svc_->UpdateCatalogEntry(1, "Hack", 0, false, clerk_ctx_, 2000);
    EXPECT_FALSE(AuditEventExists(*db_, "CATALOG_ENTRY_UPDATED"));
}

// ─── CreatePriceRule ───────────────────────────────────────────────────────

TEST_F(AdminSvcTest, CreatePriceRuleAdminSucceeds) {
    PriceRuleRecord r;
    r.name            = "Summer Discount";
    r.applies_to      = "boarding";
    r.condition_json  = "{}";
    r.adjustment_type = PriceAdjustmentType::PercentDiscount;
    r.amount          = 10.0;
    r.is_active       = true;
    r.created_by      = admin_ctx_.user_id;
    r.created_at      = 2000;

    auto err = svc_->CreatePriceRule(r, admin_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Internal, err.code);
    EXPECT_TRUE(err.message.empty());
}

TEST_F(AdminSvcTest, CreatePriceRuleAdminWritesAuditEvent) {
    PriceRuleRecord r;
    r.name = "Rule"; r.applies_to = "boarding";
    r.condition_json = "{}";
    r.adjustment_type = PriceAdjustmentType::FixedDiscountCents;
    r.amount = 500; r.is_active = true;
    r.created_by = admin_ctx_.user_id; r.created_at = 2000;

    svc_->CreatePriceRule(r, admin_ctx_, 2000);
    EXPECT_TRUE(AuditEventExists(*db_, "PRICE_RULE_CREATED"));
}

TEST_F(AdminSvcTest, CreatePriceRuleClerkForbidden) {
    PriceRuleRecord r;
    r.name = "Hack"; r.applies_to = "boarding";
    r.condition_json = "{}";
    r.adjustment_type = PriceAdjustmentType::FixedDiscountCents;
    r.amount = 0; r.is_active = true;
    r.created_by = clerk_ctx_.user_id; r.created_at = 2000;

    auto err = svc_->CreatePriceRule(r, clerk_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Forbidden, err.code);
}

TEST_F(AdminSvcTest, CreatePriceRulePersisted) {
    PriceRuleRecord r;
    r.name = "Persist Rule"; r.applies_to = "boarding";
    r.condition_json = "{}";
    r.adjustment_type = PriceAdjustmentType::PercentDiscount;
    r.amount = 5; r.is_active = true;
    r.created_by = admin_ctx_.user_id; r.created_at = 1000;

    svc_->CreatePriceRule(r, admin_ctx_, 1000);

    auto rules = admin_repo_->ListActivePriceRules(2000);
    ASSERT_FALSE(rules.empty());
    EXPECT_EQ("Persist Rule", rules[0].name);
}

// ─── DeactivatePriceRule ───────────────────────────────────────────────────

TEST_F(AdminSvcTest, DeactivatePriceRuleAdminSucceeds) {
    PriceRuleRecord r;
    r.name = "ToDeactivate"; r.applies_to = "boarding";
    r.condition_json = "{}";
    r.adjustment_type = PriceAdjustmentType::PercentDiscount;
    r.amount = 5; r.is_active = true;
    r.created_by = admin_ctx_.user_id; r.created_at = 1000;
    svc_->CreatePriceRule(r, admin_ctx_, 1000);

    auto rules = admin_repo_->ListActivePriceRules(2000);
    ASSERT_FALSE(rules.empty());
    int64_t rule_id = rules[0].rule_id;

    auto err = svc_->DeactivatePriceRule(rule_id, admin_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Internal, err.code);
    EXPECT_TRUE(err.message.empty());
}

TEST_F(AdminSvcTest, DeactivatePriceRuleAdminWritesAuditEvent) {
    PriceRuleRecord r;
    r.name = "ToDeact"; r.applies_to = "boarding";
    r.condition_json = "{}";
    r.adjustment_type = PriceAdjustmentType::PercentDiscount;
    r.amount = 5; r.is_active = true;
    r.created_by = admin_ctx_.user_id; r.created_at = 1000;
    svc_->CreatePriceRule(r, admin_ctx_, 1000);

    auto rules = admin_repo_->ListActivePriceRules(2000);
    ASSERT_FALSE(rules.empty());
    svc_->DeactivatePriceRule(rules[0].rule_id, admin_ctx_, 2000);
    EXPECT_TRUE(AuditEventExists(*db_, "PRICE_RULE_DEACTIVATED"));
}

TEST_F(AdminSvcTest, DeactivatePriceRuleClerkForbidden) {
    auto err = svc_->DeactivatePriceRule(1, clerk_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Forbidden, err.code);
}

TEST_F(AdminSvcTest, DeactivatedRuleAbsentFromActiveListing) {
    PriceRuleRecord r;
    r.name = "Gone"; r.applies_to = "boarding";
    r.condition_json = "{}";
    r.adjustment_type = PriceAdjustmentType::PercentDiscount;
    r.amount = 5; r.is_active = true;
    r.created_by = admin_ctx_.user_id; r.created_at = 1000;
    svc_->CreatePriceRule(r, admin_ctx_, 1000);

    auto before = admin_repo_->ListActivePriceRules(2000);
    ASSERT_FALSE(before.empty());
    int64_t rid = before[0].rule_id;
    svc_->DeactivatePriceRule(rid, admin_ctx_, 2000);

    auto after = admin_repo_->ListActivePriceRules(2000);
    for (auto& rule : after) EXPECT_NE(rid, rule.rule_id);
}

// ─── SetRetentionPolicy ────────────────────────────────────────────────────

TEST_F(AdminSvcTest, SetRetentionPolicyAdminSucceeds) {
    auto err = svc_->SetRetentionPolicy("users", 7,
                                         RetentionActionKind::Anonymize,
                                         admin_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Internal, err.code);
    EXPECT_TRUE(err.message.empty());
}

TEST_F(AdminSvcTest, SetRetentionPolicyAdminWritesAuditEvent) {
    svc_->SetRetentionPolicy("animals", 3, RetentionActionKind::Delete,
                              admin_ctx_, 2000);
    EXPECT_TRUE(AuditEventExists(*db_, "RETENTION_POLICY_UPDATED"));
}

TEST_F(AdminSvcTest, SetRetentionPolicyClerkForbidden) {
    auto err = svc_->SetRetentionPolicy("users", 7,
                                         RetentionActionKind::Anonymize,
                                         clerk_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Forbidden, err.code);
}

TEST_F(AdminSvcTest, SetRetentionPolicyPersisted) {
    svc_->SetRetentionPolicy("bookings", 5, RetentionActionKind::Anonymize,
                              admin_ctx_, 2000);
    auto policies = admin_repo_->ListRetentionPolicies();
    bool found = false;
    for (auto& p : policies) {
        if (p.entity_type == "bookings") {
            EXPECT_EQ(5, p.retention_years);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ─── SetExportPermission ───────────────────────────────────────────────────

TEST_F(AdminSvcTest, SetExportPermissionAdminSucceeds) {
    auto err = svc_->SetExportPermission("operations_manager", "occupancy",
                                          true, false, admin_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Internal, err.code);
    EXPECT_TRUE(err.message.empty());
}

TEST_F(AdminSvcTest, SetExportPermissionAdminWritesAuditEvent) {
    svc_->SetExportPermission("operations_manager", "census",
                               true, true, admin_ctx_, 2000);
    EXPECT_TRUE(AuditEventExists(*db_, "EXPORT_PERMISSION_UPDATED"));
}

TEST_F(AdminSvcTest, SetExportPermissionClerkForbidden) {
    auto err = svc_->SetExportPermission("administrator", "occupancy",
                                          true, true, clerk_ctx_, 2000);
    EXPECT_EQ(ErrorCode::Forbidden, err.code);
}

TEST_F(AdminSvcTest, SetExportPermissionPersisted) {
    svc_->SetExportPermission("auditor", "audit_log", false, false, admin_ctx_, 1000);
    auto perms = admin_repo_->ListExportPermissions("auditor");
    bool found = false;
    for (auto& p : perms) {
        if (p.report_type == "audit_log") { found = true; }
    }
    EXPECT_TRUE(found);
}
