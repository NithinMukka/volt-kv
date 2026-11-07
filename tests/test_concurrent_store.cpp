#include <gtest/gtest.h>
#include "storage/sharded_store.hpp"
#include <thread>
#include <vector>
#include <string>

TEST(ShardedStoreConcurrencyTest, TwentyThreadsReadWriteStress) {
    const size_t num_shards = 16;
    const int num_threads = 20;
    const int ops_per_thread = 500; // 10 * 500 = 5,000 keys (fits comfortably in 16,000 capacity)

    ShardedStore store(num_shards);
    std::vector<std::thread> workers;
    workers.reserve(num_threads);

    // 10 Writer threads
    for (int t = 0; t < num_threads / 2; ++t) {
        workers.emplace_back([&store, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                std::string key = "th_" + std::to_string(t) + "_k" + std::to_string(i);
                std::string val = "v_" + std::to_string(i);
                store.set(std::move(key), std::move(val));
            }
        });
    }

    // 10 Reader/Deleter threads
    for (int t = num_threads / 2; t < num_threads; ++t) {
        workers.emplace_back([&store, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                // Try reading keys written by other threads
                int target_thread = t - (num_threads / 2);
                std::string key = "th_" + std::to_string(target_thread) + "_k" + std::to_string(i);
                store.get(key);
            }
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    // Verification: all written keys must either exist or have consistent state
    for (int t = 0; t < num_threads / 2; ++t) {
        for (int i = 0; i < ops_per_thread; ++i) {
            std::string key = "th_" + std::to_string(t) + "_k" + std::to_string(i);
            auto val = store.get(key);
            ASSERT_TRUE(val.has_value());
            EXPECT_EQ(val.value(), "v_" + std::to_string(i));
        }
    }
}