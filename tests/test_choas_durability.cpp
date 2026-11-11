#include <gtest/gtest.h>
#include "storage/sharded_store.hpp"
#include "persistence/wal_manager.hpp"
#include "persistence/wal_recovery.hpp"
#include "persistence/snapshot.hpp"
#include <thread>
#include <vector>
#include <fstream>
#include <cstdio>
#include <sys/stat.h>

TEST(ChaosDurabilityTest, HighLoadCrashSnapshotWalReplayVerification) {
    const std::string snap_file = "chaos_test.snap";
    const std::string wal_file = "chaos_test.wal";

    std::remove(snap_file.c_str());
    std::remove(wal_file.c_str());

    const int keys_before_snap = 10000;
    const int num_threads = 20;

    // =========================================================================
    // PHASE 1: Write 10,000 keys across 20 threads (Pre-Snapshot)
    // =========================================================================
    {
        // Explicitly set 50,000 capacity per shard (800,000 total capacity)
        ShardedStore store(16, 50000);
        WalManager wal(wal_file, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(wal.open());

        std::vector<std::thread> workers;
        int ops_per_worker = keys_before_snap / num_threads;

        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back([&store, &wal, t, ops_per_worker]() {
                for (int i = 0; i < ops_per_worker; ++i) {
                    int id = t * ops_per_worker + i;
                    std::string k = "pre_snap_" + std::to_string(id);
                    std::string v = "payload_" + std::to_string(id);

                    ASSERT_TRUE(wal.append_set(k, v));
                    store.set(std::move(k), std::move(v));
                }
            });
        }
        for (auto& th : workers) th.join();
        EXPECT_EQ(store.total_keys(), keys_before_snap);

        // =====================================================================
        // PHASE 2: Trigger Snapshot & WAL Reset under load
        // =====================================================================
        SnapshotStats snap_stats;
        ASSERT_TRUE(SnapshotManager::create_snapshot(snap_file, store, wal, snap_stats));
        EXPECT_EQ(snap_stats.keys_saved, keys_before_snap);

        struct stat wal_st{};
        stat(wal_file.c_str(), &wal_st);
        EXPECT_EQ(wal_st.st_size, 0);

        // =====================================================================
        // PHASE 3: Write another 10,000 mutations post-snapshot
        // =====================================================================
        workers.clear();
        int post_snap_ops = 5000;
        ops_per_worker = post_snap_ops / num_threads;

        // 1. Add 5,000 brand new keys
        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back([&store, &wal, t, ops_per_worker]() {
                for (int i = 0; i < ops_per_worker; ++i) {
                    int id = t * ops_per_worker + i;
                    std::string k = "post_snap_" + std::to_string(id);
                    std::string v = "new_payload_" + std::to_string(id);

                    ASSERT_TRUE(wal.append_set(k, v));
                    store.set(std::move(k), std::move(v));
                }
            });
        }
        for (auto& th : workers) th.join();

        // 2. Delete 5,000 keys from the pre-snapshot batch
        workers.clear();
        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back([&store, &wal, t, ops_per_worker]() {
                for (int i = 0; i < ops_per_worker; ++i) {
                    int id = t * ops_per_worker + i;
                    std::string k = "pre_snap_" + std::to_string(id);

                    ASSERT_TRUE(wal.append_del(k));
                    store.del(k);
                }
            });
        }
        for (auto& th : workers) th.join();

        EXPECT_EQ(store.total_keys(), 10000);

    } // Hard crash simulation: RAM wiped

    // =========================================================================
    // PHASE 4: Inject Chaos - Torn write at the tail
    // =========================================================================
    {
        std::ofstream out(wal_file, std::ios::binary | std::ios::app);
        const char torn_bytes[] = {0x56, 0x4B, 0x01, 0x02, 0x03, 0x04};
        out.write(torn_bytes, sizeof(torn_bytes));
    }

    // =========================================================================
    // PHASE 5: Cold Boot Recovery from Disk
    // =========================================================================
    ShardedStore recovered_store(16, 50000);

    // Step 1: Restore Snapshot
    SnapshotStats load_stats;
    ASSERT_TRUE(SnapshotManager::load_snapshot(snap_file, recovered_store, load_stats));
    EXPECT_EQ(load_stats.keys_saved, keys_before_snap);

    // Step 2: Replay subsequent WAL mutations
    RecoveryStats wal_stats;
    ASSERT_TRUE(WalRecovery::recover(wal_file, recovered_store, wal_stats));
    EXPECT_TRUE(wal_stats.torn_tail_detected);
    EXPECT_EQ(wal_stats.set_ops, 5000);
    EXPECT_EQ(wal_stats.del_ops, 5000);

    // =========================================================================
    // PHASE 6: 100% Data Integrity Verification
    // =========================================================================
    EXPECT_EQ(recovered_store.total_keys(), 10000);

    // 1. Deleted pre-snapshot keys must not exist
    for (int i = 0; i < 5000; ++i) {
        std::string k = "pre_snap_" + std::to_string(i);
        EXPECT_FALSE(recovered_store.get(k).has_value());
    }

    // 2. Surviving pre-snapshot keys must match exactly
    for (int i = 5000; i < 10000; ++i) {
        std::string k = "pre_snap_" + std::to_string(i);
        auto val = recovered_store.get(k);
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(val.value(), "payload_" + std::to_string(i));
    }

    // 3. New post-snapshot keys must match exactly
    for (int i = 0; i < 5000; ++i) {
        std::string k = "post_snap_" + std::to_string(i);
        auto val = recovered_store.get(k);
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(val.value(), "new_payload_" + std::to_string(i));
    }

    std::remove(snap_file.c_str());
    std::remove(wal_file.c_str());
}