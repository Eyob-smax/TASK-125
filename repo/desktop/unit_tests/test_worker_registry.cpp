#include <gtest/gtest.h>
#include "shelterops/workers/WorkerRegistry.h"
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/MaintenanceRepository.h"
#include "shelterops/repositories/UserRepository.h"
#include "shelterops/repositories/AnimalRepository.h"
#include "shelterops/services/ReportService.h"
#include "shelterops/services/ExportService.h"
#include "shelterops/services/RetentionService.h"
#include "shelterops/services/AlertService.h"
#include "shelterops/services/AuditService.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <thread>

using namespace shelterops::workers;
using namespace shelterops::services;
using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::domain;

static void CreateMinimalSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "event_type TEXT NOT NULL, description TEXT NOT NULL, actor_id INTEGER, "
            "occurred_at INTEGER NOT NULL, entity_type TEXT, entity_id INTEGER)");
    g->Exec("CREATE TABLE report_runs(run_id INTEGER PRIMARY KEY, report_type TEXT NOT NULL, "
            "status TEXT NOT NULL, started_at INTEGER NOT NULL, completed_at INTEGER)");
    g->Exec("CREATE TABLE export_jobs(job_id INTEGER PRIMARY KEY, job_type TEXT NOT NULL, "
            "status TEXT NOT NULL, created_at INTEGER NOT NULL)");
    g->Exec("CREATE TABLE kennels(kennel_id INTEGER PRIMARY KEY, zone_id INTEGER NOT NULL, "
            "name TEXT NOT NULL, capacity INTEGER NOT NULL DEFAULT 1, "
            "current_purpose TEXT NOT NULL DEFAULT 'boarding', "
            "nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "rating REAL DEFAULT 0, is_active INTEGER NOT NULL DEFAULT 1, notes TEXT)");
    g->Exec("CREATE TABLE bookings(booking_id INTEGER PRIMARY KEY, kennel_id INTEGER NOT NULL, "
            "animal_id INTEGER, guest_name TEXT, guest_phone_enc TEXT, guest_email_enc TEXT, "
            "check_in_at INTEGER NOT NULL, check_out_at INTEGER NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'pending', nightly_price_cents INTEGER NOT NULL DEFAULT 0, "
            "total_price_cents INTEGER NOT NULL DEFAULT 0, special_requirements TEXT, "
            "created_by INTEGER, created_at INTEGER NOT NULL, "
            "approved_by INTEGER, approved_at INTEGER, notes TEXT)");
    g->Exec("CREATE TABLE animals(animal_id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "species TEXT NOT NULL, intake_at INTEGER NOT NULL, intake_type TEXT NOT NULL, "
            "status TEXT NOT NULL DEFAULT 'intake', is_aggressive INTEGER DEFAULT 0, "
            "is_large_dog INTEGER DEFAULT 0, breed TEXT, age_years REAL, weight_lbs REAL, "
            "color TEXT, sex TEXT, microchip_id TEXT, notes TEXT, created_by INTEGER, anonymized_at INTEGER)");
    g->Exec("CREATE TABLE inventory_categories(category_id INTEGER PRIMARY KEY, "
            "name TEXT NOT NULL, unit TEXT NOT NULL DEFAULT 'unit', "
            "low_stock_threshold_days INTEGER NOT NULL DEFAULT 7, "
            "expiration_alert_days INTEGER NOT NULL DEFAULT 14, is_active INTEGER NOT NULL DEFAULT 1)");
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
    g->Exec("CREATE TABLE maintenance_events(event_id INTEGER PRIMARY KEY, kennel_id INTEGER NOT NULL, "
            "event_type TEXT NOT NULL, description TEXT NOT NULL, created_at INTEGER NOT NULL, "
            "first_action_at INTEGER, resolved_at INTEGER)");
}

struct WaitState {
    JobOutcome result;
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
};

// Helper: submit a job, wait for outcome via callbacks (timeout 5 s).
// Uses shared_ptr so the callback lambda is safe even if the caller returns early.
static JobOutcome SubmitAndWait(JobQueue& queue, const JobDescriptor& desc) {
    auto state = std::make_shared<WaitState>();

    queue.SetLifecycleCallbacks(
        nullptr,
        [state](const JobDescriptor&, const JobOutcome& outcome, int64_t) {
            std::lock_guard<std::mutex> lk(state->mu);
            state->result = outcome;
            state->done = true;
            state->cv.notify_one();
        });

    queue.Submit(desc);

    std::unique_lock<std::mutex> lk(state->mu);
    state->cv.wait_for(lk, std::chrono::seconds(5), [&state]{ return state->done; });
    return state->result;
}

class WorkerRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = std::make_unique<Database>(":memory:");
        CreateMinimalSchema(*db_);

        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        report_repo_ = std::make_unique<ReportRepository>(*db_);
        admin_repo_ = std::make_unique<AdminRepository>(*db_);
        kennel_repo_ = std::make_unique<KennelRepository>(*db_);
        booking_repo_ = std::make_unique<BookingRepository>(*db_);
        inventory_repo_ = std::make_unique<InventoryRepository>(*db_);
        maintenance_repo_ = std::make_unique<MaintenanceRepository>(*db_);
        user_repo_ = std::make_unique<UserRepository>(*db_);
        animal_repo_ = std::make_unique<AnimalRepository>(*db_);

        audit_svc_ = std::make_unique<AuditService>(*audit_repo_);
        report_svc_ = std::make_unique<ReportService>(*report_repo_, *kennel_repo_, *booking_repo_,
                                                       *inventory_repo_, *maintenance_repo_, *audit_svc_, "exports");
        export_svc_ = std::make_unique<ExportService>(*report_repo_, *admin_repo_, *audit_svc_);
        retention_svc_ = std::make_unique<RetentionService>(*user_repo_, *booking_repo_, *animal_repo_,
                                                             *inventory_repo_, *maintenance_repo_, *admin_repo_, *audit_svc_);
        alert_svc_ = std::make_unique<AlertService>(*inventory_repo_, *audit_svc_);

        queue_ = std::make_unique<JobQueue>();
        registry_ = std::make_unique<WorkerRegistry>(*queue_, *report_svc_, *export_svc_, *retention_svc_, *alert_svc_);
    }

    void TearDown() override {
        if (queue_) queue_->Stop();
    }

    std::unique_ptr<Database> db_;
    std::unique_ptr<AuditRepository> audit_repo_;
    std::unique_ptr<ReportRepository> report_repo_;
    std::unique_ptr<AdminRepository> admin_repo_;
    std::unique_ptr<KennelRepository> kennel_repo_;
    std::unique_ptr<BookingRepository> booking_repo_;
    std::unique_ptr<InventoryRepository> inventory_repo_;
    std::unique_ptr<MaintenanceRepository> maintenance_repo_;
    std::unique_ptr<UserRepository> user_repo_;
    std::unique_ptr<AnimalRepository> animal_repo_;
    std::unique_ptr<AuditService> audit_svc_;
    std::unique_ptr<ReportService> report_svc_;
    std::unique_ptr<ExportService> export_svc_;
    std::unique_ptr<RetentionService> retention_svc_;
    std::unique_ptr<AlertService> alert_svc_;
    std::unique_ptr<JobQueue> queue_;
    std::unique_ptr<WorkerRegistry> registry_;
};

TEST_F(WorkerRegistryTest, RegisterAllDoesNotThrow) {
    EXPECT_NO_THROW(registry_->RegisterAll());
}

TEST_F(WorkerRegistryTest, QueueCanStartAfterRegistration) {
    registry_->RegisterAll();
    EXPECT_NO_THROW(queue_->Start());
    queue_->Stop();
}

TEST_F(WorkerRegistryTest, BackupHandlerMissingParamsReturnsFailed) {
    registry_->RegisterAll();
    queue_->Start();

    JobDescriptor d;
    d.run_id = 1; d.job_id = 1;
    d.job_type = JobType::Backup;
    d.parameters_json = "{}";  // missing backup_dir and db_path
    d.priority = 5; d.max_concurrency = 1;

    auto outcome = SubmitAndWait(*queue_, d);
    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.error_message.empty());
    EXPECT_NE(std::string::npos, outcome.error_message.find("required"));
}

TEST_F(WorkerRegistryTest, BackupHandlerNonExistentDbReturnsFailed) {
    registry_->RegisterAll();
    queue_->Start();

    namespace fs = std::filesystem;
    auto tmp_dir = fs::temp_directory_path() / "shelterops_test_backup";
    fs::create_directories(tmp_dir);

    JobDescriptor d;
    d.run_id = 2; d.job_id = 2;
    d.job_type = JobType::Backup;
    d.parameters_json = R"({"backup_dir":")" + (tmp_dir / "out").string()
        + R"(","db_path":"nonexistent_db_12345.db"})";
    d.priority = 5; d.max_concurrency = 1;

    auto outcome = SubmitAndWait(*queue_, d);
    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.error_message.empty());

    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
}

TEST_F(WorkerRegistryTest, BackupHandlerWithRealFileSucceeds) {
    registry_->RegisterAll();
    queue_->Start();

    namespace fs = std::filesystem;
    auto tmp_dir = fs::temp_directory_path() / "shelterops_test_backup2";
    fs::create_directories(tmp_dir);

    // Create a non-empty source file to back up.
    auto src = (tmp_dir / "src.db").string();
    {
        std::ofstream f(src, std::ios::binary);
        f << "fake_sqlite_data_content";
    }

    auto out_dir = (tmp_dir / "backups").string();

    JobDescriptor d;
    d.run_id = 3; d.job_id = 3;
    d.job_type = JobType::Backup;
    d.parameters_json = R"({"backup_dir":")" + out_dir + R"(","db_path":")" + src + R"("})";
    d.priority = 5; d.max_concurrency = 1;

    auto outcome = SubmitAndWait(*queue_, d);
    EXPECT_TRUE(outcome.success) << "error: " << outcome.error_message;
    EXPECT_FALSE(outcome.output_json.empty());

    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
}

TEST_F(WorkerRegistryTest, LanSyncHandlerMissingParamsReturnsFailed) {
    registry_->RegisterAll();
    queue_->Start();

    JobDescriptor d;
    d.run_id = 4; d.job_id = 4;
    d.job_type = JobType::LanSync;
    d.parameters_json = "{}";  // missing all required fields
    d.priority = 5; d.max_concurrency = 1;

    auto outcome = SubmitAndWait(*queue_, d);
    EXPECT_FALSE(outcome.success);
    EXPECT_NE(std::string::npos, outcome.error_message.find("required"));
}

TEST_F(WorkerRegistryTest, LanSyncHandlerNonWindowsReturnsNotAvailable) {
#if defined(_WIN32)
    GTEST_SKIP() << "Non-Windows LAN sync path only testable outside Windows";
#endif
    registry_->RegisterAll();
    queue_->Start();

    JobDescriptor d;
    d.run_id = 5; d.job_id = 5;
    d.job_type = JobType::LanSync;
    d.parameters_json = R"({
        "source_path":"nonexistent.db",
        "peer_host":"127.0.0.1",
        "peer_port":9999,
        "pinned_certs_path":"nonexistent_certs.json"
    })";
    d.priority = 5; d.max_concurrency = 1;

    auto outcome = SubmitAndWait(*queue_, d);
    EXPECT_FALSE(outcome.success);
    // On non-Windows the Schannel path is unavailable.
    EXPECT_FALSE(outcome.error_message.empty());
}

TEST_F(WorkerRegistryTest, ReportGenerateHandlerInvalidJsonReturnsFailed) {
    registry_->RegisterAll();
    queue_->Start();

    JobDescriptor d;
    d.run_id = 6; d.job_id = 6;
    d.job_type = JobType::ReportGenerate;
    d.parameters_json = "not_valid_json{{{";
    d.priority = 5; d.max_concurrency = 1;

    auto outcome = SubmitAndWait(*queue_, d);
    EXPECT_FALSE(outcome.success);
    EXPECT_NE(std::string::npos, outcome.error_message.find("invalid"));
}

TEST_F(WorkerRegistryTest, AlertScanHandlerRunsWithDefaultParams) {
    registry_->RegisterAll();
    queue_->Start();

    JobDescriptor d;
    d.run_id = 7; d.job_id = 7;
    d.job_type = JobType::AlertScan;
    d.parameters_json = R"({"low_stock_qty":5,"expiring_soon_days":30})";
    d.priority = 5; d.max_concurrency = 1;

    auto outcome = SubmitAndWait(*queue_, d);
    // Alert scan on empty inventory should succeed with zero new alerts.
    EXPECT_TRUE(outcome.success) << "error: " << outcome.error_message;
    EXPECT_FALSE(outcome.output_json.empty());
}
