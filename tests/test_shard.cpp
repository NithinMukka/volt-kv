#include "storage/shard.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

void test_basic_crud() {
    Shard shard;

    // SET and GET
    shard.set("key1", "val1");
    auto res = shard.get("key1");
    assert(res.has_value());
    assert(res.value() == "val1");

    // Overwrite existing key
    shard.set("key1", "val2");
    assert(shard.get("key1").value() == "val2");

    // Non-existent key
    assert(!shard.get("missing_key").has_value());

    // DEL
    assert(shard.del("key1") == true);
    assert(!shard.get("key1").has_value());
    assert(shard.del("key1") == false); // Second delete fails

    std::cout << "[PASS] Basic CRUD\n";
}

void test_move_semantics() {
    Shard shard;
    std::string key = "user_100";
    std::string val = "some_large_heap_string_payload";

    shard.set(std::move(key), std::move(val));

    // Verify map stored the value
    assert(shard.get("user_100").value() == "some_large_heap_string_payload");

    // Verify caller variables were drained
    assert(val.empty());

    std::cout << "[PASS] Move Semantics\n";
}

void test_concurrency() {
    Shard shard;
    const int num_threads = 8;
    const int ops_per_thread = 1000;
    std::vector<std::thread> threads;

    // 4 Writer threads
    for (int t = 0; t < num_threads / 2; ++t) {
        threads.emplace_back([&shard, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                shard.set("k" + std::to_string(i), "v" + std::to_string(t));
            }
        });
    }

    // 4 Reader threads
    for (int t = 0; t < num_threads / 2; ++t) {
        threads.emplace_back([&shard, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                shard.get("k" + std::to_string(i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    std::cout << "[PASS] Concurrency\n";
}

int main() {
    test_basic_crud();
    test_move_semantics();
    test_concurrency();

    std::cout << "\nALL SHARD TESTS PASSED!\n";
    return 0;
}