#pragma once
#include "shelterops/repositories/KennelRepository.h"
#include "shelterops/repositories/BookingRepository.h"
#include "shelterops/repositories/InventoryRepository.h"
#include "shelterops/repositories/ReportRepository.h"
#include "shelterops/repositories/AuditRepository.h"
#include "shelterops/shell/KeyboardShortcutHandler.h"
#include "shelterops/domain/Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace shelterops::ui::controllers {

enum class SearchCategory {
    Kennel, Booking, Inventory, Report, AuditEvent
};

struct SearchResultItem {
    SearchCategory    category      = SearchCategory::Kennel;
    int64_t           record_id     = 0;
    std::string       display_text;   // may be masked for Auditor
    std::string       detail_snippet; // brief context line
    bool              is_masked      = false;
    shell::WindowId   target_window  = shell::WindowId::KennelBoard;
};

struct SearchResults {
    std::string                    query;
    std::vector<SearchResultItem>  items;
    int64_t                        searched_at = 0;
    bool                           completed   = false;
};

// Runs a keyword search across kennels, bookings, inventory, and reports.
// Results are masked per the caller's role (Auditor always receives masked snippets).
// Cross-platform; no ImGui dependency.
class GlobalSearchController {
public:
    GlobalSearchController(
        repositories::KennelRepository&    kennels,
        repositories::BookingRepository&   bookings,
        repositories::InventoryRepository& inventory,
        repositories::ReportRepository&    reports,
        repositories::AuditRepository&     audit);

    // Execute a search across all categories accessible to `role`.
    SearchResults Search(const std::string& query,
                         domain::UserRole   role,
                         int64_t            now_unix) const;

private:
    void SearchKennels (const std::string& q, domain::UserRole r,
                        std::vector<SearchResultItem>& out) const;
    void SearchBookings(const std::string& q, domain::UserRole r,
                        std::vector<SearchResultItem>& out) const;
    void SearchInventory(const std::string& q, domain::UserRole r,
                         std::vector<SearchResultItem>& out) const;
    void SearchReports  (const std::string& q, domain::UserRole r,
                         std::vector<SearchResultItem>& out) const;

    static bool IcontainsQuery(const std::string& haystack, const std::string& needle);
    static std::string MaskName(const std::string& full_name);

    repositories::KennelRepository&    kennels_;
    repositories::BookingRepository&   bookings_;
    repositories::InventoryRepository& inventory_;
    repositories::ReportRepository&    reports_;
    repositories::AuditRepository&     audit_;
};

} // namespace shelterops::ui::controllers
