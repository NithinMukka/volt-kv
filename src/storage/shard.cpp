#include "storage/shard.hpp"
#include "metrics/telemetry.hpp"
#include <mutex>
#include <algorithm>

Shard::Shard(size_t capacity) : capacity_(std::max<size_t>(1, capacity)) {
    head_ = new Node();
    tail_ = new Node();
    head_->next = tail_;
    tail_->prev = head_;
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
                map_.erase(victim->key);
                delete victim;
            }
        }

        Node* node = new Node{std::move(key), std::move(value), expire_at};
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
        map_.erase(it);
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
        map_.erase(it);
        delete node;
        get_metrics().total_deletes.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

// Bucket-index sweeping: Immune to rehashing and iterator invalidation
size_t Shard::purge_expired(size_t max_keys_to_check) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (map_.empty()) return 0;

    size_t keys_purged = 0;
    size_t keys_checked = 0;
    uint64_t current = now_ms();
    size_t num_buckets = map_.bucket_count();

    if (num_buckets == 0) return 0;

    size_t starting_bucket = sweep_bucket_ % num_buckets;
    size_t current_bucket = starting_bucket;

    do {
        for (auto it = map_.begin(current_bucket); it != map_.end(current_bucket); ) {
            Node* node = it->second;
            if (node->expire_at_ms != 0 && current >= node->expire_at_ms) {
                detach(node);
                std::string k = node->key;
                ++it;
                map_.erase(k);
                delete node;
                keys_purged++;
            } else {
                ++it;
            }

            keys_checked++;
            if (keys_checked >= max_keys_to_check) {
                sweep_bucket_ = current_bucket;
                return keys_purged;
            }
        }

        current_bucket = (current_bucket + 1) % num_buckets;
    } while (current_bucket != starting_bucket && keys_checked < max_keys_to_check);

    sweep_bucket_ = current_bucket;
    return keys_purged;
}

void Shard::dump_entries_unlocked(std::vector<SnapshotEntry>& out) const {
    uint64_t current = now_ms();
    for (const auto& [key, node] : map_) {
        if (node->expire_at_ms == 0 || node->expire_at_ms > current) {
            out.push_back({node->key, node->value, node->expire_at_ms});
        }
    }
}