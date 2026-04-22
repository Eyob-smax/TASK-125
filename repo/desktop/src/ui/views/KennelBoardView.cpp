#if defined(_WIN32)
#include "shelterops/ui/views/KennelBoardView.h"
#include "shelterops/shell/ClipboardHelper.h"
#include "shelterops/shell/ErrorDisplay.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace shelterops::ui::views {

static const char* kSpeciesOptions[] = {
    "Any", "Dog", "Cat", "Rabbit", "Bird", "Reptile", "Other"
};

KennelBoardView::KennelBoardView(
    controllers::KennelBoardController& ctrl,
    shell::SessionContext&               session)
    : ctrl_(ctrl), session_(session)
{}

bool KennelBoardView::Render(int64_t now_unix) {
    if (!open_) return false;

    ImGui::SetNextWindowSize({900.0f, 600.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Intake & Kennel Board", &open_)) {
        ImGui::End();
        return open_;
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        // Signal AppController when this window gains focus.
    }

    auto state = ctrl_.State();

    // Toolbar
    if (ImGui::Button("Search##kb") ||
        (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_Enter)))
        ctrl_.Refresh(session_.Get(), now_unix);

    ImGui::SameLine();
    bool can_write = session_.CurrentRole() != domain::UserRole::Auditor;
    if (!can_write) ImGui::BeginDisabled();
    if (ImGui::Button("New Booking [Ctrl+N]") &&
        ctrl_.SelectedKennel() != 0)
        ctrl_.BeginCreateBooking(ctrl_.SelectedKennel());
    if (!can_write) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Copy TSV [Ctrl+Shift+E]"))
        shell::ClipboardHelper::SetText(ctrl_.ClipboardTsv());

    ImGui::Separator();
    RenderFilterBar(now_unix);
    ImGui::Separator();

    if (state == controllers::KennelBoardState::Loading) {
        RenderLoadingOverlay();
    } else if (state == controllers::KennelBoardState::Error) {
        RenderErrorBanner();
    } else if (state == controllers::KennelBoardState::BookingConflict) {
        RenderConflictBanner();
    } else if (state == controllers::KennelBoardState::CreatingBooking ||
               state == controllers::KennelBoardState::BookingSuccess) {
        RenderDetailPanel(now_unix);
    } else {
        RenderResultsTable(now_unix);
        if (ctrl_.SelectedKennel() != 0)
            RenderDetailPanel(now_unix);
    }

    ImGui::End();
    return open_;
}

// Convert y/m/d ints to a Unix midnight UTC timestamp.
static int64_t YmdToUnix(int y, int m, int d) {
    std::tm t{};
    t.tm_year = y - 1900;
    t.tm_mon  = m - 1;
    t.tm_mday = d;
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
    std::time_t ts = std::mktime(&t);
    return static_cast<int64_t>(ts);
}

// Parse "1,2,3" into a vector of int64_t zone ids.
static std::vector<int64_t> ParseZoneIds(const char* buf) {
    std::vector<int64_t> out;
    std::istringstream ss(buf);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try {
            int64_t v = std::stoll(tok);
            if (v > 0) out.push_back(v);
        } catch (...) {}
    }
    return out;
}

void KennelBoardView::RenderFilterBar(int64_t /*now_unix*/) {
    auto f = ctrl_.CurrentFilter();
    controllers::KennelBoardFilter updated = f;
    bool changed = false;

    // ── Row 1: dates ─────────────────────────────────────────────────────────
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Check-in:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    if (ImGui::InputInt("Y##ci", &date_in_year_,  0)) changed = true;
    ImGui::SameLine(); ImGui::SetNextItemWidth(40.0f);
    if (ImGui::InputInt("M##ci", &date_in_month_, 0)) changed = true;
    ImGui::SameLine(); ImGui::SetNextItemWidth(40.0f);
    if (ImGui::InputInt("D##ci", &date_in_day_,   0)) changed = true;
    date_in_month_ = std::max(1, std::min(12, date_in_month_));
    date_in_day_   = std::max(1, std::min(31, date_in_day_));

    ImGui::SameLine();
    ImGui::TextUnformatted("Check-out:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    if (ImGui::InputInt("Y##co", &date_out_year_,  0)) changed = true;
    ImGui::SameLine(); ImGui::SetNextItemWidth(40.0f);
    if (ImGui::InputInt("M##co", &date_out_month_, 0)) changed = true;
    ImGui::SameLine(); ImGui::SetNextItemWidth(40.0f);
    if (ImGui::InputInt("D##co", &date_out_day_,   0)) changed = true;
    date_out_month_ = std::max(1, std::min(12, date_out_month_));
    date_out_day_   = std::max(1, std::min(31, date_out_day_));

    if (changed) {
        updated.check_in_at  = YmdToUnix(date_in_year_,  date_in_month_,  date_in_day_);
        updated.check_out_at = YmdToUnix(date_out_year_, date_out_month_, date_out_day_);
    }

    // ── Row 2: zone ids, max distance, species, rating, price, aggressive ───
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Zones (ids):");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputText("##zones", zone_ids_buf_, sizeof(zone_ids_buf_))) {
        updated.zone_ids = ParseZoneIds(zone_ids_buf_);
        changed = true;
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("Max dist(ft):");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::InputInt("##maxd", &max_dist_input_)) {
        if (max_dist_input_ < 0) max_dist_input_ = 0;
        // max_dist is stored in the domain filter via BookingService; expose via KennelBoardFilter
        changed = true;
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("Species:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::Combo("##species", &species_idx_,
                      kSpeciesOptions,
                      static_cast<int>(std::size(kSpeciesOptions)))) {
        static const char* kSpeciesKeys[] = {
            "", "dog", "cat", "rabbit", "bird", "reptile", "other"
        };
        updated.species = kSpeciesKeys[species_idx_];
        changed = true;
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("Min Rating:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::SliderFloat("##minr", &min_rating_slider_, 0.0f, 5.0f, "%.1f")) {
        updated.min_rating = min_rating_slider_;
        changed = true;
    }

    ImGui::SameLine();
    ImGui::TextUnformatted("Max $/night:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt("##maxp", &max_price_input_)) {
        updated.max_price_cents = max_price_input_ * 100;
        changed = true;
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Aggressive", &agg_flag_)) {
        updated.is_aggressive = agg_flag_;
        changed = true;
    }

    if (changed)
        ctrl_.SetFilter(updated);
}

void KennelBoardView::RenderResultsTable(int64_t now_unix) {
    const auto& results = ctrl_.Results();
    if (results.empty()) {
        ImGui::TextDisabled("No kennels match the current filter. "
                            "Adjust filters or click Search.");
        return;
    }

    auto& sort = ctrl_.SortState();

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Sortable;
    float table_h = ImGui::GetContentRegionAvail().y * 0.55f;

    if (!ImGui::BeginTable("##kennels", 7, flags, {0.0f, table_h})) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Rank",      ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
    ImGui::TableSetupColumn("Kennel",    ImGuiTableColumnFlags_None,        0.0f, 1);
    ImGui::TableSetupColumn("Zone",      ImGuiTableColumnFlags_None,        0.0f, 2);
    ImGui::TableSetupColumn("$/night",   ImGuiTableColumnFlags_None,        0.0f, 3);
    ImGui::TableSetupColumn("Rating",    ImGuiTableColumnFlags_None,        0.0f, 4);
    ImGui::TableSetupColumn("Bookable",  ImGuiTableColumnFlags_None,        0.0f, 5);
    ImGui::TableSetupColumn("Reasons",   ImGuiTableColumnFlags_NoSort,      0.0f, 6);
    ImGui::TableHeadersRow();

    if (auto* sort_specs = ImGui::TableGetSortSpecs()) {
        if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
            const auto& s = sort_specs->Specs[0];
            sort.SetSort(s.ColumnIndex,
                         s.SortDirection == ImGuiSortDirection_Ascending);
            sort_specs->SpecsDirty = false;
        }
    }

    auto indices = sort.ComputeIndices(
        results.size(),
        [&](std::size_t a, std::size_t b) {
            const auto& ra = results[a]; const auto& rb = results[b];
            switch (sort.GetSort().column_idx) {
            case 0: return sort.GetSort().ascending ? ra.rank < rb.rank : ra.rank > rb.rank;
            case 3: return sort.GetSort().ascending
                    ? ra.kennel.nightly_price_cents < rb.kennel.nightly_price_cents
                    : ra.kennel.nightly_price_cents > rb.kennel.nightly_price_cents;
            case 4: return sort.GetSort().ascending
                    ? ra.kennel.rating < rb.kennel.rating
                    : ra.kennel.rating > rb.kennel.rating;
            default: return a < b;
            }
        },
        nullptr);

    for (std::size_t i : indices) {
        const auto& rk = results[i];
        ImGui::TableNextRow();
        bool selected = rk.kennel_id == ctrl_.SelectedKennel();

        ImGui::TableSetColumnIndex(0);
        char sel_id[32]; std::snprintf(sel_id, sizeof(sel_id), "##row%d", (int)i);
        if (ImGui::Selectable(std::to_string(rk.rank).c_str(),
                              selected,
                              ImGuiSelectableFlags_SpanAllColumns,
                              {0, 0}))
            ctrl_.SelectKennel(rk.kennel_id);

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%lld", (long long)rk.kennel_id);

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%lld", (long long)rk.kennel.zone_id);

        ImGui::TableSetColumnIndex(3);
        ImGui::Text("$%d", rk.kennel.nightly_price_cents / 100);

        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.1f", rk.kennel.rating);

        ImGui::TableSetColumnIndex(5);
        RenderBookabilityBadge(rk.bookability);

        ImGui::TableSetColumnIndex(6);
        std::string reasons;
        for (const auto& r : rk.reasons) {
            if (!reasons.empty()) reasons += ", ";
            reasons += r.code;
        }
        ImGui::TextUnformatted(reasons.c_str());
    }
    ImGui::EndTable();

    // Hover tooltip with full explanation.
    for (std::size_t i : indices) {
        const auto& rk = results[i];
        if (rk.kennel_id == ctrl_.SelectedKennel()) {
            std::string expl = ctrl_.BookabilityExplanation(rk);
            if (!expl.empty()) {
                ImGui::TextDisabled("Explanation: %s", expl.c_str());
            }
        }
    }
    (void)now_unix;
}

void KennelBoardView::RenderDetailPanel(int64_t now_unix) {
    auto state = ctrl_.State();

    if (state == controllers::KennelBoardState::BookingSuccess) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::TextUnformatted("Booking created successfully!");
        ImGui::PopStyleColor();
        if (ImGui::Button("Close")) {
            ctrl_.CancelCreateBooking();
            ctrl_.ClearError();
        }
        return;
    }

    if (state != controllers::KennelBoardState::CreatingBooking) {
        ImGui::SeparatorText("Booking Form");
        if (ctrl_.SelectedKennel() == 0) {
            ImGui::TextDisabled("Select a kennel to create a booking.");
            return;
        }
        if (ImGui::Button("Create Booking for selected kennel"))
            ctrl_.BeginCreateBooking(ctrl_.SelectedKennel());
        return;
    }

    RenderBookingForm(now_unix);
}

void KennelBoardView::RenderBookingForm(int64_t now_unix) {
    auto& form = ctrl_.FormState();
    const auto& vstate = ctrl_.Validation();
    auto role = session_.CurrentRole();

    ImGui::SeparatorText("New Booking");

    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("Guest Name##kb", guest_name_buf_, sizeof(guest_name_buf_)))
        form.guest_name = guest_name_buf_;
    if (vstate.HasError("guest_name")) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.3f,0.3f,1.0f));
        ImGui::TextUnformatted(vstate.GetError("guest_name").c_str());
        ImGui::PopStyleColor();
    }

    // Phone and email only editable by non-Auditors.
    if (role != domain::UserRole::Auditor) {
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("Phone##kb", phone_buf_, sizeof(phone_buf_)))
            form.guest_phone = phone_buf_;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::InputText("Email##kb", email_buf_, sizeof(email_buf_)))
            form.guest_email = email_buf_;
    }

    ImGui::SetNextItemWidth(400.0f);
    if (ImGui::InputText("Special Requirements##kb",
                          special_buf_, sizeof(special_buf_)))
        form.special_requirements = special_buf_;

    // Date inputs (using Unix seconds from int inputs for simplicity).
    ImGui::SetNextItemWidth(160.0f);
    int ci = static_cast<int>(form.check_in_at);
    if (ImGui::InputInt("Check-in (unix)##kb", &ci)) form.check_in_at = ci;
    if (vstate.HasError("check_in_at")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
        ImGui::TextUnformatted(vstate.GetError("check_in_at").c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(160.0f);
    int co = static_cast<int>(form.check_out_at);
    if (ImGui::InputInt("Check-out (unix)##kb", &co)) form.check_out_at = co;
    if (vstate.HasError("check_out_at")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
        ImGui::TextUnformatted(vstate.GetError("check_out_at").c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    bool submit_disabled = role == domain::UserRole::Auditor;
    if (submit_disabled) ImGui::BeginDisabled();
    if (ImGui::Button("Submit Booking"))
        ctrl_.SubmitBooking(session_.Get(), now_unix);
    if (submit_disabled) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel##kb"))
        ctrl_.CancelCreateBooking();
}

void KennelBoardView::RenderConflictBanner() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.1f, 1.0f));
    ImGui::TextWrapped("Booking conflict: %s",
                        ctrl_.LastError().message.c_str());
    ImGui::PopStyleColor();
    if (ImGui::Button("Go back")) ctrl_.CancelCreateBooking();
}

void KennelBoardView::RenderErrorBanner() {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImGui::TextWrapped("Error: %s",
                        shell::ErrorDisplay::UserMessage(ctrl_.LastError()).c_str());
    ImGui::PopStyleColor();
    if (ImGui::Button("Dismiss##kb")) ctrl_.ClearError();
}

void KennelBoardView::RenderLoadingOverlay() {
    ImGui::TextUnformatted("Loading kennels...");
}

void KennelBoardView::RenderBookabilityBadge(const domain::BookabilityResult& b) {
    if (b.is_bookable) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::TextUnformatted("Available");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextUnformatted("Unavailable");
    }
    ImGui::PopStyleColor();
}

} // namespace shelterops::ui::views
#endif // _WIN32
