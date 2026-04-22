#include <gtest/gtest.h>
#include "shelterops/domain/ReportStageGraph.h"

using namespace shelterops::domain;

TEST(ReportStageGraph, OrderIsCollectCleansAnalyzeVisualize) {
    auto seq = AllStages();
    ASSERT_EQ(4u, seq.size());
    EXPECT_EQ(ReportStage::Collect,   seq[0]);
    EXPECT_EQ(ReportStage::Cleanse,   seq[1]);
    EXPECT_EQ(ReportStage::Analyze,   seq[2]);
    EXPECT_EQ(ReportStage::Visualize, seq[3]);
}

TEST(ReportStageGraph, NextStageCollect) {
    auto next = NextStage(ReportStage::Collect);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(ReportStage::Cleanse, *next);
}

TEST(ReportStageGraph, NextStageCleanse) {
    auto next = NextStage(ReportStage::Cleanse);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(ReportStage::Analyze, *next);
}

TEST(ReportStageGraph, NextStageAnalyze) {
    auto next = NextStage(ReportStage::Analyze);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(ReportStage::Visualize, *next);
}

TEST(ReportStageGraph, NextStageVisualizeReturnsNullopt) {
    auto next = NextStage(ReportStage::Visualize);
    EXPECT_FALSE(next.has_value());
}
