#include "network/tcp_server.hpp"
#include "network/event_loop.hpp"
#include <iostream>

int main() {
    TcpServer server(8080);

    if (!server.start()) {
        std::cerr << "Failed to start TCP server\n";
        return 1;
    }

    EventLoop loop(server);
    std::cout << "VoltKV Server running on port 8080. Press Ctrl+C to stop.\n";
    loop.run();

    return 0;
}