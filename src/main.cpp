#include "network/tcp_server.hpp"
#include "network/event_loop.hpp"
#include "storage/sharded_store.hpp"
#include "storage/ttl_cleaner.hpp"
#include "persistence/wal_manager.hpp"
#include "persistence/wal_recovery.hpp"
#include "persistence/snapshot.hpp"
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
    const std::string snap_path = "voltkv.snap";
    const std::string wal_path = "voltkv.wal";

    ShardedStore store(16);

    // 1. Restore baseline state from Snapshot (if exists)
    SnapshotStats snap_stats;
    std::cout << "[VoltKV] Checking for snapshot at " << snap_path << "...\n";
    if (SnapshotManager::load_snapshot(snap_path, store, snap_stats) && snap_stats.keys_saved > 0) {
        std::cout << "[VoltKV] Loaded snapshot: " << snap_stats.keys_saved << " keys restored.\n";
    }

    // 2. Replay subsequent mutating transactions from WAL
    RecoveryStats wal_stats;
    std::cout << "[VoltKV] Replaying WAL from " << wal_path << "...\n";
    if (WalRecovery::recover(wal_path, store, wal_stats)) {
        std::cout << "[VoltKV] WAL replay complete: " << wal_stats.records_replayed 
                  << " records restored (" << wal_stats.set_ops << " sets, " 
                  << wal_stats.del_ops << " dels).\n";
        if (wal_stats.torn_tail_detected) {
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