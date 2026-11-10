#include "protocol/parser.hpp"
#include <charconv>
#include <algorithm>
#include <cctype>

namespace {

constexpr size_t MAX_KEY_SIZE = 1024;                    // 1 KB
constexpr size_t MAX_VAL_SIZE = 32 * 1024 * 1024;        // 32 MB
constexpr size_t MAX_LINE_SIZE = MAX_VAL_SIZE + 2048;    // Line size comfortably holds 32MB payload + command/key overhead

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

// Safely trims leading spaces
std::string_view trim_leading_spaces(std::string_view sv) {
    size_t pos = sv.find_first_not_of(' ');
    if (pos == std::string_view::npos) return {};
    return sv.substr(pos);
}

// Extracts next space-delimited word
std::pair<std::string_view, std::string_view> next_token(std::string_view sv) {
    sv = trim_leading_spaces(sv);
    if (sv.empty()) return {{}, {}};

    size_t pos = sv.find_first_of(' ');
    if (pos == std::string_view::npos) {
        return {sv, {}};
    }
    return {sv.substr(0, pos), sv.substr(pos + 1)};
}

} // namespace

Command ProtocolParser::parse(std::string_view raw_line) {
    // Defense 1: Reject over-sized line inputs upfront
    if (raw_line.size() > MAX_LINE_SIZE) {
        return {CommandType::UNKNOWN, "", "", 0, "ERR line too long"};
    }

    // Strip trailing CRLF
    while (!raw_line.empty() && (raw_line.back() == '\r' || raw_line.back() == '\n')) {
        raw_line.remove_suffix(1);
    }

    auto [verb, rest] = next_token(raw_line);
    if (verb.empty()) {
        return {CommandType::UNKNOWN, "", "", 0, "ERR empty command"};
    }

    // ------------------------------------------------------------------------
    // GET <key>
    // ------------------------------------------------------------------------
    if (iequals(verb, "GET")) {
        auto [key, extra] = next_token(rest);
        if (key.empty()) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR wrong number of arguments for 'GET'"};
        }
        if (!trim_leading_spaces(extra).empty()) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR wrong number of arguments for 'GET'"};
        }
        if (key.size() > MAX_KEY_SIZE) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR key too large"};
        }
        return {CommandType::GET, std::string(key), "", 0, ""};
    }

    // ------------------------------------------------------------------------
    // DEL <key>
    // ------------------------------------------------------------------------
    if (iequals(verb, "DEL")) {
        auto [key, extra] = next_token(rest);
        if (key.empty()) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR wrong number of arguments for 'DEL'"};
        }
        if (!trim_leading_spaces(extra).empty()) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR wrong number of arguments for 'DEL'"};
        }
        if (key.size() > MAX_KEY_SIZE) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR key too large"};
        }
        return {CommandType::DEL, std::string(key), "", 0, ""};
    }

    // ------------------------------------------------------------------------
    // STATS
    // ------------------------------------------------------------------------
    if (iequals(verb, "STATS")) {
        if (!trim_leading_spaces(rest).empty()) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR 'STATS' takes no arguments"};
        }
        return {CommandType::STATS, "", "", 0, ""};
    }

 // ------------------------------------------------------------------------
    // SET <key> <val> [EX <sec>]  (Supports spaces inside <val>)
    // ------------------------------------------------------------------------
    if (iequals(verb, "SET")) {
        auto [key, body] = next_token(rest);
        if (key.empty() || body.empty()) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR wrong number of arguments for 'SET'"};
        }
        if (key.size() > MAX_KEY_SIZE) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR key too large"};
        }

        body = trim_leading_spaces(body);
        std::string_view val = body;
        uint64_t ttl_ms = 0;

        // Check if command contains EX flag
        size_t ex_pos = body.rfind(" EX ");
        if (ex_pos == std::string_view::npos) {
            ex_pos = body.rfind(" ex ");
        }

        if (ex_pos != std::string_view::npos) {
            std::string_view ex_part = body.substr(ex_pos + 4);
            auto [sec_str, extra] = next_token(ex_part);

            // Rejection 1: Trailing arguments after EX <sec>
            if (!trim_leading_spaces(extra).empty()) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR syntax error in 'SET'"};
            }

            // Rejection 2: Missing seconds after EX
            if (sec_str.empty()) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR syntax error in 'SET'"};
            }

            uint64_t ttl_sec = 0;
            auto [ptr, ec] = std::from_chars(sec_str.data(), sec_str.data() + sec_str.size(), ttl_sec);

            if (ec == std::errc::result_out_of_range) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR invalid expire time, overflow"};
            }
            if (ec != std::errc{} || ptr != (sec_str.data() + sec_str.size()) || ttl_sec == 0) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR invalid expire time in 'SET'"};
            }

            if (ttl_sec > (UINT64_MAX / 1000)) {
                return {CommandType::UNKNOWN, "", "", 0, "ERR invalid expire time, overflow"};
            }

            ttl_ms = ttl_sec * 1000;
            val = body.substr(0, ex_pos); // Strip EX <sec> from value
        }

        if (val.size() > MAX_VAL_SIZE) {
            return {CommandType::UNKNOWN, "", "", 0, "ERR value too large"};
        }

        return {CommandType::SET, std::string(key), std::string(val), ttl_ms, ""};
    }
    return {CommandType::UNKNOWN, "", "", 0, "ERR unknown command '" + std::string(verb) + "'"};
}