#include "shelterops/services/BookingService.h"
#include "shelterops/domain/BookingRules.h"
#include "shelterops/domain/PriceRuleEngine.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include "shelterops/common/Uuid.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sodium.h>
#include <optional>

namespace shelterops::services {

namespace {

std::string HexEncode(const std::vector<uint8_t>& data) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2U);
    for (uint8_t byte : data) {
        out.push_back(kHex[(byte >> 4) & 0x0F]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

std::vector<uint8_t> LoadOrCreateKey(infrastructure::ICredentialVault& vault) {
    auto key_entry = vault.Load(infrastructure::kVaultKeyDataKey);
    if (key_entry && key_entry->data.size() == crypto_aead_aes256gcm_KEYBYTES) {
        return key_entry->data;
    }

    auto key = infrastructure::CryptoHelper::GenerateRandomKey(
        crypto_aead_aes256gcm_KEYBYTES);
    vault.Store(infrastructure::kVaultKeyDataKey, key);
    return key;
}

// Test-only: the 4-arg constructor delegates here so unit tests that do not
// exercise field encryption can omit the explicit vault argument. Production
// code must use the 5-arg constructor with the DPAPI-backed CredentialVault.
infrastructure::InMemoryCredentialVault& DefaultVault() {
    static infrastructure::InMemoryCredentialVault vault;
    return vault;
}

} // namespace

BookingService::BookingService(repositories::KennelRepository& kennels,
                               repositories::BookingRepository& bookings,
                               repositories::AdminRepository&   admin,
                               AuditService&                    audit)
    : BookingService(kennels, bookings, admin, DefaultVault(), audit) {}

BookingService::BookingService(repositories::KennelRepository& kennels,
                               repositories::BookingRepository& bookings,
                               repositories::AdminRepository&   admin,
                               infrastructure::ICredentialVault& vault,
                               AuditService&                    audit)
    : kennels_(kennels), bookings_(bookings), admin_(admin), vault_(vault), audit_(audit) {}

std::vector<domain::RankedKennel> BookingService::SearchAndRank(
    const domain::BookingSearchFilter& filter,
    const UserContext& user_ctx,
    int64_t now_unix) {
    (void)user_ctx;

    // Load all active kennels and apply hard constraints.
    auto all_kennels = kennels_.ListActiveKennels();
    auto candidates  = domain::FilterKennelsByHardConstraints(all_kennels, filter);

    // Collect overlapping bookings across all candidate kennels.
    std::vector<domain::ExistingBooking> all_bookings;
    for (const auto& k : candidates) {
        auto overlapping = bookings_.ListOverlapping(k.kennel_id, filter.window);
        all_bookings.insert(all_bookings.end(), overlapping.begin(), overlapping.end());
    }

    // Use the first candidate's zone as reference, or origin if empty.
    domain::ZoneCoord ref_coord{0.0f, 0.0f};
    if (!candidates.empty()) {
        ref_coord = candidates.front().coord;
    }

    // Convert filter to BookingRequest for ranking.
    domain::BookingRequest req;
    req.window       = filter.window;
    req.pet_species  = filter.species;
    req.is_aggressive = filter.is_aggressive;
    req.is_large_dog  = filter.is_large_dog;

    auto ranked = domain::RankKennels(candidates, req, all_bookings,
                                       ref_coord, filter.max_distance_ft);

    // Compute deterministic query hash.
    const std::string query_hash = domain::ComputeQueryHash(filter);

    // Persist recommendation results.
    for (const auto& r : ranked) {
        nlohmann::json reason_arr = nlohmann::json::array();
        for (const auto& reason : r.reasons) {
            reason_arr.push_back({{"code", reason.code}, {"detail", reason.detail}});
        }
        bookings_.InsertRecommendationResult(
            query_hash, r.kennel_id, r.rank, r.score,
            reason_arr.dump(), now_unix);
    }

    return ranked;
}

BookingResult BookingService::CreateBooking(const CreateBookingRequest& req,
                                             const UserContext& user_ctx,
                                             int64_t now_unix) {
    if (auto denied = AuthorizationService::RequireWrite(user_ctx.role))
        return *denied;

    // Load kennel for validation.
    auto all_kennels = kennels_.ListActiveKennels();
    const domain::KennelInfo* kennel_ptr = nullptr;
    for (const auto& k : all_kennels) {
        if (k.kennel_id == req.kennel_id) { kennel_ptr = &k; break; }
    }

    if (!kennel_ptr) {
        return common::ErrorEnvelope{common::ErrorCode::NotFound,
                                      "Kennel not found"};
    }

    // Re-validate bookability.
    domain::DateRange window{req.check_in_at, req.check_out_at};
    auto existing = bookings_.ListOverlapping(req.kennel_id, window);

    domain::BookingRequest book_req;
    book_req.kennel_id    = req.kennel_id;
    book_req.window       = window;

    auto result = domain::EvaluateBookability(*kennel_ptr, book_req, existing);
    if (!result.is_bookable) {
        return common::ErrorEnvelope{common::ErrorCode::BookingConflict,
                                      "Kennel is not available for the requested dates"};
    }

    // Apply price rules.
    auto price_rules_raw = admin_.ListActivePriceRules(now_unix);
    std::vector<domain::PriceRule> price_rules;
    for (const auto& r : price_rules_raw) {
        domain::PriceRule pr;
        pr.rule_id         = r.rule_id;
        pr.adjustment_type = r.adjustment_type;
        pr.amount          = r.amount;
        pr.condition_json  = r.condition_json;
        pr.is_active       = r.is_active;
        pr.valid_from      = r.valid_from;
        pr.valid_to        = r.valid_to;
        price_rules.push_back(pr);
    }

    int64_t nights = window.DurationDays();
    if (nights <= 0) nights = 1;

    domain::PriceContext price_ctx;
    price_ctx.zone_id  = kennel_ptr->zone_id;
    price_ctx.nights   = nights;

    auto adjusted = domain::ApplyPriceRules(
        kennel_ptr->nightly_price_cents, price_rules, price_ctx, now_unix);

    int total = adjusted.final_cents * static_cast<int>(nights);

    // Insert booking.
    repositories::NewBookingParams params;
    params.kennel_id           = req.kennel_id;
    params.animal_id           = req.animal_id;
    params.guest_name          = req.guest_name;
    params.check_in_at         = req.check_in_at;
    params.check_out_at        = req.check_out_at;
    params.nightly_price_cents = adjusted.final_cents;
    params.total_price_cents   = total;
    params.special_requirements = req.special_requirements;
    params.created_by          = user_ctx.user_id;

    if (auto err = EncryptSensitiveFields(req, params); err.has_value()) {
        return *err;
    }

    int64_t booking_id = bookings_.InsertIfBookable(params, now_unix);
    if (booking_id <= 0) {
        return common::ErrorEnvelope{common::ErrorCode::BookingConflict,
                                     "Kennel is not available for the requested dates"};
    }

    // Insert approval request if booking_approval_required policy is true.
    const std::string policy = admin_.GetPolicy("booking_approval_required", "true");
    if (policy == "true") {
        bookings_.InsertApprovalRequest(booking_id, user_ctx.user_id, now_unix);
    }

    // Audit.
    audit_.RecordSystemEvent("BOOKING_CREATED",
        "Booking " + std::to_string(booking_id) +
        " for kennel " + std::to_string(req.kennel_id) +
        " created by user " + std::to_string(user_ctx.user_id),
        now_unix);

    return booking_id;
}

std::optional<common::ErrorEnvelope> BookingService::EncryptSensitiveFields(
    const CreateBookingRequest& req,
    repositories::NewBookingParams& params) const {
    try {
        infrastructure::CryptoHelper::Init();
        auto key = LoadOrCreateKey(vault_);
        if (!req.guest_phone_enc.empty()) {
            params.guest_phone_enc = HexEncode(
                infrastructure::CryptoHelper::Encrypt(req.guest_phone_enc, key));
        }
        if (!req.guest_email_enc.empty()) {
            params.guest_email_enc = HexEncode(
                infrastructure::CryptoHelper::Encrypt(req.guest_email_enc, key));
        }
    } catch (const std::exception& ex) {
        spdlog::error("BookingService: failed to encrypt booking contact fields: {}", ex.what());
        return common::ErrorEnvelope{common::ErrorCode::Internal,
                                     "Failed to encrypt booking contact fields"};
    }
    return std::nullopt;
}

BookingResult BookingService::TransitionBooking(int64_t booking_id,
                                                 domain::BookingEvent event,
                                                 const UserContext& user_ctx,
                                                 int64_t now_unix) {
    auto maybe_booking = bookings_.FindById(booking_id);
    if (!maybe_booking) {
        return common::ErrorEnvelope{common::ErrorCode::NotFound,
                                      "Booking not found"};
    }

    auto can_approve = [&]() -> bool {
        return user_ctx.role == domain::UserRole::Administrator ||
               user_ctx.role == domain::UserRole::OperationsManager;
    };

    auto trans_result = domain::Transition(maybe_booking->status, event, can_approve);
    if (auto* err = std::get_if<domain::StateTransitionError>(&trans_result)) {
        common::ErrorCode code = (err->code == "APPROVAL_ROLE_REQUIRED")
            ? common::ErrorCode::Forbidden
            : common::ErrorCode::InvalidInput;
        return common::ErrorEnvelope{code, err->message};
    }

    auto new_status = std::get<domain::BookingStatus>(trans_result);
    bookings_.UpdateStatus(booking_id, new_status, user_ctx.user_id, now_unix);

    if (event == domain::BookingEvent::Approve || event == domain::BookingEvent::Reject) {
        auto approval = bookings_.FindApprovalByBooking(booking_id);
        if (approval) {
            const std::string decision = (event == domain::BookingEvent::Approve)
                ? "approved" : "rejected";
            bookings_.DecideApproval(approval->approval_id, decision,
                                     user_ctx.user_id, now_unix);
        }
    }

    audit_.RecordSystemEvent(
        "BOOKING_" + domain::BookingEventName(event),
        "Booking " + std::to_string(booking_id) +
        " transitioned to " + domain::BookingStatusName(new_status) +
        " by user " + std::to_string(user_ctx.user_id),
        now_unix);

    return booking_id;
}

BookingResult BookingService::ApproveBooking(int64_t booking_id,
                                              const UserContext& user_ctx,
                                              int64_t now_unix) {
    return TransitionBooking(booking_id, domain::BookingEvent::Approve,
                              user_ctx, now_unix);
}

BookingResult BookingService::RejectBooking(int64_t booking_id,
                                             const UserContext& user_ctx,
                                             int64_t now_unix) {
    return TransitionBooking(booking_id, domain::BookingEvent::Reject,
                              user_ctx, now_unix);
}

BookingResult BookingService::CancelBooking(int64_t booking_id,
                                             const UserContext& user_ctx,
                                             int64_t now_unix) {
    return TransitionBooking(booking_id, domain::BookingEvent::Cancel,
                              user_ctx, now_unix);
}

BookingResult BookingService::MarkNoShow(int64_t booking_id,
                                          const UserContext& user_ctx,
                                          int64_t now_unix) {
    return TransitionBooking(booking_id, domain::BookingEvent::NoShow,
                              user_ctx, now_unix);
}

BookingResult BookingService::ActivateBooking(int64_t booking_id,
                                               const UserContext& user_ctx,
                                               int64_t now_unix) {
    return TransitionBooking(booking_id, domain::BookingEvent::Activate,
                              user_ctx, now_unix);
}

BookingResult BookingService::CompleteBooking(int64_t booking_id,
                                               const UserContext& user_ctx,
                                               int64_t now_unix) {
    return TransitionBooking(booking_id, domain::BookingEvent::Complete,
                              user_ctx, now_unix);
}

common::ErrorEnvelope BookingService::RecordAfterSalesAdjustment(
    int64_t booking_id, int amount_cents,
    const std::string& reason,
    const UserContext& approver_ctx,
    int64_t now_unix) {

    auto denied = AuthorizationService::RequireBookingApproval(approver_ctx.role);
    if (denied) return *denied;

    admin_.InsertAfterSalesAdjustment(booking_id, amount_cents, reason,
                                       approver_ctx.user_id,
                                       approver_ctx.user_id,
                                       now_unix);

    audit_.RecordSystemEvent("AFTER_SALES_ADJUSTMENT",
        "Booking " + std::to_string(booking_id) +
        " adjustment " + std::to_string(amount_cents) + " cents",
        now_unix);

    return common::ErrorEnvelope{common::ErrorCode::Internal, ""};
}

} // namespace shelterops::services
