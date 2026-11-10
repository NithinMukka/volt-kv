#include "network/tcp_server.hpp"
#include "network/event_loop.hpp"
#include "storage/sharded_store.hpp"
#include "storage/ttl_cleaner.hpp"
#include "persistence/wal_manager.hpp"
#include "persistence/wal_recovery.hpp"
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
    const std::string wal_path = "voltkv.wal";

    // 1. Initialize in-memory storage
    ShardedStore store(16);

    // 2. Crash Recovery: Reconstruct state from WAL before accepting connections
    RecoveryStats stats;
    std::cout << "[VoltKV] Replaying WAL from " << wal_path << "...\n";
    if (WalRecovery::recover(wal_path, store, stats)) {
        std::cout << "[VoltKV] Replay complete: " << stats.records_replayed 
                  << " records restored (" << stats.set_ops << " sets, " 
                  << stats.del_ops << " dels).\n";
        if (stats.torn_tail_detected) {
            std::cout << "[VoltKV] Warning: Corrupted tail detected and truncated.\n";
        }
    }

    // 3. Open WAL for live writes
    WalManager wal(wal_path, FsyncPolicy::ALWAYS);
    if (!wal.open()) {
        std::cerr << "[VoltKV] Fatal: Failed to open WAL file for writing.\n";
        return 1;
    }

    // 4. Background TTL cleaner
    TTLCleaner cleaner(store);
    cleaner.start();

    // 5. Network server
    TcpServer server(8080);
    if (!server.start()) {
        std::cerr << "[VoltKV] Fatal: Failed to start TCP server on port 8080\n";
        return 1;
    }

    // 6. Event loop
    EventLoop loop(server, store, wal);
    g_loop.store(&loop);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[VoltKV] Engine ready on port 8080. Press Ctrl+C to stop.\n";
    loop.run();

    std::cout << "\n[VoltKV] Shutting down cleanly...\n";
    cleaner.stop();
    wal.close();
    std::cout << "[VoltKV] Shutdown complete.\n";

    return 0;
}