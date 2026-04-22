#pragma once
#include "shelterops/ui/controllers/KennelBoardController.h"
#include "shelterops/shell/SessionContext.h"
#include <cstdint>
#include <ctime>
#include <sstream>
#include <string>

namespace shelterops::ui::views {

// Dear ImGui rendering of the Intake & Kennel Board window.
// Win32 only — compiled into ShelterOpsDesk.exe.
class KennelBoardView {
public:
    KennelBoardView(controllers::KennelBoardController& ctrl,
                    shell::SessionContext&               session);

    // Renders the docked/floating window. Returns false when the window is closed.
    bool Render(int64_t now_unix);

private:
    void RenderFilterBar(int64_t now_unix);
    void RenderResultsTable(int64_t now_unix);
    void RenderDetailPanel(int64_t now_unix);
    void RenderBookingForm(int64_t now_unix);
    void RenderConflictBanner();
    void RenderErrorBanner();
    void RenderLoadingOverlay();

    // Status badge helper: colours a row label by bookability.
    static void RenderBookabilityBadge(const domain::BookabilityResult& b);

    controllers::KennelBoardController& ctrl_;
    shell::SessionContext&               session_;

    // Per-frame form buffers (cleared on BeginCreateBooking).
    char guest_name_buf_[128] = {};
    char phone_buf_[32]       = {};
    char email_buf_[128]      = {};
    char special_buf_[512]    = {};

    // UI state
    bool         open_               = true;
    // Filter bar state
    int          date_in_year_       = 2024;
    int          date_in_month_      = 1;
    int          date_in_day_        = 1;
    int          date_out_year_      = 2024;
    int          date_out_month_     = 1;
    int          date_out_day_       = 2;
    float        min_rating_slider_  = 0.0f;
    int          max_price_input_    = 0;
    char         species_buf_[32]    = {};
    int          species_idx_        = 0;
    bool         agg_flag_           = false;
    char         zone_ids_buf_[128]  = {};   // comma-separated zone ids
    int          max_dist_input_     = 0;    // feet; 0 = no limit
};

} // namespace shelterops::ui::views
