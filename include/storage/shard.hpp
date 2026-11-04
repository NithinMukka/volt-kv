#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <shared_mutex>
#include <optional>

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const {
        return std::hash<std::string_view>{}(sv);
    }
};

class Shard {
public:
    Shard() = default;
    Shard(const Shard&) = delete;
    Shard& operator=(const Shard&) = delete;

    void set(std::string key, std::string value);
    std::optional<std::string> get(std::string_view key) const;
    bool del(std::string_view key);

private:
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> map_;
    mutable std::shared_mutex mutex_;
};