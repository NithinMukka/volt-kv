#include <gtest/gtest.h>
#include "persistence/wal_manager.hpp"
#include "persistence/crc32.hpp"
#include <fstream>
#include <vector>
#include <cstdio>

TEST(WalManagerTest, BasicWriteAndFileIntegrity) {
    const std::string test_file = "test_voltkv.wal";
    std::remove(test_file.c_str());

    {
        WalManager wal(test_file, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(wal.open());

        EXPECT_TRUE(wal.append_set("key1", "val1"));
        EXPECT_TRUE(wal.append_set("key2", "value_two"));
        EXPECT_TRUE(wal.append_del("key1"));
    } // Closes file safely on destruction

    // Read back file bytes to verify structure
    std::ifstream in(test_file, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(in.is_open());
    std::streamsize file_size = in.tellg();
    EXPECT_GT(file_size, 0);

    // Record 1: 19 header + 4 ("key1") + 4 ("val1") + 4 (crc) = 31 bytes
    // Record 2: 19 header + 4 ("key2") + 9 ("value_two") + 4 (crc) = 36 bytes
    // Record 3: 19 header + 4 ("key1") + 0 ("") + 4 (crc) = 27 bytes
    // Total expected size = 31 + 36 + 27 = 94 bytes
    EXPECT_EQ(file_size, 94);

    // Clean up
    std::remove(test_file.c_str());
}

TEST(CRC32Test, DeterministicChecksum) {
    std::string data = "VoltKV High Throughput Engine";
    uint32_t crc1 = CRC32::calculate(data.data(), data.size());
    uint32_t crc2 = CRC32::calculate(data.data(), data.size());

    EXPECT_NE(crc1, 0);
    EXPECT_EQ(crc1, crc2);

    std::string altered_data = "VoltKV High Throughput Enginf"; // 1 bit flip
    uint32_t crc3 = CRC32::calculate(altered_data.data(), altered_data.size());
    EXPECT_NE(crc1, crc3);
}