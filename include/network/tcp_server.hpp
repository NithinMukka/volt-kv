#pragma once

#include <cstdint>

class TcpServer {
public:
    explicit TcpServer(uint16_t port);
    ~TcpServer();

    // Prevent copies
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // Sets up socket, sets SO_REUSEADDR, non-blocking, bind, listen
    bool start();

    int get_fd() const { return server_fd_; }
    uint16_t get_port() const { return port_; }

private:
    uint16_t port_;
    int server_fd_{-1};

    bool set_nonblocking(int fd);
};