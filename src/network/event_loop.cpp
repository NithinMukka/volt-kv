#include "network/event_loop.hpp"
#include "protocol/parser.hpp"
#include "metrics/telemetry.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <cerrno>

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Notice the colon ':' initialization list. References MUST be initialized here.
EventLoop::EventLoop(TcpServer& server, ShardedStore& store, WalManager& wal, RateLimiter& limiter) 
    : server_(server), store_(store), wal_(wal), limiter_(limiter) {
    signal(SIGPIPE, SIG_IGN);
    epoll_fd_ = epoll_create1(0);
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

void EventLoop::modify_epoll(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::remove_from_epoll(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections_.erase(fd);
}

void EventLoop::handle_accept() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_.get_fd(), reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        std::string client_ip(ip_str);

        set_nonblocking(client_fd);

        int flag = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        add_to_epoll(client_fd, EPOLLIN | EPOLLET);
        connections_[client_fd] = Connection{client_fd, std::move(client_ip), "", ""};
    }
}

void EventLoop::handle_read(int client_fd) {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) return;

    Connection& conn = it->second;
    char buffer[16384];

    while (true) {
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

        if (bytes_read > 0) {
            conn.read_buffer.append(buffer, bytes_read);

            if (conn.read_buffer.size() > MAX_READ_BUFFER) {
                remove_from_epoll(client_fd);
                return;
            }
        } else if (bytes_read == 0) {
            remove_from_epoll(client_fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            remove_from_epoll(client_fd);
            return;
        }
    }

    process_pipeline(conn);
}

void EventLoop::process_pipeline(Connection& conn) {
    size_t newline_pos;
    size_t commands_processed = 0;
    constexpr size_t MAX_BATCH_COMMANDS = 16;

    while (commands_processed < MAX_BATCH_COMMANDS && 
          (newline_pos = conn.read_buffer.find('\n')) != std::string::npos) {
        
        std::string raw_line = conn.read_buffer.substr(0, newline_pos + 1);
        conn.read_buffer.erase(0, newline_pos + 1);
        commands_processed++;

        // 1. Check Rate Limit (inside the loop so continue works)
        if (!limiter_.allow(conn.client_ip)) {
            conn.write_buffer.append("-ERR rate limit exceeded\r\n");
            continue;
        }

        // 2. Parse command
        Command cmd = ProtocolParser::parse(raw_line);
        std::string reply;

        switch (cmd.type) {
            case CommandType::SET: {
                uint64_t expire_at = (cmd.ttl_ms > 0) ? (now_ms() + cmd.ttl_ms) : 0;
                
                if (!wal_.append_set(cmd.key, cmd.value, expire_at)) {
                    reply = "-ERR WAL disk write failed\r\n";
                    break;
                }
                
                store_.set(std::move(cmd.key), std::move(cmd.value), cmd.ttl_ms);
                reply = "+OK\r\n";
                break;
            }

            case CommandType::GET: {
                auto val = store_.get(cmd.key);
                if (val.has_value()) {
                    reply = "$" + std::to_string(val.value().size()) + "\r\n" + val.value() + "\r\n";
                } else {
                    reply = "$-1\r\n";
                }
                break;
            }

            case CommandType::DEL: {
                if (!wal_.append_del(cmd.key)) {
                    reply = "-ERR WAL disk write failed\r\n";
                    break;
                }

                bool deleted = store_.del(cmd.key);
                reply = deleted ? ":1\r\n" : ":0\r\n";
                break;
            }

            case CommandType::STATS: {
                auto& m = get_metrics();
                reply = "reads:" + std::to_string(m.total_reads.load()) +
                        " writes:" + std::to_string(m.total_writes.load()) +
                        " hits:" + std::to_string(m.cache_hits.load()) +
                        " misses:" + std::to_string(m.cache_misses.load()) + "\r\n";
                break;
            }

            case CommandType::UNKNOWN:
            default:
                reply = "-" + cmd.error_message + "\r\n";
                break;
        }

        conn.write_buffer.append(reply);
    }

    if (!conn.write_buffer.empty()) {
        handle_write(conn.fd);
    }
}

void EventLoop::handle_write(int client_fd) {
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) return;

    Connection& conn = it->second;

    while (!conn.write_buffer.empty()) {
        ssize_t bytes_sent = send(client_fd, conn.write_buffer.data(), conn.write_buffer.size(), MSG_NOSIGNAL);

        if (bytes_sent > 0) {
            conn.write_buffer.erase(0, bytes_sent);
        } else if (bytes_sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                modify_epoll(client_fd, EPOLLIN | EPOLLOUT | EPOLLET);
                return;
            }
            remove_from_epoll(client_fd);
            return;
        }
    }

    modify_epoll(client_fd, EPOLLIN | EPOLLET);
}

void EventLoop::run() {
    add_to_epoll(server_.get_fd(), EPOLLIN | EPOLLET);
    running_.store(true);

    while (running_.load()) {
        int n_fds = epoll_wait(epoll_fd_, events_, MAX_EVENTS, 100);
        if (n_fds < 0) {
            if (errno == EINTR) continue;
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
                } else {
                    if (ev & EPOLLIN) {
                        handle_read(fd);
                    }
                    if (ev & EPOLLOUT) {
                        handle_write(fd);
                    }
                }
            }
        }
    }
}

void EventLoop::stop() {
    running_.store(false);
}