#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <cstdint>
#include <memory>

class RateLimiter {
public:
    explicit RateLimiter(
        double refill_rate = 500.0,      // Tokens per second
        double burst_capacity = 1000.0,  // Max burst
        size_t num_shards = 16,
        size_t max_ips_per_shard = 5000
    );

    // Checks if request from client_ip is allowed
    bool allow(std::string_view client_ip);

    // Whitelist specific IP (e.g., localhost 127.0.0.1)
    void whitelist_ip(const std::string& ip);

private:
    struct Bucket {
        double tokens;
        uint64_t last_refill_ms;
    };

    struct LimiterShard {
        mutable std::mutex mtx;
        std::unordered_map<std::string, Bucket> buckets;
    };

    size_t get_shard(std::string_view ip) const;

    double refill_rate_;
    double burst_capacity_;
    size_t num_shards_;
    size_t max_ips_per_shard_;

    std::vector<std::unique_ptr<LimiterShard>> shards_;

    mutable std::mutex whitelist_mtx_;
    std::unordered_set<std::string> whitelist_;
};