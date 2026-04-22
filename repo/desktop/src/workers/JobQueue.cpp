#include "shelterops/workers/JobQueue.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>

namespace shelterops::workers {

namespace {

// Returns the enforced per-type concurrency cap.
int TypeCap(domain::JobType type, int descriptor_max) {
    switch (type) {
    case domain::JobType::ExportPdf:        return 1;
    case domain::JobType::ExportCsv:        return 1;
    case domain::JobType::ReportGenerate:   return 2;
    default:                                return descriptor_max > 0 ? descriptor_max : 4;
    }
}

} // namespace

JobQueue::JobQueue(int worker_count)
    : worker_count_(worker_count) {
    // Pre-populate type caps for known types.
    type_caps_[static_cast<int>(domain::JobType::ExportPdf)]      = 1;
    type_caps_[static_cast<int>(domain::JobType::ExportCsv)]      = 1;
    type_caps_[static_cast<int>(domain::JobType::ReportGenerate)]  = 2;
    type_caps_[static_cast<int>(domain::JobType::RetentionRun)]    = 1;
    type_caps_[static_cast<int>(domain::JobType::AlertScan)]       = 2;
    type_caps_[static_cast<int>(domain::JobType::LanSync)]         = 1;
    type_caps_[static_cast<int>(domain::JobType::Backup)]          = 1;
}

JobQueue::~JobQueue() {
    Stop();
}

void JobQueue::RegisterHandler(domain::JobType type, JobHandler handler) {
    std::lock_guard<std::mutex> lock(mu_);
    handlers_[static_cast<int>(type)] = std::move(handler);
}

void JobQueue::SetLifecycleCallbacks(JobStartCallback on_start,
                                     JobFinishCallback on_finish) {
    std::lock_guard<std::mutex> lock(mu_);
    on_start_ = std::move(on_start);
    on_finish_ = std::move(on_finish);
}

void JobQueue::Start() {
    std::lock_guard<std::mutex> lock(mu_);
    if (started_) return;
    started_ = true;
    workers_.reserve(static_cast<size_t>(worker_count_));
    for (int i = 0; i < worker_count_; ++i) {
        workers_.emplace_back([this](std::stop_token st) {
            WorkerLoop(st);
        });
    }
}

void JobQueue::Stop() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!started_) return;
        started_ = false;
    }
    // Request stop and wake all workers.
    for (auto& t : workers_) t.request_stop();
    cv_.notify_all();
    type_cap_cv_.notify_all();
    workers_.clear();  // jthread destructor joins
}

void JobQueue::Submit(const JobDescriptor& desc) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push(QueueEntry{desc});
    }
    cv_.notify_one();
}

bool JobQueue::IsIdle() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.empty() && in_flight_.load() == 0;
}

void JobQueue::WorkerLoop(std::stop_token stop) {
    while (!stop.stop_requested()) {
        JobDescriptor desc;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] {
                return stop.stop_requested() || !queue_.empty();
            });
            if (stop.stop_requested() && queue_.empty()) return;
            if (queue_.empty()) continue;

            // Peek at the top entry.
            auto entry = queue_.top();
            int type_key = static_cast<int>(entry.desc.job_type);
            int cap = TypeCap(entry.desc.job_type, entry.desc.max_concurrency);

            // If at type cap, wait without popping.
            if (active_by_type_[type_key] >= cap) {
                // Release mutex and wait on type_cap_cv_.
                lock.unlock();
                std::unique_lock<std::mutex> cap_lock(mu_);
                type_cap_cv_.wait(cap_lock, [&] {
                    return stop.stop_requested() ||
                           active_by_type_[type_key] < cap ||
                           queue_.empty();
                });
                continue;
            }

            queue_.pop();
            active_by_type_[type_key]++;
            in_flight_++;
            desc = entry.desc;
        }

        // Execute handler outside lock.
        int type_key = static_cast<int>(desc.job_type);
        JobHandler handler;
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto it = handlers_.find(type_key);
            if (it != handlers_.end()) handler = it->second;
        }

        JobOutcome outcome;
        JobStartCallback on_start;
        JobFinishCallback on_finish;
        {
            std::lock_guard<std::mutex> lock(mu_);
            on_start = on_start_;
            on_finish = on_finish_;
        }

        const int64_t started_at = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::ostringstream worker_ss;
        worker_ss << "worker-" << std::this_thread::get_id();
        if (on_start) {
            on_start(desc, worker_ss.str(), started_at);
        }
        if (handler) {
            try {
                outcome = handler(desc.parameters_json, stop);
            } catch (const std::exception& ex) {
                outcome.success       = false;
                outcome.error_message = ex.what();
                spdlog::error("JobQueue: job {} (run {}) threw: {}",
                              desc.job_id, desc.run_id, ex.what());
            }
        } else {
            outcome.success       = false;
            outcome.error_message = "handler not registered";
            spdlog::warn("JobQueue: no handler for job type {}", type_key);
        }

        if (!outcome.success) {
            spdlog::warn("JobQueue: job {} (run {}) failed: {}",
                         desc.job_id, desc.run_id, outcome.error_message);
        }

        if (on_finish) {
            const int64_t completed_at = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            on_finish(desc, outcome, completed_at);
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            active_by_type_[type_key]--;
            in_flight_--;
        }
        type_cap_cv_.notify_all();
        cv_.notify_one();
    }
}

} // namespace shelterops::workers
