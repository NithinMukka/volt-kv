#include <gtest/gtest.h>
#include "persistence/snapshot.hpp"
#include "persistence/wal_manager.hpp"
#include "storage/sharded_store.hpp"
#include <cstdio>
#include <sys/stat.h>

TEST(SnapshotTest, CreateLoadAndWalTruncate) {
    const std::string snap_file = "test_snapshot.snap";
    const std::string wal_file = "test_snap_wal.wal";
    std::remove(snap_file.c_str());
    std::remove(wal_file.c_str());

    ShardedStore store(16);
    WalManager wal(wal_file, FsyncPolicy::ALWAYS);
    ASSERT_TRUE(wal.open());

    // 1. Insert 500 keys into store and WAL
    for (int i = 0; i < 500; ++i) {
        std::string k = "k" + std::to_string(i);
        std::string v = "v" + std::to_string(i);
        wal.append_set(k, v);
        store.set(k, v);
    }

    struct stat wal_st{};
    stat(wal_file.c_str(), &wal_st);
    EXPECT_GT(wal_st.st_size, 0);

    // 2. Create Snapshot
    SnapshotStats snap_stats;
    EXPECT_TRUE(SnapshotManager::create_snapshot(snap_file, store, wal, snap_stats));
    EXPECT_EQ(snap_stats.keys_saved, 500);
    EXPECT_TRUE(snap_stats.success);

    // Verify WAL was truncated to 0
    stat(wal_file.c_str(), &wal_st);
    EXPECT_EQ(wal_st.st_size, 0);

    // 3. Restore into clean store
    ShardedStore clean_store(16);
    SnapshotStats load_stats;
    EXPECT_TRUE(SnapshotManager::load_snapshot(snap_file, clean_store, load_stats));
    EXPECT_EQ(load_stats.keys_saved, 500);

    for (int i = 0; i < 500; ++i) {
        std::string k = "k" + std::to_string(i);
        auto val = clean_store.get(k);
        ASSERT_TRUE(val.has_value());
        EXPECT_EQ(val.value(), "v" + std::to_string(i));
    }

    std::remove(snap_file.c_str());
    std::remove(wal_file.c_str());
}