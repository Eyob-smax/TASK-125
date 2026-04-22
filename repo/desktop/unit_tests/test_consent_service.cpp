#include <gtest/gtest.h>
#include "shelterops/infrastructure/Database.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/services/ConsentService.h"
#include "shelterops/services/AuditService.h"

using namespace shelterops::infrastructure;
using namespace shelterops::repositories;
using namespace shelterops::services;
using namespace shelterops::domain;

static void CreateSchema(Database& db) {
    auto g = db.Acquire();
    g->Exec("CREATE TABLE users(user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, "
            "display_name TEXT NOT NULL, password_hash TEXT NOT NULL, role TEXT NOT NULL, "
            "is_active INTEGER NOT NULL DEFAULT 1, created_at INTEGER NOT NULL)");
    g->Exec("INSERT INTO users VALUES(1,'admin','Admin','h','administrator',1,1)");
    g->Exec("CREATE TABLE audit_events(event_id INTEGER PRIMARY KEY, "
            "event_type TEXT NOT NULL, description TEXT NOT NULL, actor_id INTEGER, "
            "occurred_at INTEGER NOT NULL, entity_type TEXT, entity_id INTEGER)");
    g->Exec("CREATE TABLE consent_records(consent_id INTEGER PRIMARY KEY, "
            "entity_type TEXT NOT NULL, entity_id INTEGER NOT NULL, "
            "consent_type TEXT NOT NULL, given_at INTEGER NOT NULL, withdrawn_at INTEGER)");
}

class ConsentServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_ = std::make_unique<Database>(":memory:");
        CreateSchema(*db_);
        admin_repo_ = std::make_unique<AdminRepository>(*db_);
        audit_repo_ = std::make_unique<AuditRepository>(*db_);
        audit_svc_ = std::make_unique<AuditService>(*audit_repo_);
        svc_ = std::make_unique<ConsentService>(*admin_repo_, *audit_svc_);
        ctx_.user_id = 1;
        ctx_.role = UserRole::Administrator;
    }

    std::unique_ptr<Database> db_;
    std::unique_ptr<AdminRepository> admin_repo_;
    std::unique_ptr<AuditRepository> audit_repo_;
    std::unique_ptr<AuditService> audit_svc_;
    std::unique_ptr<ConsentService> svc_;
    UserContext ctx_;
};

TEST_F(ConsentServiceTest, RecordConsentReturnsId) {
    auto id = svc_->RecordConsent("booking", 123, "data_processing", ctx_, 1000);
    EXPECT_GT(id, 0);
}

TEST_F(ConsentServiceTest, ListConsentsReturnsRecorded) {
    svc_->RecordConsent("booking", 123, "data_processing", ctx_, 1000);
    auto consents = svc_->ListConsentsFor("booking", 123);
    ASSERT_EQ(1u, consents.size());
    EXPECT_EQ("data_processing", consents[0].consent_type);
}

TEST_F(ConsentServiceTest, WithdrawConsentSetsTimestamp) {
    auto id = svc_->RecordConsent("booking", 123, "data_processing", ctx_, 1000);
    svc_->WithdrawConsent(id, ctx_, 2000);
    auto consents = svc_->ListConsentsFor("booking", 123);
    ASSERT_EQ(1u, consents.size());
    EXPECT_EQ(2000, consents[0].withdrawn_at);
}

TEST_F(ConsentServiceTest, EmptyListForEntityWithNoConsents) {
    auto consents = svc_->ListConsentsFor("booking", 999);
    EXPECT_TRUE(consents.empty());
}

TEST_F(ConsentServiceTest, MultipleConsentsForSameEntityReturnedAll) {
    svc_->RecordConsent("booking", 200, "data_processing", ctx_, 1000);
    svc_->RecordConsent("booking", 200, "marketing",       ctx_, 1001);
    svc_->RecordConsent("booking", 200, "third_party",     ctx_, 1002);

    auto consents = svc_->ListConsentsFor("booking", 200);
    ASSERT_EQ(3u, consents.size());
}

TEST_F(ConsentServiceTest, ConsentsForDifferentEntitiesAreIsolated) {
    svc_->RecordConsent("booking", 1, "data_processing", ctx_, 1000);
    svc_->RecordConsent("booking", 2, "data_processing", ctx_, 1001);

    EXPECT_EQ(1u, svc_->ListConsentsFor("booking", 1).size());
    EXPECT_EQ(1u, svc_->ListConsentsFor("booking", 2).size());
    EXPECT_TRUE(svc_->ListConsentsFor("booking", 3).empty());
}

TEST_F(ConsentServiceTest, NonWithdrawnConsentHasZeroWithdrawnAt) {
    svc_->RecordConsent("booking", 300, "data_processing", ctx_, 5000);
    auto consents = svc_->ListConsentsFor("booking", 300);
    ASSERT_EQ(1u, consents.size());
    EXPECT_EQ(0, consents[0].withdrawn_at);
    EXPECT_EQ(5000, consents[0].given_at);
}

TEST_F(ConsentServiceTest, RecordConsentWritesAuditEvent) {
    svc_->RecordConsent("booking", 400, "data_processing", ctx_, 3000);

    AuditQueryFilter f;
    f.event_type = "CONSENT_RECORDED";
    f.to_unix    = 0;  // no upper bound
    auto events = audit_repo_->Query(f);
    ASSERT_FALSE(events.empty());
    EXPECT_EQ("CONSENT_RECORDED", events[0].event_type);
}

TEST_F(ConsentServiceTest, WithdrawConsentWritesAuditEvent) {
    auto id = svc_->RecordConsent("booking", 500, "data_processing", ctx_, 1000);
    svc_->WithdrawConsent(id, ctx_, 2000);

    AuditQueryFilter f;
    f.event_type = "CONSENT_WITHDRAWN";
    f.to_unix    = 0;
    auto events = audit_repo_->Query(f);
    ASSERT_FALSE(events.empty());
    EXPECT_EQ("CONSENT_WITHDRAWN", events[0].event_type);
}

TEST_F(ConsentServiceTest, ConsentsAreScopedByEntityType) {
    svc_->RecordConsent("booking", 10, "data_processing", ctx_, 1000);
    svc_->RecordConsent("animal",  10, "data_processing", ctx_, 1001);

    EXPECT_EQ(1u, svc_->ListConsentsFor("booking", 10).size());
    EXPECT_EQ(1u, svc_->ListConsentsFor("animal",  10).size());
}
