#include "storage/shard.hpp"
#include <cassert>
#include <iostream>

void test_lru_eviction() {
    // Capacity = 3
    Shard shard(3);

    shard.set("k1", "v1");
    shard.set("k2", "v2");
    shard.set("k3", "v3");

    // Cache is full: [k3 (MRU), k2, k1 (LRU)]
    // Inserting "k4" must evict "k1" (oldest)
    shard.set("k4", "v4");

    assert(!shard.get("k1").has_value()); // k1 was evicted!
    assert(shard.get("k2").has_value());
    assert(shard.get("k3").has_value());
    assert(shard.get("k4").has_value());

    std::cout << "[PASS] Basic LRU Eviction\n";
}

void test_lru_promotion() {
    // Capacity = 3
    Shard shard(3);

    shard.set("k1", "v1");
    shard.set("k2", "v2");
    shard.set("k3", "v3");

    // Access "k1" via get() -> promotes "k1" to MRU
    // Order now: [k1 (MRU), k3, k2 (LRU)]
    auto res = shard.get("k1");
    assert(res.has_value());

    // Inserting "k4" should now evict "k2" (NOT "k1")
    shard.set("k4", "v4");

    assert(shard.get("k1").has_value()); // k1 survived!
    assert(!shard.get("k2").has_value()); // k2 was evicted!
    assert(shard.get("k3").has_value());
    assert(shard.get("k4").has_value());

    std::cout << "[PASS] LRU Access Promotion\n";
}

int main() {
    test_lru_eviction();
    test_lru_promotion();

    std::cout << "\nALL LRU TESTS PASSED!\n";
    return 0;
}