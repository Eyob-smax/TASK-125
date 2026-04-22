#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/infrastructure/CrashCheckpoint.h"
#include "shelterops/infrastructure/CredentialVault.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/SchedulerRepository.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/BookingService.h"
#include "shelterops/services/InventoryService.h"
#include "shelterops/services/ReportService.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/services/AlertService.h"
#include "shelterops/services/SchedulerService.h"
#include "shelterops/services/AdminService.h"
#include "shelterops/services/ConsentService.h"
#include "shelterops/services/CheckpointService.h"
#include "shelterops/shell/SessionContext.h"
#include "shelterops/shell/TrayBadgeState.h"
#include "shelterops/ui/controllers/KennelBoardController.h"
#include "shelterops/ui/controllers/ItemLedgerController.h"
#include "shelterops/ui/controllers/ReportsController.h"
#include "shelterops/ui/controllers/GlobalSearchController.h"
#include "shelterops/ui/controllers/AdminPanelController.h"
#include "shelterops/ui/controllers/AuditLogController.h"
#include "shelterops/ui/controllers/AlertsPanelController.h"
#include "shelterops/ui/controllers/SchedulerPanelController.h"
#include "shelterops/ui/controllers/AppController.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::shell;
using namespace shelterops::ui::controllers;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    // Users
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    // Audit
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "occurred_at INTEGER NOT NULL, actor_user_id INTEGER, actor_role TEXT, "
            "event_type TEXT NOT NULL, entity_type TEXT, entity_id INTEGER, "
            "description TEXT NOT NULL, session_id TEXT)");
    // Zones + kennels
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
    // Bookings
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
    // Inventory
    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
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
    g->Exec("CREATE TABLE outbound_records(record_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, quantity INTEGER NOT NULL, "
            "issued_at INTEGER NOT NULL, issued_by INTEGER NOT NULL, "
            "recipient TEXT, reason TEXT NOT NULL, booking_id INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE alert_states(alert_id INTEGER PRIMARY KEY, "
            "item_id INTEGER NOT NULL, alert_type TEXT NOT NULL, "
            "triggered_at INTEGER NOT NULL, acknowledged_at INTEGER, acknowledged_by INTEGER, "
            "UNIQUE(item_id,alert_type,triggered_at))");
    // Maintenance
    g->Exec("CREATE TABLE maintenance_tickets(ticket_id INTEGER PRIMARY KEY, "
            "zone_id INTEGER, kennel_id INTEGER, title TEXT NOT NULL, description TEXT, "
            "priority TEXT NOT NULL DEFAULT 'normal', status TEXT NOT NULL DEFAULT 'open', "
            "created_at INTEGER NOT NULL, created_by INTEGER, assigned_to INTEGER, "
            "first_action_at INTEGER, resolved_at INTEGER)");
    g->Exec("CREATE TABLE maintenance_events(event_id INTEGER PRIMARY KEY, "
            "ticket_id INTEGER NOT NULL, actor_id INTEGER, event_type TEXT NOT NULL, "
            "notes TEXT, occurred_at INTEGER NOT NULL)");
    // Reports
    g->Exec("CREATE TABLE report_definitions(definition_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, report_type TEXT NOT NULL, "
            "description TEXT, is_active INTEGER NOT NULL DEFAULT 1, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE report_runs(run_id INTEGER PRIMARY KEY, "
            "definition_id INTEGER NOT NULL, version_label TEXT NOT NULL, "
            "trigger_type TEXT NOT NULL, triggered_by INTEGER, "
            "filter_json TEXT, status TEXT NOT NULL DEFAULT 'queued', "
            "error_message TEXT, anomaly_flags_json TEXT, "
            "output_path TEXT, row_count INTEGER, "
            "started_at INTEGER, completed_at INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE report_snapshots(snapshot_id INTEGER PRIMARY KEY, "
            "run_id INTEGER NOT NULL, metric_name TEXT NOT NULL, "
            "metric_value REAL NOT NULL, snapshot_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE export_jobs(job_id INTEGER PRIMARY KEY, "
            "run_id INTEGER NOT NULL, format TEXT NOT NULL, "
            "requested_by INTEGER, requested_at INTEGER NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'queued', output_path TEXT, "
            "error_message TEXT, completed_at INTEGER)");
    g->Exec("CREATE TABLE watermark_rules(rule_id INTEGER PRIMARY KEY, "
            "report_type TEXT NOT NULL, metric_name TEXT NOT NULL, "
            "lower_bound REAL, upper_bound REAL, is_active INTEGER NOT NULL DEFAULT 1)");
    // Admin
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
    // Scheduler
    g->Exec("CREATE TABLE scheduled_jobs(job_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL UNIQUE, job_type TEXT NOT NULL, parameters_json TEXT NOT NULL DEFAULT '{}', "
            "cron_expression TEXT, priority INTEGER NOT NULL DEFAULT 5, max_concurrency INTEGER NOT NULL DEFAULT 4, "
            "is_active INTEGER NOT NULL DEFAULT 1, last_run_at INTEGER, next_run_at INTEGER, "
            "created_by INTEGER, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE job_runs(run_id INTEGER PRIMARY KEY, "
            "job_id INTEGER NOT NULL, worker_id TEXT, started_at INTEGER NOT NULL, completed_at INTEGER, "
            "status TEXT NOT NULL DEFAULT 'queued', error_message TEXT, output_json TEXT)");
    g->Exec("CREATE TABLE job_dependencies(dependency_id INTEGER PRIMARY KEY, "
            "job_id INTEGER NOT NULL, depends_on_job_id INTEGER NOT NULL, UNIQUE(job_id,depends_on_job_id))");
    g->Exec("CREATE TABLE worker_leases(lease_id INTEGER PRIMARY KEY, worker_id TEXT NOT NULL, "
            "job_run_id INTEGER NOT NULL, acquired_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1)");
    // Checkpoint
    g->Exec("CREATE TABLE crash_checkpoints(checkpoint_id INTEGER PRIMARY KEY, "
            "window_state TEXT NOT NULL, form_state TEXT NOT NULL, saved_at INTEGER NOT NULL)");
}

class AppCtrlTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_             = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);

        kennel_repo_    = std::make_unique<KennelRepository>(*db_);
        booking_repo_   = std::make_unique<BookingRepository>(*db_);
        admin_repo_     = std::make_unique<AdminRepository>(*db_);
        audit_repo_     = std::make_unique<AuditRepository>(*db_);
        inv_repo_       = std::make_unique<InventoryRepository>(*db_);
        report_repo_    = std::make_unique<ReportRepository>(*db_);
        maint_repo_     = std::make_unique<MaintenanceRepository>(*db_);
                scheduler_repo_ = std::make_unique<SchedulerRepository>(*db_);
        audit_svc_      = std::make_unique<AuditService>(*audit_repo_);
        booking_vault_  = std::make_unique<InMemoryCredentialVault>();
        booking_svc_    = std::make_unique<BookingService>(
            *kennel_repo_, *booking_repo_, *admin_repo_, *booking_vault_, *audit_svc_);
        inv_svc_        = std::make_unique<InventoryService>(*inv_repo_, *audit_svc_);
        report_svc_     = std::make_unique<ReportService>(
            *report_repo_, *kennel_repo_, *booking_repo_, *inv_repo_, *maint_repo_, *audit_svc_);
        export_svc_     = std::make_unique<ExportService>(*report_repo_, *admin_repo_, *audit_svc_);
                alert_svc_      = std::make_unique<AlertService>(*inv_repo_, *audit_svc_);
                scheduler_svc_  = std::make_unique<SchedulerService>(*scheduler_repo_, *audit_svc_);
                admin_svc_      = std::make_unique<AdminService>(*admin_repo_, *audit_svc_);
                consent_svc_    = std::make_unique<ConsentService>(*admin_repo_, *audit_svc_);
        crash_cp_       = std::make_unique<CrashCheckpoint>(*db_);
        checkpoint_svc_ = std::make_unique<CheckpointService>(*crash_cp_);

        kennel_ctrl_    = std::make_unique<KennelBoardController>(*booking_svc_, *kennel_repo_);
        ledger_ctrl_    = std::make_unique<ItemLedgerController>(*inv_svc_, *inv_repo_);
        reports_ctrl_   = std::make_unique<ReportsController>(*report_svc_, *export_svc_, *report_repo_);
        search_ctrl_    = std::make_unique<GlobalSearchController>(
            *kennel_repo_, *booking_repo_, *inv_repo_, *report_repo_, *audit_repo_);
                admin_panel_ctrl_ = std::make_unique<AdminPanelController>(
                        *admin_svc_, *booking_svc_, *consent_svc_, *admin_repo_, *booking_repo_);
                audit_log_ctrl_ = std::make_unique<AuditLogController>(*audit_repo_);
                alerts_panel_ctrl_ = std::make_unique<AlertsPanelController>(*alert_svc_, tray_badge_state_);
                scheduler_panel_ctrl_ = std::make_unique<SchedulerPanelController>(
                        *scheduler_svc_, *scheduler_repo_);

        session_        = std::make_unique<SessionContext>();
        app_ctrl_       = std::make_unique<AppController>(
            *kennel_ctrl_, *ledger_ctrl_, *reports_ctrl_, *search_ctrl_,
                        *admin_panel_ctrl_, *audit_log_ctrl_, *alerts_panel_ctrl_, *scheduler_panel_ctrl_,
            *checkpoint_svc_, *session_);
    }

    std::unique_ptr<Database>               db_;
    std::unique_ptr<InMemoryCredentialVault> booking_vault_;
    std::unique_ptr<KennelRepository>       kennel_repo_;
    std::unique_ptr<BookingRepository>      booking_repo_;
    std::unique_ptr<AdminRepository>        admin_repo_;
    std::unique_ptr<AuditRepository>        audit_repo_;
    std::unique_ptr<InventoryRepository>    inv_repo_;
    std::unique_ptr<ReportRepository>       report_repo_;
    std::unique_ptr<MaintenanceRepository>  maint_repo_;
        std::unique_ptr<SchedulerRepository>    scheduler_repo_;
    std::unique_ptr<AuditService>           audit_svc_;
    std::unique_ptr<BookingService>         booking_svc_;
    std::unique_ptr<InventoryService>       inv_svc_;
    std::unique_ptr<ReportService>          report_svc_;
    std::unique_ptr<ExportService>          export_svc_;
        std::unique_ptr<AlertService>           alert_svc_;
        std::unique_ptr<SchedulerService>       scheduler_svc_;
        std::unique_ptr<AdminService>           admin_svc_;
        std::unique_ptr<ConsentService>         consent_svc_;
    std::unique_ptr<CrashCheckpoint>        crash_cp_;
    std::unique_ptr<CheckpointService>      checkpoint_svc_;
    std::unique_ptr<KennelBoardController>  kennel_ctrl_;
    std::unique_ptr<ItemLedgerController>   ledger_ctrl_;
    std::unique_ptr<ReportsController>      reports_ctrl_;
    std::unique_ptr<GlobalSearchController> search_ctrl_;
        std::unique_ptr<AdminPanelController>   admin_panel_ctrl_;
        std::unique_ptr<AuditLogController>     audit_log_ctrl_;
        std::unique_ptr<AlertsPanelController>  alerts_panel_ctrl_;
        std::unique_ptr<SchedulerPanelController> scheduler_panel_ctrl_;
        TrayBadgeState                          tray_badge_state_;
    std::unique_ptr<SessionContext>         session_;
    std::unique_ptr<AppController>          app_ctrl_;
};

TEST_F(AppCtrlTest, InitiallyNoWindowsOpen) {
    EXPECT_FALSE(app_ctrl_->IsWindowOpen(WindowId::KennelBoard));
    EXPECT_FALSE(app_ctrl_->IsWindowOpen(WindowId::ItemLedger));
    EXPECT_EQ(WindowId::None, app_ctrl_->GetActiveWindow());
}

TEST_F(AppCtrlTest, OpenWindowSetsOpen) {
    app_ctrl_->OpenWindow(WindowId::KennelBoard);
    EXPECT_TRUE(app_ctrl_->IsWindowOpen(WindowId::KennelBoard));
    EXPECT_FALSE(app_ctrl_->IsWindowOpen(WindowId::ItemLedger));
}

TEST_F(AppCtrlTest, CloseWindowClearsOpen) {
    app_ctrl_->OpenWindow(WindowId::KennelBoard);
    app_ctrl_->CloseWindow(WindowId::KennelBoard);
    EXPECT_FALSE(app_ctrl_->IsWindowOpen(WindowId::KennelBoard));
}

TEST_F(AppCtrlTest, SetActiveWindowTracked) {
    app_ctrl_->OpenWindow(WindowId::KennelBoard);
    app_ctrl_->OpenWindow(WindowId::ItemLedger);
    app_ctrl_->SetActiveWindow(WindowId::ItemLedger);
    EXPECT_EQ(WindowId::ItemLedger, app_ctrl_->GetActiveWindow());
}

TEST_F(AppCtrlTest, MultipleWindowsOpenSimultaneously) {
    app_ctrl_->OpenWindow(WindowId::KennelBoard);
    app_ctrl_->OpenWindow(WindowId::ItemLedger);
    app_ctrl_->OpenWindow(WindowId::ReportsStudio);
    EXPECT_TRUE(app_ctrl_->IsWindowOpen(WindowId::KennelBoard));
    EXPECT_TRUE(app_ctrl_->IsWindowOpen(WindowId::ItemLedger));
    EXPECT_TRUE(app_ctrl_->IsWindowOpen(WindowId::ReportsStudio));
}

TEST_F(AppCtrlTest, ProcessKeyEventCtrlFReturnsSearch) {
    app_ctrl_->OpenWindow(WindowId::KennelBoard);
    app_ctrl_->SetActiveWindow(WindowId::KennelBoard);

    UserContext ctx;
    ctx.user_id = 1; ctx.role = UserRole::OperationsManager;
    session_->Set(ctx);

    // Ctrl+F = 0x46
    auto action = app_ctrl_->ProcessKeyEvent(true, false, false, 0x46, 1000);
    EXPECT_EQ(ShortcutAction::BeginGlobalSearch, action);
}

TEST_F(AppCtrlTest, ProcessKeyEventCtrlShiftLReturnsLogout) {
    app_ctrl_->OpenWindow(WindowId::KennelBoard);
    app_ctrl_->SetActiveWindow(WindowId::KennelBoard);

    UserContext ctx;
    ctx.user_id = 1; ctx.role = UserRole::OperationsManager;
    session_->Set(ctx);

    // Ctrl+Shift+L = 0x4C
    auto action = app_ctrl_->ProcessKeyEvent(true, true, false, 0x4C, 1000);
    EXPECT_EQ(ShortcutAction::BeginLogout, action);
}

TEST_F(AppCtrlTest, CrossWindowRefreshFlag) {
    EXPECT_FALSE(app_ctrl_->HasCrossWindowRefresh());
    app_ctrl_->SetCrossWindowRefresh();
    EXPECT_TRUE(app_ctrl_->HasCrossWindowRefresh());
    app_ctrl_->ClearCrossWindowRefresh();
    EXPECT_FALSE(app_ctrl_->HasCrossWindowRefresh());
}

TEST_F(AppCtrlTest, CheckpointCaptureAndRestore) {
    app_ctrl_->OpenWindow(WindowId::KennelBoard);
    app_ctrl_->OpenWindow(WindowId::ItemLedger);
    app_ctrl_->SetActiveWindow(WindowId::KennelBoard);

    app_ctrl_->CaptureCheckpoint(5000);
    // Reset state
    app_ctrl_->CloseWindow(WindowId::KennelBoard);
    app_ctrl_->CloseWindow(WindowId::ItemLedger);
    EXPECT_FALSE(app_ctrl_->IsWindowOpen(WindowId::KennelBoard));

    bool restored = app_ctrl_->RestoreCheckpoint(6000);
    EXPECT_TRUE(restored);
    // After restore, kennel board should be open again
    EXPECT_TRUE(app_ctrl_->IsWindowOpen(WindowId::KennelBoard));
}

TEST_F(AppCtrlTest, ChildControllerAccessorsReturnSameObjects) {
    EXPECT_EQ(&app_ctrl_->KennelBoard(), kennel_ctrl_.get());
    EXPECT_EQ(&app_ctrl_->ItemLedger(),  ledger_ctrl_.get());
    EXPECT_EQ(&app_ctrl_->Reports(),     reports_ctrl_.get());
    EXPECT_EQ(&app_ctrl_->GlobalSearch(), search_ctrl_.get());
}
