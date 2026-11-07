#pragma once

#include <string>

struct Connection {
    int fd{-1};
    std::string read_buffer;  // Accumulates bytes until complete \r\n arrives
    std::string write_buffer; // Holds response bytes if socket buffer is temporarily full
};