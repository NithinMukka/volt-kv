#include "storage/sharded_store.hpp"
#include <functional>
#include <algorithm>
#include <shared_mutex>

ShardedStore::ShardedStore(size_t num_shards, size_t capacity_per_shard) 
    : num_shards_(std::max<size_t>(1, num_shards)),
      capacity_per_shard_(std::max<size_t>(1, capacity_per_shard)) {
    shards_.reserve(num_shards_);
    for (size_t i = 0; i < num_shards_; ++i) {
        shards_.push_back(std::make_unique<Shard>(capacity_per_shard_));
    }
}

size_t ShardedStore::shardid(std::string_view key) const {
    return std::hash<std::string_view>{}(key) % num_shards_;
}

void ShardedStore::set(std::string key, std::string value, uint64_t ttl_ms) {
    size_t idx = shardid(key);
    shards_[idx]->set(std::move(key), std::move(value), ttl_ms);
}

std::optional<std::string> ShardedStore::get(std::string_view key) const {
    size_t idx = shardid(key);
    return shards_[idx]->get(key);
}

bool ShardedStore::del(std::string_view key) {
    size_t idx = shardid(key);
    return shards_[idx]->del(key);
}

void ShardedStore::purge_all_expired() {
    for (const auto& shard : shards_) {
        shard->purge_expired();
    }
}

size_t ShardedStore::total_keys() const {
    size_t total = 0;
    for (const auto& shard : shards_) {
        total += shard->size();
    }
    return total;
}

std::vector<Shard::SnapshotEntry> ShardedStore::capture_snapshot() {
    std::vector<std::shared_lock<std::shared_mutex>> locks;
    locks.reserve(shards_.size());
    for (const auto& shard : shards_) {
        locks.emplace_back(shard->get_mutex());
    }

    std::vector<Shard::SnapshotEntry> all_entries;
    for (const auto& shard : shards_) {
        shard->dump_entries_unlocked(all_entries);
    }

    return all_entries;
}