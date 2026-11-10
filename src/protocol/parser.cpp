#include "protocol/parser.hpp"
#include <charconv>
#include <algorithm>
#include <cctype>

namespace {

// Safety constants
constexpr size_t MAX_KEY_SIZE = 1024;             // 1 KB
constexpr size_t MAX_VAL_SIZE = 32 * 1024 * 1024; // 32 MB
constexpr size_t MAX_LINE_SIZE = 64 * 1024;       // 64 KB

// Zero-allocation case-insensitive equality check
bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != 
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<std::string_view> ProtocolParser::tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;

    // Defense 1: Reject lines exceeding maximum safe buffer size
    if (line.size() > MAX_LINE_SIZE) {
        return tokens;
    }

    // Defense 2: Strip trailing CRLF safely
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }

    // Defense 3: Extract tokens, compressing arbitrary whitespace
    size_t start = line.find_first_not_of(' ');
    while (start != std::string_view::npos) {
        size_t end = line.find_first_of(' ', start);
        if (end == std::string_view::npos) {
            tokens.push_back(line.substr(start));
            break;
        }
        tokens.push_back(line.substr(start, end - start));
        start = line.find_first_not_of(' ', end);
    }

    return tokens;
}

Command ProtocolParser::parse(std::string_view raw_line) {
    auto tokens = tokenize(raw_line);

    // Defense 4: Empty input
    if (tokens.empty()) {
        return {CommandType::UNKNOWN, "", "", 0, "ERR empty command"};
    }

    const auto& verb = tokens[0];

    // ------------------------------------------------------------------------
    // GET <key>
    // ------------------------------------------------------------------------
    if (iequals(verb, "GET")) {
        if (tokens.size() != 2) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR wrong number of arguments for 'GET'"};
        }
        if (tokens[1].size() > MAX_KEY_SIZE) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR key too large"};
        }
        return {CommandType::GET, std::string(tokens[1]), "", 0, ""};
    }

    // ------------------------------------------------------------------------
    // DEL <key>
    // ------------------------------------------------------------------------
    if (iequals(verb, "DEL")) {
        if (tokens.size() != 2) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR wrong number of arguments for 'DEL'"};
        }
        if (tokens[1].size() > MAX_KEY_SIZE) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR key too large"};
        }
        return {CommandType::DEL, std::string(tokens[1]), "", 0, ""};
    }

    // ------------------------------------------------------------------------
    // STATS
    // ------------------------------------------------------------------------
    if (iequals(verb, "STATS")) {
        if (tokens.size() != 1) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR 'STATS' takes no arguments"};
        }
        return {CommandType::STATS, "", "", 0, ""};
    }

    // ------------------------------------------------------------------------
    // SET <key> <val> [EX <sec>]
    // ------------------------------------------------------------------------
    if (iequals(verb, "SET")) {
        if (tokens.size() != 3 && tokens.size() != 5) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR wrong number of arguments for 'SET'"};
        }

        if (tokens[1].size() > MAX_KEY_SIZE) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR key too large"};
        }
        if (tokens[2].size() > MAX_VAL_SIZE) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR value too large"};
        }

        uint64_t ttl_ms = 0;

        // TTL option: EX <sec>
        if (tokens.size() == 5) {
            if (!iequals(tokens[3], "EX")) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR syntax error in 'SET'"};
            }

            uint64_t ttl_sec = 0;
            const char* start_ptr = tokens[4].data();
            const char* end_ptr = start_ptr + tokens[4].size();

            auto [ptr, ec] = std::from_chars(start_ptr, end_ptr, ttl_sec);

            // Bounds & format validation
            if (ec == std::errc::result_out_of_range) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR invalid expire time, overflow"};
            }
            if (ec == std::errc::invalid_argument || ptr != end_ptr || ttl_sec == 0) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR invalid expire time in 'SET'"};
            }

            // Guard against multiplication overflow when converting to ms
            if (ttl_sec > (UINT64_MAX / 1000)) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR invalid expire time, overflow"};
            }

            ttl_ms = ttl_sec * 1000;
        }

        return {CommandType::SET, std::string(tokens[1]), std::string(tokens[2]), ttl_ms, ""};
    }

    // ------------------------------------------------------------------------
    // Unknown Verb
    // ------------------------------------------------------------------------
    return {CommandType::UNKNOWN, "", "", 0, "ERR unknown command '" + std::string(verb) + "'"};
}