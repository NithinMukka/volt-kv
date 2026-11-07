#include "storage/ttl_cleaner.hpp"

TTLCleaner::TTLCleaner(ShardedStore& store, std::chrono::milliseconds interval)
    : store_(store), interval_(interval) {}

TTLCleaner::~TTLCleaner() {
    stop();
}

void TTLCleaner::start() {
    worker_thread_ = std::thread(&TTLCleaner::run, this);
}

void TTLCleaner::stop() {
    {
        std::lock_guard<std::mutex> lock(cv_mutex_);
        if (stop_flag_) return; // already stopped
        stop_flag_ = true;
    }
    cv_.notify_all(); // Instantly wakes up the worker thread

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void TTLCleaner::run() {
    std::unique_lock<std::mutex> lock(cv_mutex_);
    while (!stop_flag_) {
        // Sleep until interval expires OR until stop() notifies us
        cv_.wait_for(lock, interval_, [this]() { return stop_flag_; });

        if (stop_flag_) break;

        // Unlock during the purge so we don't hold cv_mutex_ unnecessarily
        lock.unlock();
        store_.purge_all_expired();
        lock.lock();
    }
}