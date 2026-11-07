#pragma once

#include "storage/sharded_store.hpp"
#include <thread>
#include <atomic>
#include <chrono>

class TTLCleaner {
public:
    explicit TTLCleaner(ShardedStore& store);
    ~TTLCleaner();

    void start();
    void stop();

private:
    void run();

    ShardedStore& store_;
    std::thread worker_thread_;
    std::atomic<bool> stop_flag_{false};
};

