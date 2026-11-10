#pragma once

#include "storage/sharded_store.hpp"
#include <string>
#include <cstdint>

struct RecoveryStats {
    uint64_t records_replayed{0};
    uint64_t set_ops{0};
    uint64_t del_ops{0};
    bool torn_tail_detected{false};
};

class WalRecovery {
public:
    // Reads WAL file, validates CRC32, and replays state into store
    static bool recover(const std::string& wal_filepath, ShardedStore& store, RecoveryStats& stats);
};