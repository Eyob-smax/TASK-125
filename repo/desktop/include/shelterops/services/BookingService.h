#pragma once
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/AdminRepository.h"
#include "shelterops/domain/BookingSearchFilter.h"
#include "shelterops/domain/BookingStateMachine.h"
#include "shelterops/domain/PriceRuleEngine.h"
#include "shelterops/services/AuditService.h"
#include "shelterops/services/AuthorizationService.h"
#include "shelterops/services/UserContext.h"
#include "shelterops/infrastructure/CredentialVault.h"
#include "shelterops/common/ErrorEnvelope.h"
#include <vector>
#include <variant>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::services {

struct CreateBookingRequest {
    int64_t     kennel_id           = 0;
    int64_t     animal_id           = 0;
    std::string guest_name;
    std::string guest_phone_enc;
    std::string guest_email_enc;
    int64_t     check_in_at         = 0;
    int64_t     check_out_at        = 0;
    std::string special_requirements;
    std::string idempotency_key;    // optional; deduplicate within 5-min window
};

using BookingResult = std::variant<int64_t, common::ErrorEnvelope>;

class BookingService {
public:
    BookingService(repositories::KennelRepository& kennels,
                   repositories::BookingRepository& bookings,
                   repositories::AdminRepository&   admin,
                   AuditService&                    audit);
    BookingService(repositories::KennelRepository& kennels,
                   repositories::BookingRepository& bookings,
                   repositories::AdminRepository&   admin,
                   infrastructure::ICredentialVault& vault,
                   AuditService&                    audit);

    // Loads kennels, applies hard constraints and ranking, persists
    // recommendation_results rows. Returns explainable ranked list.
    std::vector<domain::RankedKennel> SearchAndRank(
        const domain::BookingSearchFilter& filter,
        const UserContext& user_ctx,
        int64_t now_unix);

    // Creates booking + optional approval row atomically.
    // Re-validates bookability before inserting.
    BookingResult CreateBooking(const CreateBookingRequest& req,
                                const UserContext& user_ctx,
                                int64_t now_unix);

    // State transitions — each routes through BookingStateMachine and writes audit.
    BookingResult ApproveBooking(int64_t booking_id, const UserContext& user_ctx,
                                 int64_t now_unix);
    BookingResult RejectBooking(int64_t booking_id, const UserContext& user_ctx,
                                int64_t now_unix);
    BookingResult CancelBooking(int64_t booking_id, const UserContext& user_ctx,
                                int64_t now_unix);
    BookingResult MarkNoShow(int64_t booking_id, const UserContext& user_ctx,
                             int64_t now_unix);
    BookingResult ActivateBooking(int64_t booking_id, const UserContext& user_ctx,
                                  int64_t now_unix);
    BookingResult CompleteBooking(int64_t booking_id, const UserContext& user_ctx,
                                  int64_t now_unix);

    // Records an after-sales adjustment. Requires BookingApproval role.
    common::ErrorEnvelope RecordAfterSalesAdjustment(
        int64_t booking_id, int amount_cents,
        const std::string& reason,
        const UserContext& approver_ctx,
        int64_t now_unix);

private:
    BookingResult TransitionBooking(int64_t booking_id,
                                    domain::BookingEvent event,
                                    const UserContext& user_ctx,
                                    int64_t now_unix);
    std::optional<common::ErrorEnvelope> EncryptSensitiveFields(
        const CreateBookingRequest& req,
        repositories::NewBookingParams& params) const;

    repositories::KennelRepository&  kennels_;
    repositories::BookingRepository& bookings_;
    repositories::AdminRepository&   admin_;
    infrastructure::ICredentialVault& vault_;
    AuditService&                    audit_;
};

} // namespace shelterops::services
