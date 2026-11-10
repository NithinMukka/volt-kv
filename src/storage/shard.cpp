#include "storage/shard.hpp"
#include "metrics/telemetry.hpp"
#include <mutex>
#include <algorithm>

Shard::Shard(size_t capacity) : capacity_(std::max<size_t>(1, capacity)) {
    head_ = new Node();
    tail_ = new Node();
    head_->next = tail_;
    tail_->prev = head_;
    sweep_cursor_ = map_.end();
}

Shard::~Shard() {
    Node* curr = head_->next;
    while (curr != tail_) {
        Node* next = curr->next;
        delete curr;
        curr = next;
    }
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

    // Clamp against uint64_t overflow
    uint64_t current = now_ms();
    uint64_t expire_at = 0;
    if (ttl_ms > 0) {
        if (ttl_ms > (UINT64_MAX - current)) {
            expire_at = UINT64_MAX;
        } else {
            expire_at = current + ttl_ms;
        }
    }

    auto it = map_.find(key);
    if (it != map_.end()) {
        it->second->value = std::move(value);
        it->second->expire_at_ms = expire_at;
        move_to_head(it->second);
    } else {
        if (map_.size() >= capacity_) {
            Node* victim = pop_tail();
            if (victim) {
                // If cursor pointed to the evicted node, advance it
                auto victim_it = map_.find(victim->key);
                if (victim_it != map_.end()) {
                    if (sweep_cursor_ == victim_it) {
                        sweep_cursor_ = map_.erase(victim_it);
                    } else {
                        map_.erase(victim_it);
                    }
                }
                delete victim;
            }
        }

        // Allocate node
        Node* node = new Node{std::move(key), std::move(value), expire_at};

        // Insert into map first (exception safety)
        try {
            map_[node->key] = node;
        } catch (...) {
            delete node;
            throw;
        }

        attach_head(node);
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

    if (node->expire_at_ms != 0 && now_ms() >= node->expire_at_ms) {
        detach(node);
        if (sweep_cursor_ == it) {
            sweep_cursor_ = map_.erase(it);
        } else {
            map_.erase(it);
        }
        delete node;
        get_metrics().cache_misses.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    move_to_head(node);
    get_metrics().cache_hits.fetch_add(1, std::memory_order_relaxed);
    return node->value;
}

bool Shard::del(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        Node* node = it->second;
        detach(node);
        if (sweep_cursor_ == it) {
            sweep_cursor_ = map_.erase(it);
        } else {
            map_.erase(it);
        }
        delete node;
        get_metrics().total_deletes.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

// Cursor-based sweep avoids starvation at map_.begin()
size_t Shard::purge_expired(size_t max_keys_to_check) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (map_.empty()) {
        sweep_cursor_ = map_.end();
        return 0;
    }

    size_t keys_purged = 0;
    size_t keys_checked = 0;
    uint64_t current = now_ms();

    while (keys_checked < max_keys_to_check && !map_.empty()) {
        if (sweep_cursor_ == map_.end()) {
            sweep_cursor_ = map_.begin();
        }

        auto it = sweep_cursor_;
        Node* node = it->second;

        if (node->expire_at_ms != 0 && current >= node->expire_at_ms) {
            detach(node);
            sweep_cursor_ = map_.erase(it);
            delete node;
            keys_purged++;
        } else {
            ++sweep_cursor_;
        }
        keys_checked++;
    }

    return keys_purged;
}