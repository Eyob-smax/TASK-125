#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/AuditService.h"
#include <nlohmann/json.hpp>

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::domain;

static void CreateAuditTable(Database& db) {
    db.Acquire()->Exec(
        "CREATE TABLE audit_events("
        "  event_id INTEGER PRIMARY KEY, occurred_at INTEGER NOT NULL,"
        "  actor_user_id INTEGER, actor_role TEXT, event_type TEXT NOT NULL,"
        "  entity_type TEXT, entity_id INTEGER, description TEXT NOT NULL,"
        "  session_id TEXT)");
}

class AuditServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_   = std::make_unique<Database>(":memory:");
        CreateAuditTable(*db_);
        repo_ = std::make_unique<AuditRepository>(*db_);
        svc_  = std::make_unique<AuditService>(*repo_);
    }
    std::unique_ptr<Database>          db_;
    std::unique_ptr<AuditRepository>   repo_;
    std::unique_ptr<AuditService>      svc_;
};

TEST_F(AuditServiceTest, RecordLoginWritesRow) {
    svc_->RecordLogin(7, "administrator", "sess-1", 1000);
    AuditQueryFilter f; f.event_type = "LOGIN";
    auto events = repo_->Query(f);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].actor_user_id, 7);
    EXPECT_TRUE(events[0].session_id.empty());
}

TEST_F(AuditServiceTest, RecordLoginFailureWritesRow) {
    svc_->RecordLoginFailure("baduser", "wrong password", 1000);
    AuditQueryFilter f; f.event_type = "LOGIN_FAILURE";
    auto events = repo_->Query(f);
    EXPECT_EQ(events.size(), 1u);
}

TEST_F(AuditServiceTest, RecordLoginFailureDoesNotIncludeUsername) {
    svc_->RecordLoginFailure("alice.sensitive", "wrong password", 1000);
    AuditQueryFilter f; f.event_type = "LOGIN_FAILURE";
    auto events = repo_->Query(f);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].description.find("alice.sensitive"), std::string::npos);
}

TEST_F(AuditServiceTest, RecordLogoutWritesRow) {
    svc_->RecordLogout(3, "sess-2", 1200);
    AuditQueryFilter f; f.event_type = "LOGOUT";
    auto events = repo_->Query(f);
    EXPECT_EQ(events.size(), 1u);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].session_id.empty());
}

TEST_F(AuditServiceTest, RecordMutationMasksPiiFields) {
    nlohmann::json before = {{"name", "Widget"}, {"password_hash", "abc"}};
    nlohmann::json after  = {{"name", "Widget2"}, {"password_hash", "xyz"}};
    svc_->RecordMutation(1, "administrator", "s", "item", 42, before, after, 1000);

    AuditQueryFilter f; f.event_type = "MUTATION";
    auto events = repo_->Query(f);
    ASSERT_EQ(events.size(), 1u);
    // password_hash must not appear in the description.
    EXPECT_EQ(events[0].description.find("abc"), std::string::npos);
    EXPECT_EQ(events[0].description.find("xyz"), std::string::npos);
    // Non-PII change should be recorded.
    EXPECT_NE(events[0].description.find("Widget"), std::string::npos);
}

TEST_F(AuditServiceTest, CsvExportProducesHeader) {
    svc_->RecordLogin(1, "auditor", "s", 1000);
    std::vector<std::string> lines;
    AuditQueryFilter f;
    svc_->ExportCsv(f, UserRole::Auditor,
                    [&lines](const std::string& line) { lines.push_back(line); });
    ASSERT_GE(lines.size(), 1u);
    EXPECT_NE(lines[0].find("event_type"), std::string::npos);
}

TEST_F(AuditServiceTest, SystemEventDescriptionRedactsEmailAndPhone) {
    // Feed a description that contains both an email address and a phone number.
    svc_->RecordSystemEvent("TEST_EVENT",
        "Staff member john.doe@shelter.org issued item to 555-867-5309", 3000);

    AuditQueryFilter f; f.event_type = "TEST_EVENT";
    auto events = repo_->Query(f);
    ASSERT_EQ(1u, events.size());

    const std::string& desc = events[0].description;
    // Raw email must not appear in the stored description.
    EXPECT_EQ(std::string::npos, desc.find("john.doe@shelter.org"))
        << "Email address must be redacted from audit system-event descriptions";
    // Raw phone must not appear in the stored description.
    EXPECT_EQ(std::string::npos, desc.find("867-5309"))
        << "Phone number must be redacted from audit system-event descriptions";
    // Redaction placeholders must be present.
    EXPECT_NE(std::string::npos, desc.find("REDACTED"));
}
