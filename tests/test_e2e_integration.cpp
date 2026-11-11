#include <gtest/gtest.h>
#include "network/tcp_server.hpp"
#include "network/event_loop.hpp"
#include "storage/sharded_store.hpp"
#include "persistence/wal_manager.hpp"
#include "protocol/rate_limiter.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace {

constexpr uint16_t TEST_PORT = 9091;
const std::string TEST_WAL = "test_e2e.wal";

int connect_client() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(TEST_PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

std::string read_response(int sock, size_t expected_bytes) {
    std::string result;
    result.resize(expected_bytes);
    size_t total_read = 0;

    while (total_read < expected_bytes) {
        ssize_t n = recv(sock, result.data() + total_read, expected_bytes - total_read, 0);
        if (n <= 0) break;
        total_read += n;
    }
    result.resize(total_read);
    return result;
}

} // namespace

class EndToEndSocketTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::remove(TEST_WAL.c_str());

        store_ = std::make_unique<ShardedStore>(16, 50000);
        wal_ = std::make_unique<WalManager>(TEST_WAL, FsyncPolicy::ALWAYS);
        ASSERT_TRUE(wal_->open());

        server_ = std::make_unique<TcpServer>(TEST_PORT);
        ASSERT_TRUE(server_->start());

        limiter_ = std::make_unique<RateLimiter>(10000.0, 10000.0); // high limits for tests
        loop_ = std::make_unique<EventLoop>(*server_, *store_, *wal_, *limiter_);

        server_thread_ = std::thread([this]() {
            loop_->run();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        loop_->stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }

        wal_->close();
        std::remove(TEST_WAL.c_str());
    }

    std::unique_ptr<ShardedStore> store_;
    std::unique_ptr<WalManager> wal_;
    std::unique_ptr<TcpServer> server_;
    std::unique_ptr<RateLimiter> limiter_;
    std::unique_ptr<EventLoop> loop_;
    std::thread server_thread_;
};

TEST_F(EndToEndSocketTest, BasicClientLifecycle) {
    int client = connect_client();
    ASSERT_GE(client, 0);

    // 1. SET
    std::string set_cmd = "SET client_1 hello_world\r\n";
    send(client, set_cmd.data(), set_cmd.size(), 0);
    EXPECT_EQ(read_response(client, 5), "+OK\r\n");

    // 2. GET
    std::string get_cmd = "GET client_1\r\n";
    send(client, get_cmd.data(), get_cmd.size(), 0);
    EXPECT_EQ(read_response(client, 18), "$11\r\nhello_world\r\n");

    // 3. DEL
    std::string del_cmd = "DEL client_1\r\n";
    send(client, del_cmd.data(), del_cmd.size(), 0);
    EXPECT_EQ(read_response(client, 4), ":1\r\n");

    // 4. GET after DEL
    send(client, get_cmd.data(), get_cmd.size(), 0);
    EXPECT_EQ(read_response(client, 5), "$-1\r\n");

    close(client);
}

TEST_F(EndToEndSocketTest, CommandPipelining) {
    int client = connect_client();
    ASSERT_GE(client, 0);

    std::string batch;
    for (int i = 0; i < 10; ++i) {
        batch += "SET pipe_" + std::to_string(i) + " val_" + std::to_string(i) + "\r\n";
    }

    send(client, batch.data(), batch.size(), 0);

    std::string expected_replies;
    for (int i = 0; i < 10; ++i) expected_replies += "+OK\r\n";

    EXPECT_EQ(read_response(client, 50), expected_replies);

    close(client);
}

TEST_F(EndToEndSocketTest, StreamFragmentationTrickleFeed) {
    int client = connect_client();
    ASSERT_GE(client, 0);

    std::string fragmented_cmd = "SET trickled_key trickled_value\r\n";

    for (char c : fragmented_cmd) {
        send(client, &c, 1, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_EQ(read_response(client, 5), "+OK\r\n");

    std::string get_cmd = "GET trickled_key\r\n";
    send(client, get_cmd.data(), get_cmd.size(), 0);
    EXPECT_EQ(read_response(client, 21), "$14\r\ntrickled_value\r\n");

    close(client);
}

TEST_F(EndToEndSocketTest, MultipleConcurrentClients) {
    const int num_clients = 5;
    std::vector<std::thread> client_threads;

    for (int c = 0; c < num_clients; ++c) {
        client_threads.emplace_back([c]() {
            int sock = connect_client();
            ASSERT_GE(sock, 0);

            for (int i = 0; i < 20; ++i) {
                std::string k = "c" + std::to_string(c) + "_k" + std::to_string(i);
                std::string v = "v" + std::to_string(i);

                std::string cmd = "SET " + k + " " + v + "\r\n";
                send(sock, cmd.data(), cmd.size(), 0);
                ASSERT_EQ(read_response(sock, 5), "+OK\r\n");
            }
            close(sock);
        });
    }

    for (auto& th : client_threads) th.join();
}