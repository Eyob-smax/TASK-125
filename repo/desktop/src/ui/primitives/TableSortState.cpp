#include "shelterops/ui/primitives/TableSortState.h"
#include <algorithm>
#include <sstream>

namespace shelterops::ui::primitives {

void TableSortState::SetSort(int col_idx, bool ascending) {
    sort_ = { col_idx, ascending };
}

void TableSortState::ToggleSort(int col_idx) {
    if (sort_.column_idx == col_idx)
        sort_.ascending = !sort_.ascending;
    else
        sort_ = { col_idx, true };
}

void TableSortState::SetFilter(const std::string& text) {
    filter_text_ = text;
}

void TableSortState::ClearFilter() {
    filter_text_.clear();
}

std::vector<std::size_t> TableSortState::ComputeIndices(
    std::size_t row_count,
    std::function<bool(std::size_t, std::size_t)> comparator,
    std::function<bool(std::size_t)>              filter_pred) const
{
    std::vector<std::size_t> indices;
    indices.reserve(row_count);
    for (std::size_t i = 0; i < row_count; ++i) {
        if (!filter_pred || filter_pred(i))
            indices.push_back(i);
    }
    if (sort_.column_idx >= 0 && comparator)
        std::stable_sort(indices.begin(), indices.end(), comparator);
    return indices;
}

std::string TableSortState::FormatTsv(
    const std::vector<std::string>& headers,
    const std::vector<std::size_t>& indices,
    std::size_t                     col_count,
    std::function<std::string(std::size_t, std::size_t)> get_cell)
{
    std::ostringstream oss;
    for (std::size_t c = 0; c < headers.size(); ++c) {
        if (c) oss << '\t';
        oss << headers[c];
    }
    oss << '\n';
    for (std::size_t row : indices) {
        for (std::size_t c = 0; c < col_count; ++c) {
            if (c) oss << '\t';
            oss << get_cell(row, c);
        }
        oss << '\n';
    }
    return oss.str();
}

} // namespace shelterops::ui::primitives
