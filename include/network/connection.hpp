#pragma once

#include <string>

struct Connection {
    int fd{-1};
    std::string client_ip;    // <-- Captured client IP for rate limiting
    std::string read_buffer;
    std::string write_buffer;
};