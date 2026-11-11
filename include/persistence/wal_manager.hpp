#pragma once

#include <string>
#include <string_view>
#include <cstdint>
#include <mutex>
#include <vector>

enum class WalOpType : uint8_t {
    SET = 0x01,
    DEL = 0x02
};

enum class FsyncPolicy {
    ALWAYS,   // Call fsync() on every write (maximum durability, lower throughput)
    PERIODIC  // Rely on OS page cache + periodic fsync (high throughput)
};

class WalManager {
public:
    explicit WalManager(const std::string& filepath, FsyncPolicy policy = FsyncPolicy::ALWAYS);
    ~WalManager();

    WalManager(const WalManager&) = delete;
    WalManager& operator=(const WalManager&) = delete;

    bool open();
    void close();

    // Appends a SET mutation to disk
    bool append_set(std::string_view key, std::string_view val, uint64_t expire_at_ms = 0);

    // Appends a DEL mutation to disk
    bool append_del(std::string_view key);

    // Forces OS dirty pages to physical disk
    bool sync();

    const std::string& get_filepath() const { return filepath_; }
    // Truncates WAL file to 0 bytes after successful snapshot
    bool reset();

private:
    bool write_record(WalOpType op, std::string_view key, std::string_view val, uint64_t timestamp);

    std::string filepath_;
    FsyncPolicy policy_;
    int fd_{-1};
    std::mutex write_mutex_;

    static constexpr uint16_t WAL_MAGIC = 0x564B; // 'V' 'K'
};