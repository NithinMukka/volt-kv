#include <gtest/gtest.h>
#include "storage/sharded_store.hpp"
#include "storage/ttl_cleaner.hpp"
#include <thread>
#include <chrono>

TEST(TTLCleanerTest, ActiveBackgroundPurge) {
    ShardedStore store(4);
    TTLCleaner cleaner(store, std::chrono::milliseconds(50));

    // 100 temporary keys (60ms TTL)
    for (int i = 0; i < 100; ++i) {
        store.set("temp_" + std::to_string(i), "val", 60);
    }

    // 50 permanent keys (0 TTL)
    for (int i = 0; i < 50; ++i) {
        store.set("perm_" + std::to_string(i), "val", 0);
    }

    EXPECT_EQ(store.total_keys(), 150);

    cleaner.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    cleaner.stop();

    // Verify expired keys were purged without any get() calls
    EXPECT_EQ(store.total_keys(), 50);
}