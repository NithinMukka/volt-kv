#pragma once

#include "protocol/command.hpp"
#include <string_view>
#include <vector>

class ProtocolParser {
public:
    static Command parse(std::string_view raw_line);

private:
    static std::vector<std::string_view> tokenize(std::string_view line);
};