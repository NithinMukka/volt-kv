#pragma once

#include "storage/sharded_store.hpp"
#include "persistence/wal_manager.hpp"
#include <string>
#include <cstdint>

struct SnapshotStats {
    uint64_t keys_saved{0};
    uint64_t bytes_written{0};
    bool success{false};
};

class SnapshotManager {
public:
    static constexpr uint32_t SNAP_MAGIC = 0x564B5350; // 'V' 'K' 'S' 'P'
    static constexpr uint16_t SNAP_VERSION = 1;

    // Atomically dumps in-memory shards to disk, then truncates WAL to 0
    static bool create_snapshot(
        const std::string& snap_filepath,
        ShardedStore& store,
        WalManager& wal,
        SnapshotStats& stats
    );

    // Restores store state from snapshot file on startup
    static bool load_snapshot(
        const std::string& snap_filepath,
        ShardedStore& store,
        SnapshotStats& stats
    );
};