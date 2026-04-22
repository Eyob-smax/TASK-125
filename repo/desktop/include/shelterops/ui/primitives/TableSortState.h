#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstddef>

namespace shelterops::ui::primitives {

struct ColumnSort {
    int  column_idx = -1;   // -1 = unsorted
    bool ascending  = true;
};

// Manages sort column, sort direction, and filter text for a data table.
// Cross-platform: no ImGui dependency. The view layer calls these helpers.
class TableSortState {
public:
    TableSortState() = default;

    void SetSort(int col_idx, bool ascending);
    void ToggleSort(int col_idx);       // flip direction if same col; else sort asc
    void SetFilter(const std::string& text);
    void ClearFilter();

    ColumnSort         GetSort()   const noexcept { return sort_; }
    const std::string& GetFilter() const noexcept { return filter_text_; }

    // Returns row indices in display order after applying sort and filter.
    // comparator(a, b) returns true if row[a] should appear before row[b].
    // filter_pred(i) returns true if row[i] passes the current filter.
    std::vector<std::size_t> ComputeIndices(
        std::size_t row_count,
        std::function<bool(std::size_t, std::size_t)> comparator,
        std::function<bool(std::size_t)>              filter_pred) const;

    // Format the visible row subset as tab-separated values (header row + data rows).
    // get_cell(row_idx, col_idx) must return the display string for that cell.
    static std::string FormatTsv(
        const std::vector<std::string>& headers,
        const std::vector<std::size_t>& indices,
        std::size_t                     col_count,
        std::function<std::string(std::size_t, std::size_t)> get_cell);

private:
    ColumnSort  sort_;
    std::string filter_text_;
};

} // namespace shelterops::ui::primitives
