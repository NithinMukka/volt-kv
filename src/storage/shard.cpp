#include "storage/shard.hpp"
#include "metrics/telemetry.hpp"
#include <mutex>

Shard::Shard(size_t capacity) : capacity_(capacity) {
    head_ = new Node();
    tail_ = new Node();
    head_->next = tail_;
    tail_->prev = head_;
}

Shard::~Shard() {
    // Clear all dynamically allocated real nodes
    Node* curr = head_->next;
    while (curr != tail_) {
        Node* next = curr->next;
        delete curr;
        curr = next;
    }
    // Delete dummy sentinels
    delete head_;
    delete tail_;
}

void Shard::detach(Node* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void Shard::attach_head(Node* node) {
    node->next = head_->next;
    node->prev = head_;
    head_->next->prev = node;
    head_->next = node;
}

void Shard::move_to_head(Node* node) {
    detach(node);
    attach_head(node);
}

Shard::Node* Shard::pop_tail() {
    if (head_->next == tail_) {
        return nullptr;
    }
    Node* victim = tail_->prev;
    detach(victim);
    return victim;
}

size_t Shard::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return map_.size();
}

void Shard::set(std::string key, std::string value, uint64_t ttl_ms) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    uint64_t expire_at = (ttl_ms > 0) ? (now_ms() + ttl_ms) : 0;

    auto it = map_.find(key);
    if (it != map_.end()) {
        it->second->value = std::move(value);
        it->second->expire_at_ms = expire_at;
        move_to_head(it->second);
    } else {
        if (map_.size() >= capacity_) {
            Node* victim = pop_tail();
            if (victim) {
                map_.erase(victim->key);
                delete victim;
            }
        }
        Node* node = new Node{std::move(key), std::move(value), expire_at};
        attach_head(node);
        map_[node->key] = node;
    }
    get_metrics().total_writes.fetch_add(1, std::memory_order_relaxed);
}

std::optional<std::string> Shard::get(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    get_metrics().total_reads.fetch_add(1, std::memory_order_relaxed);
    auto it = map_.find(key);
    if (it == map_.end()) {
        get_metrics().cache_misses.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    Node* node = it->second;

    // Passive eviction check
    if (node->expire_at_ms != 0 && now_ms() >= node->expire_at_ms) {
        detach(node);
        map_.erase(it);
        delete node;
        return std::nullopt;
    }
    get_metrics().cache_hits.fetch_add(1, std::memory_order_relaxed);
    move_to_head(node);
    return node->value;
}

bool Shard::del(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        Node* node = it->second;
        detach(node);
        map_.erase(it);
        delete node;
        get_metrics().total_deletes.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

size_t Shard::purge_expired(size_t max_to_check) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (map_.empty()) {
        return 0;
    }

    size_t keys_purged = 0;
    size_t keys_checked = 0;
    uint64_t current_time = now_ms();

    for (auto it = map_.begin(); it != map_.end() && keys_checked < max_to_check; ) {
        Node* node = it->second;
        if (node->expire_at_ms != 0 && current_time >= node->expire_at_ms) {
            detach(node);
            it = map_.erase(it); // map_.erase(it) returns the next iterator
            delete node;
            keys_purged++;
        } else {
            ++it;
        }
        keys_checked++;
    }

    return keys_purged;
}