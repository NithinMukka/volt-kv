#include "protocol/rate_limiter.hpp"
#include "storage/shard.hpp" // for now_ms()
#include <algorithm>
#include <functional>

RateLimiter::RateLimiter(
    double refill_rate,
    double burst_capacity,
    size_t num_shards,
    size_t max_ips_per_shard
) : refill_rate_(refill_rate),
    burst_capacity_(burst_capacity),
    num_shards_(std::max<size_t>(1, num_shards)),
    max_ips_per_shard_(std::max<size_t>(100, max_ips_per_shard)) {
    
    shards_.reserve(num_shards_);
    for (size_t i = 0; i < num_shards_; ++i) {
        shards_.push_back(std::make_unique<LimiterShard>());
    }
}

size_t RateLimiter::get_shard(std::string_view ip) const {
    return std::hash<std::string_view>{}(ip) % num_shards_;
}

void RateLimiter::whitelist_ip(const std::string& ip) {
    std::lock_guard<std::mutex> lock(whitelist_mtx_);
    whitelist_.insert(ip);
}

bool RateLimiter::allow(std::string_view client_ip) {
    // 1. Check Whitelist
    {
        std::lock_guard<std::mutex> lock(whitelist_mtx_);
        if (whitelist_.find(std::string(client_ip)) != whitelist_.end()) {
            return true;
        }
    }

    size_t shard_idx = get_shard(client_ip);
    auto& shard = *shards_[shard_idx];

    std::lock_guard<std::mutex> lock(shard.mtx);
    uint64_t current_time = now_ms();

    auto it = shard.buckets.find(std::string(client_ip));
    if (it == shard.buckets.end()) {
        // Defense: Memory cap against IP spoofing DoS
        if (shard.buckets.size() >= max_ips_per_shard_) {
            // Evict an arbitrary idle bucket
            shard.buckets.erase(shard.buckets.begin());
        }

        // New IP starts with full capacity minus 1 token for this request
        shard.buckets[std::string(client_ip)] = Bucket{burst_capacity_ - 1.0, current_time};
        return true;
    }

    Bucket& bucket = it->second;

    // Refill tokens based on elapsed time
    if (current_time > bucket.last_refill_ms) {
        double elapsed_seconds = (current_time - bucket.last_refill_ms) / 1000.0;
        double new_tokens = bucket.tokens + (elapsed_seconds * refill_rate_);
        bucket.tokens = std::min(burst_capacity_, new_tokens);
        bucket.last_refill_ms = current_time;
    }

    if (bucket.tokens >= 1.0) {
        bucket.tokens -= 1.0;
        return true;
    }

    return false; // Rate limited
}