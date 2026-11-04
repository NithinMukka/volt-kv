#pragma once

#include "storage/shard.hpp"
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <optional>

class ShardedStore {
public:
    explicit ShardedStore(size_t num_shards = 16);

    void set(std::string key, std::string value);
    std::optional<std::string> get(std::string_view key) const;
    bool del(std::string_view key);

private:
    size_t shardid(std::string_view key) const;

    size_t num_shards_;
    std::vector<std::unique_ptr<Shard>> shards_;
};