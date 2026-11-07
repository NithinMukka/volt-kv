#include "storage/ttl_cleaner.hpp"

TTLCleaner::TTLCleaner(ShardedStore& store) : store_(store) {}

TTLCleaner::~TTLCleaner() {
    stop();
}

void TTLCleaner::start() {
    worker_thread_ = std::thread(&TTLCleaner::run, this);
}

void TTLCleaner::stop() {
    stop_flag_.store(true);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void TTLCleaner::run() {
    while (!stop_flag_.load()) {
        store_.purge_all_expired(); // We need to add this method to ShardedStore
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}