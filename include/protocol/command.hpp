#pragma once

#include <string>
#include <cstdint>

enum class CommandType {
    GET,
    SET,
    DEL,
    STATS,
    UNKNOWN
};

struct Command {
    CommandType type = CommandType::UNKNOWN;
    std::string key;
    std::string value;
    uint64_t ttl_ms = 0;
    std::string error_message;
    bool is_http = false; // Flag to format response as HTTP/1.1 vs RESP
};