# VoltKV

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%2F%20WSL2-orange.svg)](https://ubuntu.com/)
[![Build System](https://img.shields.io/badge/Build-CMake%20%7C%20Ninja-success.svg)](https://ninja-build.org/)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

**VoltKV** is a high-throughput, low-latency, in-memory key-value and caching engine written in Modern C++ (C++20) for Linux environments. 

It is engineered to maximize multi-core server utilization by pairing an edge-triggered non-blocking event loop (`epoll`) with fine-grained lock striping (sharding), $O(1)$ Least Recently Used (LRU) memory eviction, hybrid Time-To-Live (TTL) expiration, and Write-Ahead Logging (WAL) for crash durability.

---

## Architecture Overview

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
│  │ 1. Network Subsystem: Non-Blocking Event Loop (Linux epoll)                            │  │
│  │    • epoll_create1, epoll_ctl, epoll_wait (Edge-Triggered / EPOLLET)                   │  │
│  │    • Per-connection state buffers, TCP_NODELAY, O_NONBLOCK                             │  │
│  └───────────────────────────────────────┬────────────────────────────────────────────────┘  │
│                                          │ Byte Streams                                      │
│                                          ▼                                                   │
│  ┌────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │ 2. Protocol Parser & Request Dispatcher                                                │  │
│  │    • Zero-copy string view parser (`GET`, `SET <k> <v> [EX <sec>]`, `DEL`, `STATS`)    │  │
│  │    • Token Bucket Rate Limiter (IP-based throttling)                                   │  │
│  └───────────────────────────────────────┬────────────────────────────────────────────────┘  │
│                                          │ Structured Command                                │
│                                          ▼                                                   │
│  ┌────────────────────────────────────────────────────────────────────────────────────────┐  │
│  │ 3. Sharded Storage Engine (Lock Striping Layer)                                        │  │
│  │    Shard Index = std::hash<std::string>{}(key) & (NUM_SHARDS - 1)                      │  │
│  │                                                                                        │  │
│  │  ┌─────────────────────────┐ ┌─────────────────────────┐     ┌───────────────────────┐  │  │
│  │  │ Shard 0                 │ │ Shard 1                 │     │ Shard N               │  │  │
│  │  │ • std::shared_mutex     │ │ • std::shared_mutex     │     │ • std::shared_mutex   │  │  │
│  │  │ • Hash Index (Map)      │ │ • Hash Index (Map)      │ ... │ • Hash Index (Map)    │  │  │
│  │  │ • LRU Doubly Linked List│ │ • LRU Doubly Linked List│     │ • LRU Doubly List     │  │  │
│  │  └─────────────────────────┘ └─────────────────────────┘     └───────────────────────┘  │  │
│  └──────────────────┬───────────────────────────────────────────────────┬─────────────────┘  │
│                     │                                                   │                    │
│                     ▼                                                   ▼                    │
│  ┌──────────────────────────────────────┐     ┌───────────────────────────────────────────┐  │
│  │ 4. Background TTL Eviction Cleaner   │     │ 5. Durability Layer: Write-Ahead Log (WAL)│  │
│  │    • std::condition_variable worker  │     │    • Sequential Append-Only Disk Writes   │  │
│  │    • Active sampling & purge sweeps  │     │    • CRC32 Data Integrity Checksums       │  │
│  │    • Passive lazy eviction on reads  │     │    • Crash Replay & Truncation Engine     │  │
│  └──────────────────────────────────────┘     └───────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────────────────────────┘