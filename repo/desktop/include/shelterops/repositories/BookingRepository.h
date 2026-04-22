#pragma once
#include "shelterops/infrastructure/Database.h"
#include "shelterops/domain/Types.h"
#include "shelterops/domain/BookingRules.h"
#include "shelterops/domain/ReportPipeline.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::repositories {

struct BookingRecord {
    int64_t     booking_id          = 0;
    int64_t     kennel_id           = 0;
    int64_t     animal_id           = 0;    // 0 = no animal linked
    std::string guest_name;
    std::string guest_phone_enc;
    std::string guest_email_enc;
    int64_t     check_in_at         = 0;
    int64_t     check_out_at        = 0;
    domain::BookingStatus status    = domain::BookingStatus::Pending;
    int         nightly_price_cents = 0;
    int         total_price_cents   = 0;
    std::string special_requirements;
    int64_t     created_by          = 0;
    int64_t     created_at          = 0;    // IMMUTABLE
    int64_t     approved_by         = 0;
    int64_t     approved_at         = 0;
    std::string notes;
};

struct NewBookingParams {
    int64_t     kennel_id           = 0;
    int64_t     animal_id           = 0;
    std::string guest_name;
    std::string guest_phone_enc;
    std::string guest_email_enc;
    int64_t     check_in_at         = 0;
    int64_t     check_out_at        = 0;
    int         nightly_price_cents = 0;
    int         total_price_cents   = 0;
    std::string special_requirements;
    int64_t     created_by          = 0;
};

struct ApprovalRecord {
    int64_t     approval_id    = 0;
    int64_t     booking_id     = 0;
    int64_t     requested_by   = 0;
    int64_t     requested_at   = 0;    // IMMUTABLE
    int64_t     approver_id    = 0;
    std::string decision;              // "" = pending
    int64_t     decided_at     = 0;
    std::string notes;
};

struct RecommendationResultRecord {
    int64_t     result_id    = 0;
    std::string query_hash;
    int64_t     kennel_id    = 0;
    int         rank_position = 0;
    float       score        = 0.0f;
    std::string reason_json;
    int64_t     generated_at = 0;    // IMMUTABLE
};

struct BookingSearchHit {
    int64_t     booking_id    = 0;
    std::string guest_name;
    std::string status;
    int64_t     check_in_at   = 0;
    int64_t     check_out_at  = 0;
};

class BookingRepository {
public:
    explicit BookingRepository(infrastructure::Database& db);

    // Insert a new booking; returns its booking_id. created_at is set from now_unix.
    int64_t Insert(const NewBookingParams& params, int64_t now_unix);
    int64_t InsertIfBookable(const NewBookingParams& params, int64_t now_unix);

    void UpdateStatus(int64_t booking_id, domain::BookingStatus status,
                      int64_t actor_id, int64_t now_unix);

    int64_t InsertApprovalRequest(int64_t booking_id, int64_t requested_by,
                                  int64_t now_unix);

    void DecideApproval(int64_t approval_id, const std::string& decision,
                        int64_t approver_id, int64_t now_unix);

    // Returns all bookings for the same kennel that overlap the given window.
    // Excludes cancelled and no_show bookings.
    std::vector<domain::ExistingBooking> ListOverlapping(int64_t kennel_id,
                                                          const domain::DateRange& window) const;

    void InsertRecommendationResult(const std::string& query_hash,
                                    int64_t kennel_id,
                                    int rank_position,
                                    float score,
                                    const std::string& reason_json,
                                    int64_t now_unix);

    std::vector<RecommendationResultRecord> ListRecommendationsFor(
        const std::string& query_hash) const;

    std::optional<BookingRecord> FindById(int64_t booking_id) const;
    std::optional<ApprovalRecord> FindApprovalByBooking(int64_t booking_id) const;

    // Returns overdue fee points for the overdue-fee distribution report.
    // Only rows where paid_at IS NULL and check_out_at < now_unix.
    std::vector<domain::OverdueFeePoint> ListUnpaidFees(int64_t now_unix) const;

    // Returns completed bookings whose check_out_at falls within [window.from, window.to].
    // Used by the kennel_turnover report pipeline.
    std::vector<BookingRecord> ListCompletedInRange(
        const domain::DateRange& window) const;

    void InsertBoardingFee(int64_t booking_id, int amount_cents,
                           int64_t due_at, int64_t now_unix);

    // Global search support.
    std::vector<BookingSearchHit> SearchByQuery(const std::string& query,
                                                int limit = 100) const;

    // Retention helpers.
    struct RetentionCandidate {
        int64_t booking_id;
        int64_t created_at;
        bool    already_anonymized;
    };
    std::vector<RetentionCandidate> ListRetentionCandidates(int64_t cutoff_unix) const;
    void AnonymizeForRetention(int64_t booking_id, int64_t now_unix);
    bool DeleteForRetention(int64_t booking_id);

private:
    static BookingRecord RowToRecord(const std::vector<std::string>& vals);
    static domain::BookingStatus ParseStatus(const std::string& s);
    static std::string StatusToString(domain::BookingStatus s);

    infrastructure::Database& db_;
};

} // namespace shelterops::repositories
