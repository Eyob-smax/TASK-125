#include <gtest/gtest.h>
#include "shelterops/ui/primitives/TableSortState.h"

using namespace shelterops::ui::primitives;

TEST(TableSortState, DefaultStateUnsorted) {
    TableSortState ts;
    EXPECT_EQ(-1, ts.GetSort().column_idx);
    EXPECT_TRUE(ts.GetFilter().empty());
}

TEST(TableSortState, SetSortUpdatesState) {
    TableSortState ts;
    ts.SetSort(2, false);
    EXPECT_EQ(2, ts.GetSort().column_idx);
    EXPECT_FALSE(ts.GetSort().ascending);
}

TEST(TableSortState, ToggleSortSameColumn) {
    TableSortState ts;
    ts.SetSort(1, true);
    ts.ToggleSort(1);           // same col → flip direction
    EXPECT_EQ(1, ts.GetSort().column_idx);
    EXPECT_FALSE(ts.GetSort().ascending);
    ts.ToggleSort(1);
    EXPECT_TRUE(ts.GetSort().ascending);
}

TEST(TableSortState, ToggleSortDifferentColumn) {
    TableSortState ts;
    ts.SetSort(1, false);
    ts.ToggleSort(3);           // different col → reset to ascending
    EXPECT_EQ(3, ts.GetSort().column_idx);
    EXPECT_TRUE(ts.GetSort().ascending);
}

TEST(TableSortState, FilterUpdatesState) {
    TableSortState ts;
    ts.SetFilter("dog");
    EXPECT_EQ("dog", ts.GetFilter());
    ts.ClearFilter();
    EXPECT_TRUE(ts.GetFilter().empty());
}

TEST(TableSortState, ComputeIndicesNoFilterNoSort) {
    TableSortState ts;
    auto idx = ts.ComputeIndices(4, nullptr, nullptr);
    ASSERT_EQ(4u, idx.size());
    EXPECT_EQ(0u, idx[0]); EXPECT_EQ(3u, idx[3]);
}

TEST(TableSortState, ComputeIndicesFilterRemovesRows) {
    TableSortState ts;
    // Keep only even indices.
    auto idx = ts.ComputeIndices(6, nullptr,
        [](std::size_t i){ return i % 2 == 0; });
    ASSERT_EQ(3u, idx.size());
    EXPECT_EQ(0u, idx[0]); EXPECT_EQ(2u, idx[1]); EXPECT_EQ(4u, idx[2]);
}

TEST(TableSortState, ComputeIndicesSortAscending) {
    TableSortState ts;
    ts.SetSort(0, true);
    // Rows: values 3, 1, 4, 2
    std::vector<int> vals = {3, 1, 4, 2};
    auto idx = ts.ComputeIndices(4,
        [&](std::size_t a, std::size_t b){ return vals[a] < vals[b]; },
        nullptr);
    ASSERT_EQ(4u, idx.size());
    EXPECT_EQ(1u, idx[0]); // value 1
    EXPECT_EQ(3u, idx[1]); // value 2
    EXPECT_EQ(0u, idx[2]); // value 3
    EXPECT_EQ(2u, idx[3]); // value 4
}

TEST(TableSortState, ComputeIndicesSortDescending) {
    TableSortState ts;
    ts.SetSort(0, false);
    std::vector<int> vals = {3, 1, 4, 2};
    auto idx = ts.ComputeIndices(4,
        [&](std::size_t a, std::size_t b){ return vals[a] > vals[b]; },
        nullptr);
    EXPECT_EQ(2u, idx[0]); // value 4
}

TEST(TableSortState, FormatTsvHeaders) {
    std::vector<std::string> hdrs = {"ID", "Name", "Qty"};
    std::vector<std::size_t> idx  = {0, 1};
    std::string tsv = TableSortState::FormatTsv(hdrs, idx, 3,
        [](std::size_t row, std::size_t col) -> std::string {
            static const char* data[2][3] = {
                {"1","Alpha","10"},{"2","Beta","5"}
            };
            return data[row][col];
        });
    EXPECT_EQ("ID\tName\tQty\n1\tAlpha\t10\n2\tBeta\t5\n", tsv);
}

TEST(TableSortState, FormatTsvEmptyRows) {
    std::vector<std::string> hdrs = {"A"};
    std::vector<std::size_t> idx;
    std::string tsv = TableSortState::FormatTsv(hdrs, idx, 1, nullptr);
    EXPECT_EQ("A\n", tsv);
}
