#include "network/tcp_server.hpp"
#include "network/event_loop.hpp"
#include "storage/sharded_store.hpp"
#include "storage/ttl_cleaner.hpp"
#include <iostream>
#include <csignal>
#include <atomic>

static std::atomic<EventLoop*> g_loop{nullptr};

void signal_handler(int) {
    if (g_loop.load()) {
        g_loop.load()->stop();
    }
}

int main() {
    // 1. Storage and cleaner
    ShardedStore store(16);
    TTLCleaner cleaner(store);
    cleaner.start();

    // 2. Server and Event loop
    TcpServer server(8080);
    if (!server.start()) {
        std::cerr << "Failed to start TCP server\n";
        return 1;
    }

    EventLoop loop(server, store);
    g_loop.store(&loop);

    // 3. Register signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[VoltKV] Production Engine running on port 8080. Press Ctrl+C to stop.\n";
    loop.run();

    std::cout << "\n[VoltKV] Shutting down cleanly...\n";
    cleaner.stop();
    std::cout << "[VoltKV] Shutdown complete.\n";

    return 0;
}