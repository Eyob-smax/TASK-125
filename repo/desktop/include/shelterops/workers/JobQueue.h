#pragma once
#include "shelterops/domain/Types.h"
#include <functional>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <atomic>
#include <cstdint>

namespace shelterops::workers {

struct JobDescriptor {
    int64_t         run_id          = 0;
    int64_t         job_id          = 0;
    domain::JobType job_type        = domain::JobType::ReportGenerate;
    std::string     parameters_json = "{}";
    int             priority        = 5;
    int             max_concurrency = 4;
    int64_t         submitted_at    = 0;
    int64_t         submitted_by    = 0;
};

struct JobOutcome {
    bool        success = false;
    std::string output_json;
    std::string error_message;
};

// Handler signature: receives params JSON and a stop token.
using JobHandler = std::function<JobOutcome(const std::string& params_json,
                                             std::stop_token cancel)>;
using JobStartCallback = std::function<void(const JobDescriptor& desc,
                                            const std::string& worker_id,
                                            int64_t started_at_unix)>;
using JobFinishCallback = std::function<void(const JobDescriptor& desc,
                                             const JobOutcome& outcome,
                                             int64_t completed_at_unix)>;

// Bounded thread pool with per-job-type concurrency caps.
// Workers are constructed but NOT started until Start() is called.
// Callers must call Stop() (or destroy the object) to drain and join workers.
class JobQueue {
public:
    // worker_count: total threads; default 2.
    // Type-specific concurrency caps: export_pdf=1, export_csv=1,
    //   report_generate=2, others=max_concurrency field from descriptor.
    explicit JobQueue(int worker_count = 2);
    ~JobQueue();

    // Register a handler for a job type. Must be called before Start().
    void RegisterHandler(domain::JobType type, JobHandler handler);
    void SetLifecycleCallbacks(JobStartCallback on_start,
                               JobFinishCallback on_finish);

    // Not started automatically — Start() must be called explicitly.
    void Start();
    void Stop();

    // Submit a job. Wakes an idle worker.
    void Submit(const JobDescriptor& desc);

    // Returns true if there are no queued or in-progress jobs.
    bool IsIdle() const;

private:
    void WorkerLoop(std::stop_token stop);

    struct QueueEntry {
        JobDescriptor desc;
        bool operator>(const QueueEntry& o) const {
            return desc.priority > o.desc.priority;   // lower number = higher prio
        }
    };

    int  worker_count_;
    bool started_ = false;

    mutable std::mutex  mu_;
    std::condition_variable cv_;
    // min-heap by priority
    std::priority_queue<QueueEntry,
                        std::vector<QueueEntry>,
                        std::greater<QueueEntry>> queue_;

    // Per-type concurrency tracking: current active count.
    std::unordered_map<int, int> active_by_type_;
    std::condition_variable      type_cap_cv_;

    std::unordered_map<int, JobHandler> handlers_;
    std::unordered_map<int, int>        type_caps_;  // JobType int → max concurrent
    JobStartCallback                    on_start_;
    JobFinishCallback                   on_finish_;
    std::vector<std::jthread> workers_;
    std::atomic<int>          in_flight_{0};
};

} // namespace shelterops::workers
