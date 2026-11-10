#include <gtest/gtest.h>
#include "persistence/wal_manager.hpp"
#include "persistence/wal_recovery.hpp"
#include "storage/sharded_store.hpp"
#include <fstream>
#include <cstdio>
#include <sys/stat.h>

TEST(WalRecoveryTest, CleanShutdownAndReplay) {
    const std::string wal_path = "recovery_test.wal";
    std::remove(wal_path.c_str());

    {
        WalManager wal(wal_path, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(wal.open());

        wal.append_set("user_1", "Alice");
        wal.append_set("user_2", "Bob");
        wal.append_set("user_3", "Charlie");
        wal.append_del("user_2");
    }

    ShardedStore clean_store(16);
    EXPECT_EQ(clean_store.total_keys(), 0);

    RecoveryStats stats;
    EXPECT_TRUE(WalRecovery::recover(wal_path, clean_store, stats));

    EXPECT_EQ(stats.records_replayed, 4);
    EXPECT_EQ(stats.set_ops, 3);
    EXPECT_EQ(stats.del_ops, 1);
    EXPECT_FALSE(stats.torn_tail_detected);

    EXPECT_EQ(clean_store.get("user_1").value(), "Alice");
    EXPECT_FALSE(clean_store.get("user_2").has_value());
    EXPECT_EQ(clean_store.get("user_3").value(), "Charlie");

    std::remove(wal_path.c_str());
}

TEST(WalRecoveryTest, TornWriteCorruptionHandledSafely) {
    const std::string wal_path = "torn_write_test.wal";
    std::remove(wal_path.c_str());

    size_t valid_file_size = 0;

    // 1. Write 2 valid records
    {
        WalManager wal(wal_path, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(wal.open());
        wal.append_set("survivor_1", "val1");
        wal.append_set("survivor_2", "val2");
    }

    {
        struct stat st{};
        stat(wal_path.c_str(), &st);
        valid_file_size = st.st_size;
    }

    // 2. Append garbage to simulate torn write
    {
        std::ofstream out(wal_path, std::ios::binary | std::ios::app);
        const char garbage[] = {0x56, 0x4B, 0x00, 0x01, 0x02};
        out.write(garbage, sizeof(garbage));
    }

    // 3. Recovery must recover valid prefix and truncate corrupted tail back to valid_file_size
    ShardedStore store(16);
    RecoveryStats stats;
    EXPECT_TRUE(WalRecovery::recover(wal_path, store, stats));

    EXPECT_EQ(stats.records_replayed, 2);
    EXPECT_TRUE(stats.torn_tail_detected);

    EXPECT_EQ(store.get("survivor_1").value(), "val1");
    EXPECT_EQ(store.get("survivor_2").value(), "val2");

    // 4. Verify file was physically truncated on disk
    struct stat truncated_st{};
    stat(wal_path.c_str(), &truncated_st);
    EXPECT_EQ(truncated_st.st_size, valid_file_size);

    std::remove(wal_path.c_str());
}