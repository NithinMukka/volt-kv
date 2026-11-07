#pragma once

#include <atomic>
#include <cstdint>
#include <string>

struct ServerMetrics {
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> total_writes{0};
    std::atomic<uint64_t> total_deletes{0};
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};

    void reset() {
        total_reads.store(0, std::memory_order_relaxed);
        total_writes.store(0, std::memory_order_relaxed);
        total_deletes.store(0, std::memory_order_relaxed);
        cache_hits.store(0, std::memory_order_relaxed);
        cache_misses.store(0, std::memory_order_relaxed);
    }
};

// Global singleton instance for access across shards and parser
inline ServerMetrics& get_metrics() {
    static ServerMetrics metrics;
    return metrics;
}