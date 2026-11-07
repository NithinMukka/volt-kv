#pragma once

#include "network/tcp_server.hpp"
#include "network/connection.hpp"
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>
#include <atomic>

class EventLoop {
public:
    explicit EventLoop(TcpServer& server);
    ~EventLoop();

    void run();
    void stop();

private:
    void add_to_epoll(int fd, uint32_t events);
    void remove_from_epoll(int fd);

    void handle_accept();
    void handle_read(int client_fd);
    void handle_write(int client_fd);

    TcpServer& server_;
    int epoll_fd_{-1};
    std::atomic<bool> running_{false};
    std::unordered_map<int, Connection> connections_;

    static constexpr int MAX_EVENTS = 1024;
    struct epoll_event events_[MAX_EVENTS];
};