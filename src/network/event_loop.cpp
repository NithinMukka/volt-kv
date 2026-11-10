#include "network/event_loop.hpp"
#include <sys/socket.h>
#include <netinet/in.h>   
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cerrno>

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

EventLoop::EventLoop(TcpServer& server) : server_(server) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        std::cerr << "Failed to create epoll instance\n";
    }
}

EventLoop::~EventLoop() {
    stop();
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

void EventLoop::add_to_epoll(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
}

void EventLoop::remove_from_epoll(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections_.erase(fd);
}

void EventLoop::handle_accept() {
    // In Edge-Triggered mode, multiple clients may be waiting in the accept queue.
    // Loop accept() until EAGAIN to accept all queued connections.
    while (true) {
        int client_fd = accept(server_.get_fd(), nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // All pending incoming connections accepted
            }
            break;
        }

        set_nonblocking(client_fd);

        // TCP_NODELAY: disable Nagle's algorithm for low-latency request-response
        int flag = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        // Register for Edge-Triggered reading
        add_to_epoll(client_fd, EPOLLIN | EPOLLET);

        connections_[client_fd] = Connection{client_fd, "", ""};
    }
}

void EventLoop::stop() {
    running_.store(false);
}

void EventLoop::handle_read(int client_fd) {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) return;

    Connection& conn = it->second;
    char buffer[4096];

    while (true) {
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

        if (bytes_read > 0) {
            conn.read_buffer.append(buffer, bytes_read);
        } else if (bytes_read == 0) {
            remove_from_epoll(client_fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // Socket drained
            }
            remove_from_epoll(client_fd);
            return;
        }
    }

    // Process all complete lines ending with '\n'
    size_t newline_pos;
    while ((newline_pos = conn.read_buffer.find('\n')) != std::string::npos) {
        std::string line = conn.read_buffer.substr(0, newline_pos + 1);
        conn.read_buffer.erase(0, newline_pos + 1);

        // Echo response: "+PONG: " + line
        std::string response = "+PONG: " + line;
        write(client_fd, response.data(), response.size());
    }
}

void EventLoop::run() {
    // 1. Register the listening server socket with epoll
    add_to_epoll(server_.get_fd(), EPOLLIN | EPOLLET);
    running_.store(true);

    std::cout << "[EventLoop] Started polling on epoll fd: " << epoll_fd_ << "\n";

    while (running_.load()) {
        int n_fds = epoll_wait(epoll_fd_, events_, MAX_EVENTS, 100 /* timeout ms */);
        if (n_fds < 0) {
            if (errno == EINTR) continue; // Interrupted by system signal, retry
            break;
        }

        for (int i = 0; i < n_fds; ++i) {
            int fd = events_[i].data.fd;
            uint32_t ev = events_[i].events;

            if (fd == server_.get_fd()) {
                handle_accept();
            } else {
                if (ev & (EPOLLERR | EPOLLHUP)) {
                    remove_from_epoll(fd);
                } else if (ev & EPOLLIN) {
                    handle_read(fd);
                }
            }
        }
    }
}