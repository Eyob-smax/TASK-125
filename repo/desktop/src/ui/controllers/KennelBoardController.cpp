#include "shelterops/ui/controllers/KennelBoardController.h"
#include "shelterops/infrastructure/CryptoHelper.h"
#include <spdlog/spdlog.h>
#include <sstream>

namespace shelterops::ui::controllers {

KennelBoardController::KennelBoardController(
    services::BookingService&        booking_svc,
    repositories::KennelRepository&  kennel_repo)
    : booking_svc_(booking_svc), kennel_repo_(kennel_repo)
{}

void KennelBoardController::SetFilter(const KennelBoardFilter& f) {
    filter_ = f;
    is_dirty_ = true;
}

void KennelBoardController::Refresh(
    const services::UserContext& ctx, int64_t now_unix)
{
    state_ = KennelBoardState::Loading;
    results_.clear();
    last_error_ = {};

    domain::BookingSearchFilter sf;
    sf.window.from_unix          = filter_.check_in_at;
    sf.window.to_unix            = filter_.check_out_at;
    sf.is_aggressive             = filter_.is_aggressive;
    sf.min_rating                = filter_.min_rating;
    sf.max_nightly_price_cents   = filter_.max_price_cents;
    sf.only_bookable             = filter_.only_bookable;
    sf.zone_ids                  = filter_.zone_ids;
    sf.max_distance_ft           = filter_.max_distance_ft;

    if (!filter_.species.empty()) {
        if      (filter_.species == "dog")    sf.species = domain::AnimalSpecies::Dog;
        else if (filter_.species == "cat")    sf.species = domain::AnimalSpecies::Cat;
        else if (filter_.species == "rabbit") sf.species = domain::AnimalSpecies::Rabbit;
        else if (filter_.species == "bird")   sf.species = domain::AnimalSpecies::Bird;
    }

    try {
        results_ = booking_svc_.SearchAndRank(sf, ctx, now_unix);
        state_   = KennelBoardState::Loaded;
        is_dirty_ = true;
        spdlog::debug("KennelBoardController: loaded {} kennels", results_.size());
    } catch (const std::exception& e) {
        last_error_ = { common::ErrorCode::Internal, e.what() };
        state_ = KennelBoardState::Error;
        spdlog::error("KennelBoardController::Refresh: {}", e.what());
    }
}

void KennelBoardController::SelectKennel(int64_t kennel_id) {
    selected_kennel_id_ = kennel_id;
    form_state_.kennel_id = kennel_id;
}

void KennelBoardController::BeginCreateBooking(int64_t kennel_id) {
    SelectKennel(kennel_id);
    validation_.Clear();
    form_state_ = {};
    form_state_.kennel_id    = kennel_id;
    form_state_.check_in_at  = filter_.check_in_at;
    form_state_.check_out_at = filter_.check_out_at;
    state_ = KennelBoardState::CreatingBooking;
}

void KennelBoardController::CancelCreateBooking() {
    state_ = KennelBoardState::Loaded;
    validation_.Clear();
}

bool KennelBoardController::SubmitBooking(
    const services::UserContext& ctx, int64_t now_unix)
{
    validation_.Clear();

    if (form_state_.guest_name.empty())
        validation_.SetError("guest_name", "Guest name is required.");
    if (form_state_.check_in_at <= 0)
        validation_.SetError("check_in_at", "Check-in date is required.");
    if (form_state_.check_out_at <= form_state_.check_in_at)
        validation_.SetError("check_out_at", "Check-out must be after check-in.");

    if (validation_.HasErrors()) return false;

    services::CreateBookingRequest req;
    req.kennel_id          = form_state_.kennel_id;
    req.guest_name         = form_state_.guest_name;
    req.guest_phone_enc    = form_state_.guest_phone; // caller encrypts before display
    req.guest_email_enc    = form_state_.guest_email;
    req.check_in_at        = form_state_.check_in_at;
    req.check_out_at       = form_state_.check_out_at;
    req.special_requirements = form_state_.special_requirements;

    auto result = booking_svc_.CreateBooking(req, ctx, now_unix);

    if (auto* err = std::get_if<common::ErrorEnvelope>(&result)) {
        last_error_ = *err;
        state_ = (err->code == common::ErrorCode::BookingConflict)
                 ? KennelBoardState::BookingConflict
                 : KennelBoardState::Error;
        return false;
    }

    state_    = KennelBoardState::BookingSuccess;
    is_dirty_ = true;   // results need refresh after booking
    spdlog::info("KennelBoardController: booking created id={}",
                 std::get<int64_t>(result));
    return true;
}

bool KennelBoardController::ApproveBooking(
    int64_t booking_id, const services::UserContext& ctx, int64_t now_unix)
{
    state_ = KennelBoardState::ApprovingBooking;
    auto result = booking_svc_.ApproveBooking(booking_id, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&result)) {
        last_error_ = *err;
        state_ = KennelBoardState::Error;
        return false;
    }
    state_    = KennelBoardState::Loaded;
    is_dirty_ = true;
    return true;
}

bool KennelBoardController::RejectBooking(
    int64_t booking_id, const services::UserContext& ctx, int64_t now_unix)
{
    auto result = booking_svc_.RejectBooking(booking_id, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&result)) {
        last_error_ = *err;
        state_ = KennelBoardState::Error;
        return false;
    }
    is_dirty_ = true;
    return true;
}

bool KennelBoardController::CancelBooking(
    int64_t booking_id, const services::UserContext& ctx, int64_t now_unix)
{
    state_ = KennelBoardState::CancellingBooking;
    auto result = booking_svc_.CancelBooking(booking_id, ctx, now_unix);
    if (auto* err = std::get_if<common::ErrorEnvelope>(&result)) {
        last_error_ = *err;
        state_ = KennelBoardState::Error;
        return false;
    }
    state_    = KennelBoardState::Loaded;
    is_dirty_ = true;
    return true;
}

std::string KennelBoardController::ClipboardTsv() const {
    static const std::vector<std::string> kHeaders{
        "Rank", "Kennel ID", "Zone", "Score",
        "Price/night ($)", "Rating", "Bookable", "Reasons"
    };

    return primitives::TableSortState::FormatTsv(
        kHeaders,
        sort_state_.ComputeIndices(
            results_.size(),
            nullptr,   // caller may set comparator; default order here
            nullptr),
        kHeaders.size(),
        [this](std::size_t row, std::size_t col) -> std::string {
            const auto& rk = results_[row];
            switch (col) {
            case 0: return std::to_string(rk.rank);
            case 1: return std::to_string(rk.kennel_id);
            case 2: return std::to_string(rk.kennel.zone_id);
            case 3: {
                std::ostringstream o; o.precision(2);
                o << std::fixed << rk.score; return o.str();
            }
            case 4: return std::to_string(rk.kennel.nightly_price_cents / 100);
            case 5: {
                std::ostringstream o; o.precision(1);
                o << std::fixed << rk.kennel.rating; return o.str();
            }
            case 6: return rk.bookability.is_bookable ? "Yes" : "No";
            case 7: {
                std::string r;
                for (const auto& reason : rk.reasons) {
                    if (!r.empty()) r += ", ";
                    r += reason.code;
                }
                return r;
            }
            default: return "";
            }
        });
}

std::string KennelBoardController::BookabilityExplanation(
    const domain::RankedKennel& rk) const
{
    // Build a BookingSearchFilter from the controller's current filter state
    // so the explanation reflects the actual operator search criteria.
    domain::BookingSearchFilter sf;
    sf.window.from_unix        = filter_.check_in_at;
    sf.window.to_unix          = filter_.check_out_at;
    sf.is_aggressive           = filter_.is_aggressive;
    sf.min_rating              = filter_.min_rating;
    sf.max_nightly_price_cents = filter_.max_price_cents;
    sf.only_bookable           = filter_.only_bookable;
    sf.zone_ids                = filter_.zone_ids;
    sf.max_distance_ft         = filter_.max_distance_ft;

    return domain::FormatBookabilityExplanation(
        rk.kennel,
        rk.bookability,
        domain::FilterToRequest(sf, rk.kennel_id));
}

} // namespace shelterops::ui::controllers
