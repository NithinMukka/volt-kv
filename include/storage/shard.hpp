#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <shared_mutex>
#include <optional>
#include <cstdint>
#include <chrono>

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const {
        return std::hash<std::string_view>{}(sv);
    }
};

// Monotonic clock immune to NTP adjustments
inline uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

class Shard {
public:
    explicit Shard(size_t capacity = 1000);
    ~Shard();

    Shard(const Shard&) = delete;
    Shard& operator=(const Shard&) = delete;

    void set(std::string key, std::string value, uint64_t ttl_ms = 0);
    std::optional<std::string> get(std::string_view key);
    bool del(std::string_view key);

    size_t size() const;
    size_t purge_expired(size_t max_keys_to_check = 50);

private:
    struct Node {
        std::string key;
        std::string value;
        uint64_t expire_at_ms = 0;
        Node* prev = nullptr;
        Node* next = nullptr;
    };

    void detach(Node* node);
    void attach_head(Node* node);
    void move_to_head(Node* node);
    Node* pop_tail();

    size_t capacity_;
    std::unordered_map<std::string, Node*, StringHash, std::equal_to<>> map_;
    mutable std::shared_mutex mutex_;

    Node* head_;
    Node* tail_;

    // Cursor for TTL sweeping to avoid starvation
    typename std::unordered_map<std::string, Node*, StringHash, std::equal_to<>>::iterator sweep_cursor_;
};