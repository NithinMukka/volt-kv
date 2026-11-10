#pragma once

#include "network/tcp_server.hpp"
#include "network/connection.hpp"
#include "storage/sharded_store.hpp"
#include <sys/epoll.h>
#include <unordered_map>
#include <atomic>

class EventLoop {
public:
    explicit EventLoop(TcpServer& server, ShardedStore& store);
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
    int epoll_fd_{-1};
    std::atomic<bool> running_{false};
    std::unordered_map<int, Connection> connections_;

    static constexpr int MAX_EVENTS = 1024;
    static constexpr size_t MAX_READ_BUFFER = 64 * 1024 * 1024; // 64 MB hard buffer ceiling
    struct epoll_event events_[MAX_EVENTS];
};