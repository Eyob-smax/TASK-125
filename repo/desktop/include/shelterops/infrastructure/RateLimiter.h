#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace shelterops::infrastructure {

struct AcquireResult {
    bool allowed              = false;
    int  retry_after_seconds  = 0;
};

// Token-bucket rate limiter keyed by an arbitrary string (e.g. session token).
// Thread-safe. Bucket capacity = max_tokens; refill at one token per
// (60.0 / rpm) seconds.  Each successful TryAcquire consumes one token.
class RateLimiter {
public:
    // rpm: maximum requests per minute per key.
    explicit RateLimiter(int rpm = 60);

    // Attempt to consume one token for the given key.
    // Returns {allowed=true, retry_after=0} on success.
    // Returns {allowed=false, retry_after>0} when bucket is empty.
    AcquireResult TryAcquire(const std::string& key);

    // Remove state for a key (e.g. on session logout).
    void Evict(const std::string& key);

    // Reset all buckets (testing helper).
    void Reset();

private:
    struct Bucket {
        double tokens          = 0.0;
        std::chrono::steady_clock::time_point last_refill;
    };

    void Refill(Bucket& b, std::chrono::steady_clock::time_point now);

    int    rpm_;
    double capacity_;          // burst capacity = rpm (1 minute's worth)
    double refill_rate_per_ns_; // tokens per nanosecond

    std::mutex mu_;
    std::unordered_map<std::string, Bucket> buckets_;
};

} // namespace shelterops::infrastructure
