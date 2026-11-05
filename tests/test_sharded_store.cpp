#include "storage/sharded_store.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

void test_basic_crud() {
    ShardedStore store(16);

    // Write across multiple shards
    for (int i = 0; i < 1000; ++i) {
        store.set("key_" + std::to_string(i), "val_" + std::to_string(i));
    }

    // Verify all keys exist
    for (int i = 0; i < 1000; ++i) {
        auto val = store.get("key_" + std::to_string(i));
        assert(val.has_value());
        assert(val.value() == "val_" + std::to_string(i));
    }

    // Delete check
    assert(store.del("key_42") == true);
    assert(!store.get("key_42").has_value());
    assert(store.del("key_42") == false);

    std::cout << "[PASS] Basic CRUD across 16 shards\n";
}

void test_concurrency() {
    ShardedStore store(16);
    const int num_threads = 16;
    const int ops_per_thread = 2000;
    std::vector<std::thread> threads;

    // 16 concurrent threads reading and writing at the same time
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&store, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                std::string k = "t" + std::to_string(t) + "_k" + std::to_string(i);
                std::string v = "v" + std::to_string(i);
                store.set(std::move(k), std::move(v));
                store.get("t" + std::to_string(t) + "_k" + std::to_string(i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    std::cout << "[PASS] Concurrency (16 threads across 16 shards)\n";
}

int main() {
    test_basic_crud();
    test_concurrency();

    std::cout << "\nALL SHARDED STORE TESTS PASSED!\n";
    return 0;
}