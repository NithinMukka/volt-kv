#include "persistence/snapshot.hpp"
#include "persistence/crc32.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <iostream>
#include <cstring>
#include <vector>

bool SnapshotManager::create_snapshot(
    const std::string& snap_filepath,
    ShardedStore& store,
    WalManager& wal,
    SnapshotStats& stats
) {
    stats = SnapshotStats{};
    std::string tmp_filepath = snap_filepath + ".tmp";

    // 1. Phase 1: In-Memory Atomic Capture (RAM only)
    auto entries = store.capture_snapshot();
    stats.keys_saved = entries.size();

    // 2. Phase 2: Stream to temporary file
    int fd = ::open(tmp_filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        std::cerr << "[SnapshotManager] Failed to open " << tmp_filepath << ": " << strerror(errno) << "\n";
        return false;
    }

    std::vector<uint8_t> buffer;

    // 2.1 Header: Magic (4B) + Version (2B) + EntryCount (8B)
    for (int i = 3; i >= 0; --i) buffer.push_back((SNAP_MAGIC >> (i * 8)) & 0xFF);
    for (int i = 1; i >= 0; --i) buffer.push_back((SNAP_VERSION >> (i * 8)) & 0xFF);
    for (int i = 7; i >= 0; --i) buffer.push_back((entries.size() >> (i * 8)) & 0xFF);

    // 2.2 Serialize entries
    for (const auto& entry : entries) {
        uint32_t klen = static_cast<uint32_t>(entry.key.size());
        uint32_t vlen = static_cast<uint32_t>(entry.value.size());

        // expire_at_ms (8B)
        for (int i = 7; i >= 0; --i) buffer.push_back((entry.expire_at_ms >> (i * 8)) & 0xFF);
        // klen (4B)
        for (int i = 3; i >= 0; --i) buffer.push_back((klen >> (i * 8)) & 0xFF);
        // vlen (4B)
        for (int i = 3; i >= 0; --i) buffer.push_back((vlen >> (i * 8)) & 0xFF);

        buffer.insert(buffer.end(), entry.key.begin(), entry.key.end());
        buffer.insert(buffer.end(), entry.value.begin(), entry.value.end());
    }

    // 2.3 Checksum over entire payload
    uint32_t checksum = CRC32::calculate(buffer.data(), buffer.size());
    for (int i = 3; i >= 0; --i) buffer.push_back((checksum >> (i * 8)) & 0xFF);

    // Write to disk
    const uint8_t* ptr = buffer.data();
    size_t remaining = buffer.size();
    while (remaining > 0) {
        ssize_t written = ::write(fd, ptr, remaining);
        if (written <= 0) {
            ::close(fd);
            ::unlink(tmp_filepath.c_str()); // Clean up .tmp
            return false;
        }
        ptr += written;
        remaining -= written;
    }

    // Flush dirty pages to physical media before rename
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp_filepath.c_str());
        return false;
    }
    ::close(fd);

    stats.bytes_written = buffer.size();

    // 3. Atomic File Swap: POSIX rename() is guaranteed atomic by kernel
    if (::rename(tmp_filepath.c_str(), snap_filepath.c_str()) != 0) {
        std::cerr << "[SnapshotManager] Atomic rename failed: " << strerror(errno) << "\n";
        ::unlink(tmp_filepath.c_str());
        return false;
    }

    // 4. Truncate WAL to 0 bytes now that snapshot is committed
    if (!wal.reset()) {
        std::cerr << "[SnapshotManager] Warning: Snapshot saved, but failed to reset WAL\n";
    }

    stats.success = true;
    return true;
}

bool SnapshotManager::load_snapshot(
    const std::string& snap_filepath,
    ShardedStore& store,
    SnapshotStats& stats
) {
    stats = SnapshotStats{};

    int fd = ::open(snap_filepath.c_str(), O_RDONLY);
    if (fd < 0) return true; // No snapshot file exists (first boot)

    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size < 18) { // 14 header + 4 CRC
        ::close(fd);
        return true;
    }

    std::vector<uint8_t> buffer(st.st_size);
    ssize_t bytes_read = ::read(fd, buffer.data(), st.st_size);
    ::close(fd);

    if (bytes_read != st.st_size) return false;

    size_t total_size = static_cast<size_t>(bytes_read);
    size_t payload_size = total_size - 4;

    // Verify CRC32
    uint32_t expected_crc = 0;
    for (int i = 0; i < 4; ++i) {
        expected_crc = (expected_crc << 8) | buffer[payload_size + i];
    }

    if (CRC32::calculate(buffer.data(), payload_size) != expected_crc) {
        std::cerr << "[SnapshotManager] Snapshot file corrupted (CRC mismatch)!\n";
        return false;
    }

    // Validate Header
    size_t offset = 0;
    uint32_t magic = 0;
    for (int i = 0; i < 4; ++i) magic = (magic << 8) | buffer[offset++];
    if (magic != SNAP_MAGIC) {
        std::cerr << "[SnapshotManager] Invalid snapshot magic!\n";
        return false;
    }

    uint16_t version = 0;
    for (int i = 0; i < 2; ++i) version = (version << 8) | buffer[offset++];
    if (version != SNAP_VERSION) {
        std::cerr << "[SnapshotManager] Unsupported snapshot version: " << version << "\n";
        return false;
    }

    uint64_t entry_count = 0;
    for (int i = 0; i < 8; ++i) entry_count = (entry_count << 8) | buffer[offset++];

    // Restore entries
    uint64_t current = now_ms();
    for (uint64_t i = 0; i < entry_count; ++i) {
        if (offset + 16 > payload_size) return false;

        uint64_t expire_at_ms = 0;
        for (int j = 0; j < 8; ++j) expire_at_ms = (expire_at_ms << 8) | buffer[offset++];

        uint32_t klen = 0;
        for (int j = 0; j < 4; ++j) klen = (klen << 8) | buffer[offset++];

        uint32_t vlen = 0;
        for (int j = 0; j < 4; ++j) vlen = (vlen << 8) | buffer[offset++];

        if (offset + klen + vlen > payload_size) return false;

        std::string key(reinterpret_cast<const char*>(buffer.data() + offset), klen);
        offset += klen;
        std::string val(reinterpret_cast<const char*>(buffer.data() + offset), vlen);
        offset += vlen;

        // Skip keys that already expired
        if (expire_at_ms == 0 || expire_at_ms > current) {
            uint64_t remaining_ttl = (expire_at_ms > current) ? (expire_at_ms - current) : 0;
            store.set(std::move(key), std::move(val), remaining_ttl);
            stats.keys_saved++;
        }
    }

    stats.success = true;
    return true;
}