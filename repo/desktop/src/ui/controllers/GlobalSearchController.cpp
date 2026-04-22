#include "shelterops/ui/controllers/GlobalSearchController.h"
#include <algorithm>
#include <cctype>
#include <chrono>

namespace shelterops::ui::controllers {

GlobalSearchController::GlobalSearchController(
    repositories::KennelRepository&    kennels,
    repositories::BookingRepository&   bookings,
    repositories::InventoryRepository& inventory,
    repositories::ReportRepository&    reports,
    repositories::AuditRepository&     audit)
    : kennels_(kennels), bookings_(bookings), inventory_(inventory),
      reports_(reports), audit_(audit)
{}

SearchResults GlobalSearchController::Search(
    const std::string& query,
    domain::UserRole   role,
    int64_t            now_unix) const
{
    SearchResults results;
    results.query       = query;
    results.searched_at = now_unix;

    if (query.size() < 2) {
        results.completed = true;
        return results;
    }

    SearchKennels (query, role, results.items);
    SearchBookings(query, role, results.items);
    SearchInventory(query, role, results.items);
    SearchReports (query, role, results.items);

    results.completed = true;
    return results;
}

bool GlobalSearchController::IcontainsQuery(
    const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(),   needle.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
    return it != haystack.end();
}

std::string GlobalSearchController::MaskName(const std::string& full_name) {
    if (full_name.empty()) return "***";
    std::string initials;
    bool next_is_initial = true;
    for (char c : full_name) {
        if (c == ' ' || c == '-') { next_is_initial = true; continue; }
        if (next_is_initial) {
            initials += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            initials += '.';
            next_is_initial = false;
        }
    }
    return initials.empty() ? "***" : initials;
}

void GlobalSearchController::SearchKennels(
    const std::string& q, domain::UserRole /*role*/,
    std::vector<SearchResultItem>& out) const
{
    auto kennels = kennels_.ListActiveKennels();
    for (const auto& k : kennels) {
        std::string id_str = std::to_string(k.kennel_id);
        std::string zone_str = std::to_string(k.zone_id);
        if (!IcontainsQuery(id_str, q) && !IcontainsQuery(k.name, q) && !IcontainsQuery(zone_str, q)) {
            continue;
        }
        SearchResultItem item;
        item.category     = SearchCategory::Kennel;
        item.record_id    = k.kennel_id;
        item.display_text = k.name.empty() ? ("Kennel #" + id_str) : k.name;
        item.detail_snippet = "Zone " + std::to_string(k.zone_id)
                            + " | $" + std::to_string(k.nightly_price_cents / 100)
                            + "/night";
        item.target_window = shell::WindowId::KennelBoard;
        out.push_back(std::move(item));
    }
}

void GlobalSearchController::SearchBookings(
    const std::string& q, domain::UserRole role,
    std::vector<SearchResultItem>& out) const
{
    auto bookings = bookings_.SearchByQuery(q, 100);
    for (const auto& b : bookings) {
        const std::string id_str = std::to_string(b.booking_id);
        if (!IcontainsQuery(id_str, q) && !IcontainsQuery(b.guest_name, q)) continue;

        SearchResultItem item;
        item.category  = SearchCategory::Booking;
        item.record_id = b.booking_id;

        if (role == domain::UserRole::Auditor) {
            item.display_text   = "Booking #" + id_str;
            item.detail_snippet = "status=" + b.status;
            item.is_masked      = true;
        } else {
            item.display_text   = b.guest_name.empty()
                ? ("Booking #" + id_str)
                : ("Booking #" + id_str + " - " + b.guest_name);
            item.detail_snippet = "status=" + b.status;
        }
        item.target_window = shell::WindowId::KennelBoard;
        out.push_back(std::move(item));
    }
}

void GlobalSearchController::SearchInventory(
    const std::string& q, domain::UserRole /*role*/,
    std::vector<SearchResultItem>& out) const
{
    auto items = inventory_.SearchByQuery(q, 200);

    for (const auto& it : items) {
        SearchResultItem result;
        result.category     = SearchCategory::Inventory;
        result.record_id    = it.item_id;
        result.display_text = it.name;
        result.detail_snippet = "Qty: " + std::to_string(it.quantity)
                              + " | " + it.storage_location;
        result.target_window = shell::WindowId::ItemLedger;
        out.push_back(std::move(result));
    }
}

void GlobalSearchController::SearchReports(
    const std::string& q, domain::UserRole /*role*/,
    std::vector<SearchResultItem>& out) const
{
    // ReportRepository doesn't have a list-all-definitions method beyond FindDefinition.
    // Query recent runs via audit for report events.
    repositories::AuditQueryFilter filter;
    filter.entity_type = "report";
    filter.limit       = 50;

    auto events = audit_.Query(filter);
    for (const auto& ev : events) {
        if (!IcontainsQuery(ev.description, q) &&
            !IcontainsQuery(ev.event_type, q)) continue;

        SearchResultItem item;
        item.category     = SearchCategory::Report;
        item.record_id    = ev.entity_id;
        item.display_text = "Report Run #" + std::to_string(ev.entity_id);
        item.detail_snippet = ev.event_type;
        item.target_window = shell::WindowId::ReportsStudio;
        out.push_back(std::move(item));
    }
}

} // namespace shelterops::ui::controllers
