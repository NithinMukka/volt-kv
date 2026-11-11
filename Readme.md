# VoltKV: High-Throughput In-Memory Key-Value & Caching Engine

VoltKV is an ultra-low-latency, in-memory key-value and caching engine written in Modern C++20 for Linux environments. Built from scratch with failure-first systems principles, it features non-blocking event-driven network I/O (`epoll`), lock-striped sharded storage, $O(1)$ LRU eviction with monotonic TTL, a token-bucket rate limiter, and a crash-consistent binary Write-Ahead Log (WAL) with atomic point-in-time snapshotting.

---

## Benchmark Highlights (Verified with `wrk`)

Benchmarked under **100 concurrent TCP connections** across a sustained **30-second saturation test**:

```text
Running 30s test @ http://127.0.0.1:8080/keys/test
  4 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   649.58us  171.06us   9.41ms   85.11%
    Req/Sec    38.60k     4.63k   55.29k    66.42%
  Latency Distribution
     50%  640.00us
     75%  723.00us
     90%  803.00us
     99%    0.98ms
  4,607,569 requests in 30.00s, 465.78MB read
Requests/sec: 153571.36
Transfer/sec:     15.52MB
```

* **Throughput:** **153,571 requests/second** sustained.
* **p99 Tail Latency:** **0.98 ms** (sub-millisecond tail latency).
* **p50 Latency (Median):** **640 microseconds**.
* **Reliability:** **0 socket errors, 0 timeouts** over 4.6 million requests.

---

## System Architecture

```text
                      ┌────────────────────────────────────────────────────────┐
                      │              Clients (HTTP / REST / TCP)               │
                      └───────────────────────────┬────────────────────────────┘
                                                  │
                                                  ▼
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│  VoltKV Engine                                                                               │
│                                                                                              │
│  ┌────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │ 1. Network Subsystem: Non-Blocking Event Loop (Linux epoll - EPOLLET)                  │  │
│  │    • epoll_create1, epoll_ctl, epoll_wait (Edge-Triggered, O_NONBLOCK, TCP_NODELAY)    │  │
│  │    • Connection write-buffering with EPOLLOUT handling (prevents data truncation)      │  │
│  │    • Slowloris defense: 64MB hard connection buffer ceiling                            │  │
│  └───────────────────────────────────────┬────────────────────────────────────────────────┘  │
│                                          │ Byte Streams                                      │
│                                          ▼                                                   │
│  ┌────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │ 2. Dual-Mode Protocol Parser & Request Dispatcher                                      │  │
│  │    • Zero-copy std::string_view tokenization (SET, GET, DEL, STATS)                    │  │
│  │    • Dual mode: HTTP/1.1 REST paths (/keys/:id) + RESP/ASCII raw inline               │  │
│  │    • Token Bucket Rate Limiter: 16-shard lock-striped IP throttling with whitelist    │  │
│  └───────────────────────────────────────┬────────────────────────────────────────────────┘  │
│                                          │ Structured Command                                │
│                                          ▼                                                   │
│  ┌────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │ 3. Sharded Storage Engine (Lock Striping)                                              │  │
│  │    Shard Index = std::hash<std::string_view>{}(key) % NUM_SHARDS (Configurable, 16)   │  │
│  │                                                                                        │  │
│  │  ┌─────────────────────────┐ ┌─────────────────────────┐     ┌───────────────────────┐  │  │
│  │  │ Shard 0                 │ │ Shard 1                 │     │ Shard N               │  │  │
│  │  │ • std::shared_mutex     │ │ • std::shared_mutex     │ ... │ • std::shared_mutex   │  │  │
│  │  │ • Transparent Hash Map  │ │ • Transparent Hash Map  │     │ • Transparent Hash Map│  │  │
│  │  │ • LRU Doubly Linked List│ │ • LRU Doubly Linked List│     │ • LRU Doubly List     │  │  │
│  │  └─────────────────────────┘ └─────────────────────────┘     └───────────────────────┘  │  │
│  └──────────────────┬───────────────────────────────────────────────────┬─────────────────┘  │
│                     │                                                   │                    │
│                     ▼                                                   ▼                    │
│  ┌──────────────────────────────────────┐     ┌───────────────────────────────────────────┐  │
│  │ 4. Eviction Engine (LRU + TTL)       │     │ 5. Durability Layer: Binary WAL & Snap    │  │
│  │    • O(1) Doubly Linked List eviction│     │    • Append-only binary log with CRC32    │  │
│  │    • Monotonic steady_clock expiry   │     │    • Sequential log replay & ftruncate()  │  │
│  │    • Active sweeper on condition_var │     │    • Two-Phase atomic snapshot (rename()) │  │
│  └──────────────────────────────────────┘     └───────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Core Engineering & Failure-First Invariants

1. **Zero-Copy Memory Semantics:**
   * Utilizes C++ rvalue references (`std::move`) on mutations to transfer heap pointers directly into storage without byte copies.
   * Leverages C++20 heterogeneous/transparent hashing (`is_transparent`) to query hash tables using `std::string_view` without temporary `std::string` heap allocations.
2. **Durability Invariant:**
   * A mutating operation (`SET`, `DEL`) is written and flushed to the binary WAL on disk **before** modifying in-memory shards.
   * If disk write fails (`ENOSPC`), the memory mutation is aborted and the client receives an error.
3. **Crash Consistency & Torn-Write Truncation:**
   * Every WAL record encodes an IEEE 802.3 CRC32 checksum.
   * On startup, the recovery engine detects partial/torn records resulting from sudden power loss and physically truncates (`truncate()`) corrupted bytes to keep the log clean.
4. **Two-Phase Atomic Snapshotting:**
   * Takes a point-in-time capture across all 16 shards locked in strict ascending order (eliminating deadlocks).
   * Streams data to `.snap.tmp` and executes an atomic POSIX `rename()`, ensuring the active snapshot is never corrupted if a crash occurs mid-dump. WAL is safely truncated to 0 upon completion.
5. **Network Resilience & DoS Protection:**
   * Linux `epoll` in Edge-Triggered (`EPOLLET`) mode with non-blocking sockets.
   * Handles partial TCP writes via connection-level write buffers with dynamic `EPOLLOUT` polling.
   * `SIGPIPE` safely ignored to prevent server termination on abrupt client disconnects.
   * Sharded Token-Bucket Rate Limiter enforces burst caps and refill rates per client IP.

---

## Repository Structure

```text
volt-kv/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── metrics/
│   │   └── telemetry.hpp
│   ├── network/
│   │   ├── connection.hpp
│   │   ├── event_loop.hpp
│   │   └── tcp_server.hpp
│   ├── persistence/
│   │   ├── crc32.hpp
│   │   ├── snapshot.hpp
│   │   ├── wal_manager.hpp
│   │   └── wal_recovery.hpp
│   ├── protocol/
│   │   ├── command.hpp
│   │   ├── parser.hpp
│   │   └── rate_limiter.hpp
│   └── storage/
│       ├── shard.hpp
│       ├── sharded_store.hpp
│       └── ttl_cleaner.hpp
├── src/
│   ├── main.cpp
│   ├── network/
│   │   ├── event_loop.cpp
│   │   └── tcp_server.cpp
│   ├── persistence/
│   │   ├── snapshot.cpp
│   │   ├── wal_manager.cpp
│   │   └── wal_recovery.cpp
│   ├── protocol/
│   │   ├── parser.cpp
│   │   └── rate_limiter.cpp
│   └── storage/
│       ├── shard.cpp
│       ├── sharded_store.cpp
│       └── ttl_cleaner.cpp
└── tests/
    ├── CMakeLists.txt
    ├── test_chaos_durability.cpp
    ├── test_concurrent_store.cpp
    ├── test_crc32.cpp
    ├── test_e2e_integration.cpp
    ├── test_parser.cpp
    ├── test_rate_limiter.cpp
    ├── test_snapshot.cpp
    ├── test_ttl_cleaner.cpp
    ├── test_wal.cpp
    └── test_wal_recovery.cpp
```

---

## Build & Test Suite

### Prerequisites
* Linux / WSL2 (Ubuntu 22.04+)
* `g++` supporting C++20
* `cmake` (version 3.20+)
* `valgrind` and `wrk`

### 1. Build and Run Full Unit & Chaos Test Suite
```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### 2. ThreadSanitizer Concurrency Audit
```bash
cmake -B build_tsan -DENABLE_TSAN=ON -DBUILD_TESTING=ON
cmake --build build_tsan
ctest --test-dir build_tsan --output-on-failure
```

### 3. AddressSanitizer Memory Safety Audit
```bash
cmake -B build_asan -DENABLE_ASAN=ON -DENABLE_TSAN=OFF -DBUILD_TESTING=ON
cmake --build build_asan
ctest --test-dir build_asan --output-on-failure
```

### 4. Valgrind Memory Leak Audit
```bash
cmake -B build -DENABLE_TSAN=OFF -DENABLE_ASAN=OFF -DBUILD_TESTING=ON
cmake --build build
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./build/tests/voltkv_tests
```
*(Verified: **0 errors from 0 contexts, 0 bytes leaked** across 111,000+ allocations)*.

### 5. Production Release Build & Server Execution
```bash
cmake -B build_release -DCMAKE_BUILD_TYPE=Release -DENABLE_TSAN=OFF -DENABLE_ASAN=OFF -DBUILD_TESTING=OFF
cmake --build build_release
./build_release/voltkv_server
```

---

## Wire Protocol Examples

### RESP / Inline ASCII Mode (via `nc` / TCP Socket)
```text
SET user_1 Alice EX 60\r\n     ->  +OK\r\n
GET user_1\r\n                 ->  $5\r\nAlice\r\n
DEL user_1\r\n                 ->  :1\r\n
STATS\r\n                      ->  reads:1 writes:1 hits:1 misses:0\r\n
```

### HTTP / REST Mode (via `curl` / `wrk`)
```bash
curl -i http://127.0.0.1:8080/keys/user_1
```
```text
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: 5
Connection: keep-alive

Alice
```