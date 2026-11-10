#include "persistence/wal_recovery.hpp"
#include "persistence/wal_manager.hpp"
#include "persistence/crc32.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>
#include <iostream>
#include <cstring>

namespace {
constexpr uint16_t WAL_MAGIC = 0x564B;
constexpr size_t HEADER_SIZE = 19;
}

bool WalRecovery::recover(const std::string& wal_filepath, ShardedStore& store, RecoveryStats& stats) {
    stats = RecoveryStats{};

    int fd = ::open(wal_filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        return true; // Clean first boot
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size == 0) {
        ::close(fd);
        return true;
    }

    std::vector<uint8_t> buffer(st.st_size);
    ssize_t bytes_read = ::read(fd, buffer.data(), st.st_size);
    ::close(fd);

    if (bytes_read != st.st_size) {
        std::cerr << "[WalRecovery] Error reading WAL file\n";
        return false;
    }

    size_t offset = 0;
    const size_t total_size = static_cast<size_t>(bytes_read);

    while (offset < total_size) {
        if (offset + HEADER_SIZE + 4 > total_size) {
            std::cerr << "[WalRecovery] Incomplete record header at tail. Stopping replay.\n";
            stats.torn_tail_detected = true;
            break;
        }

        size_t record_start = offset;

        // 1. Magic
        uint16_t magic = (static_cast<uint16_t>(buffer[offset]) << 8) | buffer[offset + 1];
        if (magic != WAL_MAGIC) {
            std::cerr << "[WalRecovery] Invalid magic at offset " << offset << ". Stopping replay.\n";
            stats.torn_tail_detected = true;
            break;
        }

        // 2. Timestamp (expire_at_ms)
        uint64_t expire_at_ms = 0;
        for (int i = 0; i < 8; ++i) {
            expire_at_ms = (expire_at_ms << 8) | buffer[offset + 2 + i];
        }

        // 3. OpType
        uint8_t op_byte = buffer[offset + 10];
        if (op_byte != static_cast<uint8_t>(WalOpType::SET) && 
            op_byte != static_cast<uint8_t>(WalOpType::DEL)) {
            std::cerr << "[WalRecovery] Corrupted op type at offset " << offset << "\n";
            stats.torn_tail_detected = true;
            break;
        }
        auto op = static_cast<WalOpType>(op_byte);

        // 4. KeyLen & ValLen
        uint32_t key_len = 0;
        for (int i = 0; i < 4; ++i) {
            key_len = (key_len << 8) | buffer[offset + 11 + i];
        }

        uint32_t val_len = 0;
        for (int i = 0; i < 4; ++i) {
            val_len = (val_len << 8) | buffer[offset + 15 + i];
        }

        size_t payload_size = HEADER_SIZE + key_len + val_len;
        size_t total_record_size = payload_size + 4;

        if (record_start + total_record_size > total_size) {
            std::cerr << "[WalRecovery] Partial payload at tail. Stopping replay.\n";
            stats.torn_tail_detected = true;
            break;
        }

        // 5. CRC32 Check
        uint32_t expected_crc = 0;
        size_t crc_offset = record_start + payload_size;
        for (int i = 0; i < 4; ++i) {
            expected_crc = (expected_crc << 8) | buffer[crc_offset + i];
        }

        uint32_t actual_crc = CRC32::calculate(buffer.data() + record_start, payload_size);
        if (actual_crc != expected_crc) {
            std::cerr << "[WalRecovery] CRC mismatch at offset " << record_start << ". Stopping replay.\n";
            stats.torn_tail_detected = true;
            break;
        }

        // 6. Apply valid record
        std::string key(reinterpret_cast<const char*>(buffer.data() + record_start + HEADER_SIZE), key_len);
        std::string val(reinterpret_cast<const char*>(buffer.data() + record_start + HEADER_SIZE + key_len), val_len);

        if (op == WalOpType::SET) {
            uint64_t current = now_ms();
            if (expire_at_ms == 0 || expire_at_ms > current) {
                uint64_t remaining_ttl = (expire_at_ms > current) ? (expire_at_ms - current) : 0;
                store.set(std::move(key), std::move(val), remaining_ttl);
            }
            stats.set_ops++;
        } else if (op == WalOpType::DEL) {
            store.del(key);
            stats.del_ops++;
        }

        stats.records_replayed++;
        offset += total_record_size;
    }

    // Truncate corrupted tail from disk so future appends write to clean space
    if (stats.torn_tail_detected) {
        std::cerr << "[WalRecovery] Physically truncating torn tail at offset " << offset << "\n";
        if (::truncate(wal_filepath.c_str(), static_cast<off_t>(offset)) != 0) {
            std::cerr << "[WalRecovery] Failed to truncate file: " << strerror(errno) << "\n";
        }
    }

    return true;
}