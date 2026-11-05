#include "storage/shard.hpp"
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

void Shard::set(std::string key, std::string value){
    std::unique_lock<std::shared_mutex> unique_lock(mutex_);
    auto it = map_.find(key);
    if(it != map_.end()){
        it->second->value = std::move(value);
        move_to_head(it->second);
    }
    else{
        if (map_.size() >= capacity_) {
            Node* victim = pop_tail();
            if (victim) {
                map_.erase(victim->key);
                delete victim;
            }
        }
        Node* node = new Node{std::move(key), std::move(value)};
        attach_head(node);
        map_[node->key] = node;
    }
}

std::optional<std::string> Shard::get(std::string_view key){
    std::unique_lock<std::shared_mutex> unique_lock(mutex_);
    auto it = map_.find(key);
    if(it != map_.end()){
        move_to_head(it->second);
        return it->second->value;
    }
    return std::nullopt;
}

bool Shard::del(std::string_view key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        Node* node = it->second;
        detach(node);
        map_.erase(it);
        delete node;
        return true;
    }
    return false;
}