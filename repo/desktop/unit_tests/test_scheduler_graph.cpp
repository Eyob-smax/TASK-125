#include <gtest/gtest.h>
#include "shelterops/domain/SchedulerGraph.h"
#include <unordered_map>
#include <vector>
#include <algorithm>

using namespace shelterops::domain;

TEST(SchedulerGraph, AcyclicEdgesNoCycle) {
    // A→B→C — adding A→D should be fine
    std::vector<SchedulerEdge> edges = {{1,2},{2,3}};
    EXPECT_FALSE(HasCircularDependency(edges, 1, 4));
}

TEST(SchedulerGraph, SelfEdgeIsCycle) {
    std::vector<SchedulerEdge> edges;
    EXPECT_TRUE(HasCircularDependency(edges, 5, 5));
}

TEST(SchedulerGraph, DirectMutualDependencyCycle) {
    // A→B exists; adding B→A should detect cycle
    std::vector<SchedulerEdge> edges = {{1,2}};
    EXPECT_TRUE(HasCircularDependency(edges, 2, 1));
}

TEST(SchedulerGraph, LongChainCycle) {
    // A→B→C→D; adding D→A should detect cycle
    std::vector<SchedulerEdge> edges = {{1,2},{2,3},{3,4}};
    EXPECT_TRUE(HasCircularDependency(edges, 4, 1));
}

TEST(SchedulerGraph, TopologicalOrderRespectsDependencies) {
    // A(1)→B(2)→C(3); root=1; order should have 1 before 2 before 3
    std::vector<SchedulerEdge> edges = {{1,2},{2,3}};
    auto order = TopologicalOrder(edges, 1);
    ASSERT_GE(order.size(), 3u);
    auto pos = [&](int64_t id) {
        return std::distance(order.begin(),
               std::find(order.begin(), order.end(), id));
    };
    EXPECT_LT(pos(1), pos(2));
    EXPECT_LT(pos(2), pos(3));
}

TEST(SchedulerGraph, NextReadyJobsOnlyReturnsJobsWithPrereqsCompleted) {
    // Job 1 depends on job 2. Job 2 depends on job 3.
    // Only job 3 has no incomplete prerequisites.
    std::vector<SchedulerEdge> edges = {{1,2},{2,3}};
    std::unordered_map<int64_t, std::string> statuses = {
        {1, "queued"}, {2, "queued"}, {3, "queued"}
    };
    auto ready = NextReadyJobs(edges, statuses);
    // Job 3 has no prerequisites → ready; 1 and 2 blocked
    ASSERT_EQ(1u, ready.size());
    EXPECT_EQ(3, ready[0]);
}

TEST(SchedulerGraph, NextReadyJobsAfterPrereqCompleted) {
    std::vector<SchedulerEdge> edges = {{1,2}};
    std::unordered_map<int64_t, std::string> statuses = {
        {1, "queued"}, {2, "completed"}
    };
    auto ready = NextReadyJobs(edges, statuses);
    ASSERT_EQ(1u, ready.size());
    EXPECT_EQ(1, ready[0]);
}
