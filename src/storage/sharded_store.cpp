#include "storage/sharded_store.hpp"
#include <functional>

ShardedStore::ShardedStore(size_t num_shards) : num_shards_(num_shards) {
    for (size_t i = 0; i < num_shards_; ++i) {
        shards_.push_back(std::make_unique<Shard>());
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
