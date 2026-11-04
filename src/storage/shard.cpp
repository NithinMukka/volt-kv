#include "storage/shard.hpp"
#include <mutex>

void Shard::set(std::string key, std::string value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_.insert_or_assign(std::move(key), std::move(value));
}

std::optional<std::string> Shard::get(std::string_view key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool Shard::del(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        map_.erase(it);
        return true;
    }
    return false;
}