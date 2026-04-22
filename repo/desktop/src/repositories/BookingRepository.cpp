#include "shelterops/repositories/BookingRepository.h"
#include <stdexcept>

namespace shelterops::repositories {

BookingRepository::BookingRepository(infrastructure::Database& db) : db_(db) {}

domain::BookingStatus BookingRepository::ParseStatus(const std::string& s) {
    if (s == "pending")   return domain::BookingStatus::Pending;
    if (s == "approved")  return domain::BookingStatus::Approved;
    if (s == "active")    return domain::BookingStatus::Active;
    if (s == "completed") return domain::BookingStatus::Completed;
    if (s == "cancelled") return domain::BookingStatus::Cancelled;
    if (s == "no_show")   return domain::BookingStatus::NoShow;
    return domain::BookingStatus::Pending;
}

std::string BookingRepository::StatusToString(domain::BookingStatus s) {
    switch (s) {
    case domain::BookingStatus::Pending:   return "pending";
    case domain::BookingStatus::Approved:  return "approved";
    case domain::BookingStatus::Active:    return "active";
    case domain::BookingStatus::Completed: return "completed";
    case domain::BookingStatus::Cancelled: return "cancelled";
    case domain::BookingStatus::NoShow:    return "no_show";
    default: return "pending";
    }
}

BookingRecord BookingRepository::RowToRecord(const std::vector<std::string>& vals) {
    // Columns: booking_id, kennel_id, animal_id, guest_name, guest_phone_enc,
    //          guest_email_enc, check_in_at, check_out_at, status,
    //          nightly_price_cents, total_price_cents, special_requirements,
    //          created_by, created_at, approved_by, approved_at, notes
    BookingRecord r;
    r.booking_id          = vals[0].empty() ? 0 : std::stoll(vals[0]);
    r.kennel_id           = vals[1].empty() ? 0 : std::stoll(vals[1]);
    r.animal_id           = vals[2].empty() ? 0 : std::stoll(vals[2]);
    r.guest_name          = vals[3];
    r.guest_phone_enc     = vals[4];
    r.guest_email_enc     = vals[5];
    r.check_in_at         = vals[6].empty() ? 0 : std::stoll(vals[6]);
    r.check_out_at        = vals[7].empty() ? 0 : std::stoll(vals[7]);
    r.status              = ParseStatus(vals[8]);
    r.nightly_price_cents = vals[9].empty() ? 0 : std::stoi(vals[9]);
    r.total_price_cents   = vals[10].empty() ? 0 : std::stoi(vals[10]);
    r.special_requirements = vals[11];
    r.created_by          = vals[12].empty() ? 0 : std::stoll(vals[12]);
    r.created_at          = vals[13].empty() ? 0 : std::stoll(vals[13]);
    r.approved_by         = vals[14].empty() ? 0 : std::stoll(vals[14]);
    r.approved_at         = vals[15].empty() ? 0 : std::stoll(vals[15]);
    r.notes               = vals[16];
    return r;
}

int64_t BookingRepository::Insert(const NewBookingParams& params, int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO bookings "
        "(kennel_id, animal_id, guest_name, guest_phone_enc, guest_email_enc, "
        " check_in_at, check_out_at, status, nightly_price_cents, total_price_cents, "
        " special_requirements, created_by, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 'pending', ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {
        std::to_string(params.kennel_id),
        std::to_string(params.animal_id),
        params.guest_name,
        params.guest_phone_enc,
        params.guest_email_enc,
        std::to_string(params.check_in_at),
        std::to_string(params.check_out_at),
        std::to_string(params.nightly_price_cents),
        std::to_string(params.total_price_cents),
        params.special_requirements,
        std::to_string(params.created_by),
        std::to_string(now_unix)
    });
    return conn->LastInsertRowId();
}

int64_t BookingRepository::InsertIfBookable(const NewBookingParams& params, int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO bookings "
        "(kennel_id, animal_id, guest_name, guest_phone_enc, guest_email_enc, "
        " check_in_at, check_out_at, status, nightly_price_cents, total_price_cents, "
        " special_requirements, created_by, created_at) "
        "SELECT ?, ?, ?, ?, ?, ?, ?, 'pending', ?, ?, ?, ?, ? "
        "WHERE EXISTS (SELECT 1 FROM kennels WHERE kennel_id = ? AND is_active = 1) "
        "  AND (SELECT COUNT(*) FROM bookings "
        "       WHERE kennel_id = ? "
        "         AND check_out_at > ? "
        "         AND check_in_at < ? "
        "         AND status NOT IN ('cancelled','no_show')) "
        "      < COALESCE((SELECT capacity FROM kennels WHERE kennel_id = ?), 0)";

    auto conn = db_.Acquire();
    conn->Exec(sql, {
        std::to_string(params.kennel_id),
        std::to_string(params.animal_id),
        params.guest_name,
        params.guest_phone_enc,
        params.guest_email_enc,
        std::to_string(params.check_in_at),
        std::to_string(params.check_out_at),
        std::to_string(params.nightly_price_cents),
        std::to_string(params.total_price_cents),
        params.special_requirements,
        std::to_string(params.created_by),
        std::to_string(now_unix),
        std::to_string(params.kennel_id),
        std::to_string(params.kennel_id),
        std::to_string(params.check_in_at),
        std::to_string(params.check_out_at),
        std::to_string(params.kennel_id)
    });
    if (conn->ChangeCount() == 0) {
        return 0;
    }
    return conn->LastInsertRowId();
}

void BookingRepository::UpdateStatus(int64_t booking_id,
                                      domain::BookingStatus status,
                                      int64_t actor_id,
                                      int64_t now_unix) {
    (void)actor_id; (void)now_unix;
    static const std::string sql =
        "UPDATE bookings SET status = ? WHERE booking_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {StatusToString(status), std::to_string(booking_id)});
}

int64_t BookingRepository::InsertApprovalRequest(int64_t booking_id,
                                                   int64_t requested_by,
                                                   int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO booking_approvals (booking_id, requested_by, requested_at) "
        "VALUES (?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(booking_id),
                     std::to_string(requested_by),
                     std::to_string(now_unix)});
    return conn->LastInsertRowId();
}

void BookingRepository::DecideApproval(int64_t approval_id,
                                        const std::string& decision,
                                        int64_t approver_id,
                                        int64_t now_unix) {
    static const std::string sql =
        "UPDATE booking_approvals SET decision=?, approver_id=?, decided_at=? "
        "WHERE approval_id=?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {decision,
                     std::to_string(approver_id),
                     std::to_string(now_unix),
                     std::to_string(approval_id)});
}

std::vector<domain::ExistingBooking> BookingRepository::ListOverlapping(
    int64_t kennel_id, const domain::DateRange& window) const {
    // Excludes cancelled and no_show bookings.
    static const std::string sql =
        "SELECT booking_id, kennel_id, check_in_at, check_out_at, status "
        "FROM bookings "
        "WHERE kennel_id = ? AND check_out_at > ? AND check_in_at < ? "
        "  AND status NOT IN ('cancelled','no_show')";

    std::vector<domain::ExistingBooking> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(kennel_id),
                      std::to_string(window.from_unix),
                      std::to_string(window.to_unix)},
        [&](const auto&, const auto& vals) {
            domain::ExistingBooking b;
            b.booking_id     = vals[0].empty() ? 0 : std::stoll(vals[0]);
            b.kennel_id      = vals[1].empty() ? 0 : std::stoll(vals[1]);
            b.window.from_unix = vals[2].empty() ? 0 : std::stoll(vals[2]);
            b.window.to_unix   = vals[3].empty() ? 0 : std::stoll(vals[3]);
            b.status         = ParseStatus(vals[4]);
            result.push_back(b);
        });
    return result;
}

void BookingRepository::InsertRecommendationResult(const std::string& query_hash,
                                                    int64_t kennel_id,
                                                    int rank_position,
                                                    float score,
                                                    const std::string& reason_json,
                                                    int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO recommendation_results "
        "(query_hash, kennel_id, rank_position, score, reason_json, generated_at) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {query_hash,
                     std::to_string(kennel_id),
                     std::to_string(rank_position),
                     std::to_string(score),
                     reason_json,
                     std::to_string(now_unix)});
}

std::vector<RecommendationResultRecord> BookingRepository::ListRecommendationsFor(
    const std::string& query_hash) const {
    static const std::string sql =
        "SELECT result_id, query_hash, kennel_id, rank_position, score, "
        "       reason_json, generated_at "
        "FROM recommendation_results WHERE query_hash = ? ORDER BY rank_position";

    std::vector<RecommendationResultRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {query_hash}, [&](const auto&, const auto& vals) {
        RecommendationResultRecord r;
        r.result_id    = vals[0].empty() ? 0 : std::stoll(vals[0]);
        r.query_hash   = vals[1];
        r.kennel_id    = vals[2].empty() ? 0 : std::stoll(vals[2]);
        r.rank_position = vals[3].empty() ? 0 : std::stoi(vals[3]);
        r.score        = vals[4].empty() ? 0.0f : std::stof(vals[4]);
        r.reason_json  = vals[5];
        r.generated_at = vals[6].empty() ? 0 : std::stoll(vals[6]);
        result.push_back(r);
    });
    return result;
}

std::optional<BookingRecord> BookingRepository::FindById(int64_t booking_id) const {
    static const std::string sql =
        "SELECT booking_id, kennel_id, COALESCE(animal_id,0), COALESCE(guest_name,''), "
        "       COALESCE(guest_phone_enc,''), COALESCE(guest_email_enc,''), "
        "       check_in_at, check_out_at, status, nightly_price_cents, "
        "       total_price_cents, COALESCE(special_requirements,''), "
        "       COALESCE(created_by,0), created_at, "
        "       COALESCE(approved_by,0), COALESCE(approved_at,0), COALESCE(notes,'') "
        "FROM bookings WHERE booking_id = ?";

    std::optional<BookingRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(booking_id)},
        [&](const auto&, const auto& vals) {
            result = RowToRecord(vals);
        });
    return result;
}

std::optional<ApprovalRecord> BookingRepository::FindApprovalByBooking(
    int64_t booking_id) const {
    static const std::string sql =
        "SELECT approval_id, booking_id, requested_by, requested_at, "
        "       COALESCE(approver_id,0), COALESCE(decision,''), "
        "       COALESCE(decided_at,0), COALESCE(notes,'') "
        "FROM booking_approvals WHERE booking_id = ? "
        "ORDER BY requested_at DESC LIMIT 1";

    std::optional<ApprovalRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(booking_id)},
        [&](const auto&, const auto& vals) {
            ApprovalRecord r;
            r.approval_id  = vals[0].empty() ? 0 : std::stoll(vals[0]);
            r.booking_id   = vals[1].empty() ? 0 : std::stoll(vals[1]);
            r.requested_by = vals[2].empty() ? 0 : std::stoll(vals[2]);
            r.requested_at = vals[3].empty() ? 0 : std::stoll(vals[3]);
            r.approver_id  = vals[4].empty() ? 0 : std::stoll(vals[4]);
            r.decision     = vals[5];
            r.decided_at   = vals[6].empty() ? 0 : std::stoll(vals[6]);
            r.notes        = vals[7];
            result = r;
        });
    return result;
}

std::vector<domain::OverdueFeePoint> BookingRepository::ListUnpaidFees(
    int64_t now_unix) const {
    static const std::string sql =
        "SELECT f.booking_id, f.amount_cents, f.due_at "
        "FROM boarding_fees f "
        "JOIN bookings b ON b.booking_id = f.booking_id "
        "WHERE f.paid_at IS NULL AND f.due_at < ? AND b.check_out_at < ?";

    std::vector<domain::OverdueFeePoint> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(now_unix), std::to_string(now_unix)},
        [&](const auto&, const auto& vals) {
            domain::OverdueFeePoint p;
            p.booking_id   = vals[0].empty() ? 0 : std::stoll(vals[0]);
            p.amount_cents = vals[1].empty() ? 0 : std::stoll(vals[1]);
            p.due_at       = vals[2].empty() ? 0 : std::stoll(vals[2]);
            result.push_back(p);
        });
    return result;
}

void BookingRepository::InsertBoardingFee(int64_t booking_id, int amount_cents,
                                           int64_t due_at, int64_t now_unix) {
    static const std::string sql =
        "INSERT INTO boarding_fees (booking_id, amount_cents, due_at, created_at) "
        "VALUES (?, ?, ?, ?)";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(booking_id),
                     std::to_string(amount_cents),
                     std::to_string(due_at),
                     std::to_string(now_unix)});
}

std::vector<BookingRecord> BookingRepository::ListCompletedInRange(
    const domain::DateRange& window) const {
    static const std::string sql =
        "SELECT booking_id, kennel_id, COALESCE(animal_id,0),"
        "       COALESCE(guest_name,''), COALESCE(guest_phone_enc,''), COALESCE(guest_email_enc,''),"
        "       check_in_at, check_out_at, status,"
        "       nightly_price_cents, total_price_cents,"
        "       COALESCE(special_requirements,''), COALESCE(created_by,0), created_at,"
        "       COALESCE(approved_by,0), COALESCE(approved_at,0), COALESCE(notes,'')"
        " FROM bookings"
        " WHERE status = 'completed'"
        "   AND check_out_at >= ? AND check_out_at <= ?";

    std::vector<BookingRecord> result;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(window.from_unix), std::to_string(window.to_unix)},
        [&](const auto&, const auto& vals) {
            result.push_back(RowToRecord(vals));
        });
    return result;
}

std::vector<BookingSearchHit> BookingRepository::SearchByQuery(
    const std::string& query,
    int limit) const {
    static const std::string sql =
        "SELECT booking_id, COALESCE(guest_name,''), COALESCE(status,''), "
        "       COALESCE(check_in_at,0), COALESCE(check_out_at,0) "
        "FROM bookings "
        "WHERE CAST(booking_id AS TEXT) LIKE ? "
        "   OR LOWER(COALESCE(guest_name,'')) LIKE LOWER(?) "
        "ORDER BY created_at DESC LIMIT ?";

    const std::string q = "%" + query + "%";
    std::vector<BookingSearchHit> out;
    auto conn = db_.Acquire();
    conn->Query(sql, {q, q, std::to_string(limit)},
        [&](const auto&, const auto& vals) {
            BookingSearchHit h;
            h.booking_id   = vals[0].empty() ? 0 : std::stoll(vals[0]);
            h.guest_name   = vals[1];
            h.status       = vals[2];
            h.check_in_at  = vals[3].empty() ? 0 : std::stoll(vals[3]);
            h.check_out_at = vals[4].empty() ? 0 : std::stoll(vals[4]);
            out.push_back(std::move(h));
        });
    return out;
}

std::vector<BookingRepository::RetentionCandidate>
BookingRepository::ListRetentionCandidates(int64_t cutoff_unix) const {
    static const std::string sql =
        "SELECT booking_id, created_at, "
        "  CASE WHEN guest_name='[anonymized]' THEN 1 ELSE 0 END "
        "FROM bookings WHERE created_at < ?";

    std::vector<RetentionCandidate> out;
    auto conn = db_.Acquire();
    conn->Query(sql, {std::to_string(cutoff_unix)},
        [&](const auto&, const auto& vals) {
            RetentionCandidate c;
            c.booking_id          = vals[0].empty() ? 0 : std::stoll(vals[0]);
            c.created_at          = vals[1].empty() ? 0 : std::stoll(vals[1]);
            c.already_anonymized  = vals[2] == "1";
            out.push_back(c);
        });
    return out;
}

void BookingRepository::AnonymizeForRetention(int64_t booking_id, int64_t now_unix) {
    static const std::string sql =
        "UPDATE bookings SET "
        "  guest_name='[anonymized]', guest_phone_enc='', guest_email_enc='', "
        "  special_requirements=NULL, notes=NULL, approved_at=COALESCE(approved_at, ?) "
        "WHERE booking_id = ?";
    auto conn = db_.Acquire();
    conn->Exec(sql, {std::to_string(now_unix), std::to_string(booking_id)});
}

bool BookingRepository::DeleteForRetention(int64_t booking_id) {
    auto conn = db_.Acquire();
    try {
        conn->Exec("DELETE FROM boarding_fees WHERE booking_id = ?",
                   {std::to_string(booking_id)});
        conn->Exec("DELETE FROM booking_approvals WHERE booking_id = ?",
                   {std::to_string(booking_id)});
        conn->Exec("DELETE FROM outbound_records WHERE booking_id = ?",
                   {std::to_string(booking_id)});
        conn->Exec("DELETE FROM after_sales_adjustments WHERE booking_id = ?",
                   {std::to_string(booking_id)});
        conn->Exec("DELETE FROM bookings WHERE booking_id = ?",
                   {std::to_string(booking_id)});
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace shelterops::repositories
