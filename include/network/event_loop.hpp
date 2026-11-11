#pragma once

#include "network/tcp_server.hpp"
#include "network/connection.hpp"
#include "storage/sharded_store.hpp"
#include "persistence/wal_manager.hpp"
#include <sys/epoll.h>
#include <unordered_map>
#include <atomic>
#include "protocol/rate_limiter.hpp"

class EventLoop {
public:
    explicit EventLoop(TcpServer& server, ShardedStore& store, WalManager& wal, RateLimiter& limiter);
    ~EventLoop();

    void run();
    void stop();

private:
    void add_to_epoll(int fd, uint32_t events);
    void modify_epoll(int fd, uint32_t events);
    void remove_from_epoll(int fd);

    void handle_accept();
    void handle_read(int client_fd);
    void handle_write(int client_fd);
    void process_pipeline(Connection& conn);

    TcpServer& server_;
    ShardedStore& store_;
    WalManager& wal_;
    RateLimiter& limiter_;
    int epoll_fd_{-1};
    std::atomic<bool> running_{false};
    std::unordered_map<int, Connection> connections_;

    static constexpr int MAX_EVENTS = 1024;
    static constexpr size_t MAX_READ_BUFFER = 64 * 1024 * 1024;
    struct epoll_event events_[MAX_EVENTS];
};