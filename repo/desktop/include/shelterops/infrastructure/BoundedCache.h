#pragma once
#include <list>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <mutex>
#include <functional>

namespace shelterops::infrastructure {

// Thread-safe LRU cache with optional per-entry TTL.
// On capacity overflow the least-recently-used entry is evicted.
// TTL of 0 means entries never expire.
template <typename Key, typename Value>
class BoundedCache {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit BoundedCache(size_t max_size,
                           std::chrono::seconds ttl = std::chrono::seconds{0})
        : max_size_(max_size), ttl_(ttl) {}

    // Insert or replace an entry.
    void Put(const Key& key, Value value) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            lru_.erase(it->second.list_it);
            map_.erase(it);
        }
        lru_.push_front({key, std::move(value), Clock::now()});
        map_[key] = {lru_.begin()};
        if (map_.size() > max_size_) {
            EvictLruLocked();
        }
    }

    // Returns the cached value, or std::nullopt if missing or expired.
    std::optional<Value> Get(const Key& key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;

        auto& node = *(it->second.list_it);
        if (IsExpiredLocked(node)) {
            lru_.erase(it->second.list_it);
            map_.erase(it);
            return std::nullopt;
        }
        // Move to front (most recently used).
        lru_.splice(lru_.begin(), lru_, it->second.list_it);
        it->second.list_it = lru_.begin();
        return node.value;
    }

    void Invalidate(const Key& key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            lru_.erase(it->second.list_it);
            map_.erase(it);
        }
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mu_);
        lru_.clear();
        map_.clear();
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(mu_);
        return map_.size();
    }

private:
    struct Node {
        Key       key;
        Value     value;
        TimePoint inserted_at;
    };

    struct MapEntry {
        typename std::list<Node>::iterator list_it;
    };

    bool IsExpiredLocked(const Node& node) const {
        if (ttl_.count() == 0) return false;
        return (Clock::now() - node.inserted_at) > ttl_;
    }

    void EvictLruLocked() {
        if (lru_.empty()) return;
        const auto& back = lru_.back();
        map_.erase(back.key);
        lru_.pop_back();
    }

    size_t                                              max_size_;
    std::chrono::seconds                                ttl_;
    std::list<Node>                                     lru_;
    std::unordered_map<Key, MapEntry>                   map_;
    std::mutex                                          mu_;
};

} // namespace shelterops::infrastructure
