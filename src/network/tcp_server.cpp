#include "network/tcp_server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

TcpServer::TcpServer(uint16_t port) : port_(port) {}

TcpServer::~TcpServer() {
    if (server_fd_ >= 0) {
        close(server_fd_);
    }
}

bool TcpServer::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool TcpServer::start() {
    // 1. Create IPv4 TCP socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create socket\n";
        return false;
    }

    // 2. SO_REUSEADDR: Allows immediate restart without "Address already in use" error
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Make listener non-blocking
    if (!set_nonblocking(server_fd_)) {
        std::cerr << "Failed to set non-blocking on listener\n";
        return false;
    }

    // 4. Bind to 0.0.0.0:port
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_); // htons converts host byte order to network byte order

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port " << port_ << "\n";
        return false;
    }

    // 5. Listen with max backlog queue
    if (listen(server_fd_, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on socket\n";
        return false;
    }

    std::cout << "[TcpServer] Listening on port " << port_ << " (fd: " << server_fd_ << ")\n";
    return true;
}