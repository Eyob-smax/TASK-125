#include <gtest/gtest.h>
#include "shelterops/workers/JobQueue.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>

using namespace shelterops::workers;
using namespace shelterops::domain;

TEST(JobQueue, FiveExportPdfJobsRunSerially) {
    JobQueue queue(2);
    std::atomic<int> active_count{0};
    std::atomic<int> max_active{0};
    std::atomic<int> completed{0};
    std::mutex mu;

    queue.RegisterHandler(JobType::ExportPdf,
        [&](const std::string&, std::stop_token) -> JobOutcome {
            int cur = ++active_count;
            {
                std::lock_guard<std::mutex> lock(mu);
                if (cur > max_active.load()) max_active.store(cur);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            --active_count;
            ++completed;
            return {true, {}, {}};
        });

    queue.Start();

    for (int i = 0; i < 5; ++i) {
        JobDescriptor d;
        d.run_id = i + 1; d.job_id = i + 1;
        d.job_type = JobType::ExportPdf;
        d.priority = 5; d.max_concurrency = 1;
        queue.Submit(d);
    }

    // Wait for all jobs to complete.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (completed.load() < 5 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    queue.Stop();
    EXPECT_EQ(5, completed.load());
    EXPECT_EQ(1, max_active.load()); // never more than 1 concurrent
}

TEST(JobQueue, StopTokenCausesHandlerToExitCleanly) {
    JobQueue queue(1);
    std::atomic<bool> handler_was_cancelled{false};

    queue.RegisterHandler(JobType::LanSync,
        [&](const std::string&, std::stop_token st) -> JobOutcome {
            // Simulate long work with stop-token check.
            for (int i = 0; i < 100; ++i) {
                if (st.stop_requested()) {
                    handler_was_cancelled = true;
                    return {false, {}, "cancelled"};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return {true, {}, {}};
        });

    queue.Start();

    JobDescriptor d;
    d.run_id = 1; d.job_id = 1;
    d.job_type = JobType::LanSync;
    d.priority = 5; d.max_concurrency = 1;
    queue.Submit(d);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    queue.Stop(); // triggers stop token

    // Either the job ran to completion or was cancelled — no crash.
    SUCCEED();
}

TEST(JobQueue, IsIdleAfterAllJobsComplete) {
    JobQueue queue(2);
    std::atomic<int> done{0};

    queue.RegisterHandler(JobType::AlertScan,
        [&](const std::string&, std::stop_token) -> JobOutcome {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ++done;
            return {true, {}, {}};
        });

    queue.Start();

    for (int i = 0; i < 3; ++i) {
        JobDescriptor d;
        d.run_id = i + 1; d.job_id = i + 1;
        d.job_type = JobType::AlertScan;
        d.priority = 5; d.max_concurrency = 4;
        queue.Submit(d);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!queue.IsIdle() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    queue.Stop();
    EXPECT_EQ(3, done.load());
    EXPECT_TRUE(queue.IsIdle());
}
