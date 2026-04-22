#include "shelterops/infrastructure/RateLimiter.h"
#include <cmath>

namespace shelterops::infrastructure {

RateLimiter::RateLimiter(int rpm)
    : rpm_(rpm)
    , capacity_(static_cast<double>(rpm))
    , refill_rate_per_ns_(rpm / 60.0 / 1'000'000'000.0) {}

void RateLimiter::Refill(Bucket& b,
                          std::chrono::steady_clock::time_point now) {
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - b.last_refill).count();
    double added = static_cast<double>(elapsed_ns) * refill_rate_per_ns_;
    b.tokens = std::min(capacity_, b.tokens + added);
    b.last_refill = now;
}

AcquireResult RateLimiter::TryAcquire(const std::string& key) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mu_);

    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        Bucket b;
        b.tokens     = capacity_ - 1.0; // consume first token immediately
        b.last_refill = now;
        buckets_[key] = b;
        return {true, 0};
    }

    Bucket& b = it->second;
    Refill(b, now);

    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        return {true, 0};
    }

    // How many seconds until one token is available.
    double secs_until_token = (1.0 - b.tokens) / (refill_rate_per_ns_ * 1e9);
    int retry = static_cast<int>(std::ceil(secs_until_token));
    return {false, std::max(1, retry)};
}

void RateLimiter::Evict(const std::string& key) {
    std::lock_guard<std::mutex> lock(mu_);
    buckets_.erase(key);
}

void RateLimiter::Reset() {
    std::lock_guard<std::mutex> lock(mu_);
    buckets_.clear();
}

} // namespace shelterops::infrastructure
