#include "persistence/wal_manager.hpp"
#include "persistence/crc32.hpp"
#include "storage/shard.hpp" // for now_ms()
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

WalManager::WalManager(const std::string& filepath, FsyncPolicy policy)
    : filepath_(filepath), policy_(policy) {}

WalManager::~WalManager() {
    close();
}

bool WalManager::open() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ >= 0) return true;

    // Open append-only: create if missing, write-only, append mode
    fd_ = ::open(filepath_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        std::cerr << "[WalManager] Failed to open WAL file: " << filepath_ << "\n";
        return false;
    }
    return true;
}

void WalManager::close() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ >= 0) {
        if (policy_ == FsyncPolicy::ALWAYS) {
            ::fsync(fd_);
        }
        ::close(fd_);
        fd_ = -1;
    }
}

bool WalManager::sync() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ < 0) return false;
    return ::fsync(fd_) == 0;
}

bool WalManager::append_set(std::string_view key, std::string_view val, uint64_t expire_at_ms) {
    return write_record(WalOpType::SET, key, val, expire_at_ms);
}

bool WalManager::append_del(std::string_view key) {
    return write_record(WalOpType::DEL, key, "", now_ms());
}

bool WalManager::write_record(WalOpType op, std::string_view key, std::string_view val, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (fd_ < 0) return false;

    uint32_t key_len = static_cast<uint32_t>(key.size());
    uint32_t val_len = static_cast<uint32_t>(val.size());

    // Total payload size before CRC32: 19 bytes header + key + val
    size_t payload_size = 2 + 8 + 1 + 4 + 4 + key_len + val_len;
    size_t total_record_size = payload_size + 4; // +4 for CRC32

    std::vector<uint8_t> buffer;
    buffer.reserve(total_record_size);

    // 1. Magic (2 bytes)
    buffer.push_back(static_cast<uint8_t>((WAL_MAGIC >> 8) & 0xFF));
    buffer.push_back(static_cast<uint8_t>(WAL_MAGIC & 0xFF));

    // 2. Timestamp (8 bytes, Big-Endian)
    for (int i = 7; i >= 0; --i) {
        buffer.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
    }

    // 3. OpType (1 byte)
    buffer.push_back(static_cast<uint8_t>(op));

    // 4. Key Length (4 bytes, Big-Endian)
    for (int i = 3; i >= 0; --i) {
        buffer.push_back(static_cast<uint8_t>((key_len >> (i * 8)) & 0xFF));
    }

    // 5. Value Length (4 bytes, Big-Endian)
    for (int i = 3; i >= 0; --i) {
        buffer.push_back(static_cast<uint8_t>((val_len >> (i * 8)) & 0xFF));
    }

    // 6. Key bytes
    buffer.insert(buffer.end(), key.begin(), key.end());

    // 7. Value bytes
    buffer.insert(buffer.end(), val.begin(), val.end());

    // 8. Calculate CRC32 over the payload
    uint32_t checksum = CRC32::calculate(buffer.data(), buffer.size());

    // 9. Append Checksum (4 bytes, Big-Endian)
    for (int i = 3; i >= 0; --i) {
        buffer.push_back(static_cast<uint8_t>((checksum >> (i * 8)) & 0xFF));
    }

    // Write buffer to file sequentially
    const uint8_t* write_ptr = buffer.data();
    size_t remaining = buffer.size();

    while (remaining > 0) {
        ssize_t written = ::write(fd_, write_ptr, remaining);
        if (written <= 0) {
            std::cerr << "[WalManager] Failed to write WAL record\n";
            return false;
        }
        write_ptr += written;
        remaining -= written;
    }

    // Durability enforcement
    if (policy_ == FsyncPolicy::ALWAYS) {
        return ::fsync(fd_) == 0;
    }

    return true;
}