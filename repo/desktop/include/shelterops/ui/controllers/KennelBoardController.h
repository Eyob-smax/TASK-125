#pragma once
#include "shelterops/services/BookingService.h"
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/domain/BookingSearchFilter.h"
#include "shelterops/domain/Types.h"
#include "shelterops/common/ErrorEnvelope.h"
#include "shelterops/ui/primitives/TableSortState.h"
#include "shelterops/ui/primitives/ValidationState.h"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>

namespace shelterops::ui::controllers {

enum class KennelBoardState {
    Idle,
    Loading,
    Loaded,
    Error,
    CreatingBooking,
    BookingConflict,
    BookingSuccess,
    ApprovingBooking,
    CancellingBooking
};

struct KennelBoardFilter {
    int64_t check_in_at          = 0;
    int64_t check_out_at         = 0;
    std::string species;              // "dog", "cat", "" = any
    bool    is_aggressive        = false;
    float   min_rating           = 0.0f;
    int     max_price_cents      = 0;  // 0 = no limit
    bool    only_bookable        = true;
    std::vector<int64_t> zone_ids;
    float   max_distance_ft      = 0.0f;  // 0 = no limit
};

struct BookingFormState {
    int64_t     kennel_id    = 0;
    std::string guest_name;
    std::string guest_phone;    // plaintext — encrypted before persistence
    std::string guest_email;    // plaintext — encrypted before persistence
    std::string special_requirements;
    int64_t     check_in_at  = 0;
    int64_t     check_out_at = 0;
};

// Controller for the Intake & Kennel Board window.
// Holds filter/result/selection state; delegates business calls to BookingService.
// Cross-platform: no ImGui dependency.
class KennelBoardController {
public:
    KennelBoardController(services::BookingService&        booking_svc,
                          repositories::KennelRepository&  kennel_repo);

    // --- State queries ---
    KennelBoardState                          State()           const noexcept { return state_; }
    const std::vector<domain::RankedKennel>&  Results()         const noexcept { return results_; }
    int64_t                                   SelectedKennel()  const noexcept { return selected_kennel_id_; }
    const common::ErrorEnvelope&              LastError()       const noexcept { return last_error_; }
    const KennelBoardFilter&                  CurrentFilter()   const noexcept { return filter_; }
    BookingFormState&                         FormState()       noexcept       { return form_state_; }
    const BookingFormState&                   FormState()       const noexcept { return form_state_; }
    primitives::ValidationState&              Validation()      noexcept       { return validation_; }
    primitives::TableSortState&               SortState()       noexcept       { return sort_state_; }
    bool                                      IsDirty()         const noexcept { return is_dirty_; }

    // --- Commands ---
    void SetFilter(const KennelBoardFilter& f);
    void Refresh(const services::UserContext& ctx, int64_t now_unix);
    void SelectKennel(int64_t kennel_id);
    void BeginCreateBooking(int64_t kennel_id);
    void CancelCreateBooking();

    // Validates form, calls BookingService::CreateBooking. Sets state accordingly.
    bool SubmitBooking(const services::UserContext& ctx, int64_t now_unix);

    // State transitions for an existing booking.
    bool ApproveBooking(int64_t booking_id, const services::UserContext& ctx, int64_t now_unix);
    bool RejectBooking (int64_t booking_id, const services::UserContext& ctx, int64_t now_unix);
    bool CancelBooking (int64_t booking_id, const services::UserContext& ctx, int64_t now_unix);

    // Returns current result table as tab-separated values string.
    std::string ClipboardTsv() const;

    // Returns formatted explanation text for the selected kennel's bookability.
    std::string BookabilityExplanation(const domain::RankedKennel& rk) const;

    void ClearDirty() noexcept { is_dirty_ = false; }
    void ClearError() noexcept { last_error_ = {}; state_ = KennelBoardState::Idle; }

private:
    services::BookingService&        booking_svc_;
    repositories::KennelRepository&  kennel_repo_;

    KennelBoardState                  state_             = KennelBoardState::Idle;
    std::vector<domain::RankedKennel> results_;
    int64_t                           selected_kennel_id_ = 0;
    KennelBoardFilter                 filter_;
    BookingFormState                  form_state_;
    common::ErrorEnvelope             last_error_;
    primitives::ValidationState       validation_;
    primitives::TableSortState        sort_state_;
    bool                              is_dirty_           = false;
};

} // namespace shelterops::ui::controllers
