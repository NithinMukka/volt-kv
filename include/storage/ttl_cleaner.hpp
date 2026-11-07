#pragma once

#include "storage/sharded_store.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

class TTLCleaner {
public:
    explicit TTLCleaner(ShardedStore& store, std::chrono::milliseconds interval = std::chrono::milliseconds(100));
    ~TTLCleaner();

    void start();
    void stop();

private:
    void run();

    ShardedStore& store_;
    std::chrono::milliseconds interval_;
    std::thread worker_thread_;

    std::mutex cv_mutex_;
    std::condition_variable cv_;
    bool stop_flag_{false};
};