#pragma once

#include "storage/shard.hpp"
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <cstdint>

class ShardedStore {
public:
    explicit ShardedStore(size_t num_shards = 16, size_t capacity_per_shard = 100000);

    void set(std::string key, std::string value, uint64_t ttl_ms = 0);
    std::optional<std::string> get(std::string_view key) const;
    bool del(std::string_view key);

    void purge_all_expired();
    size_t total_keys() const;

    std::vector<Shard::SnapshotEntry> capture_snapshot();

private:
    size_t shardid(std::string_view key) const;

    size_t num_shards_;
    size_t capacity_per_shard_;
    std::vector<std::unique_ptr<Shard>> shards_;
};