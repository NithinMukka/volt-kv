#include <gtest/gtest.h>
#include "protocol/parser.hpp"

// Nominal Cases
TEST(ProtocolParserTest, ValidCommands) {
    auto cmd = ProtocolParser::parse("SET user_1 Alice\r\n");
    EXPECT_EQ(cmd.type, CommandType::SET);
    EXPECT_EQ(cmd.key, "user_1");
    EXPECT_EQ(cmd.value, "Alice");
    EXPECT_EQ(cmd.ttl_ms, 0);

    cmd = ProtocolParser::parse("set session token123 ex 30\n"); // Case-insensitive
    EXPECT_EQ(cmd.type, CommandType::SET);
    EXPECT_EQ(cmd.key, "session");
    EXPECT_EQ(cmd.value, "token123");
    EXPECT_EQ(cmd.ttl_ms, 30000);

    cmd = ProtocolParser::parse("GET user_1\r\n");
    EXPECT_EQ(cmd.type, CommandType::GET);
    EXPECT_EQ(cmd.key, "user_1");

    cmd = ProtocolParser::parse("del user_1\r\n");
    EXPECT_EQ(cmd.type, CommandType::DEL);
    EXPECT_EQ(cmd.key, "user_1");

    cmd = ProtocolParser::parse("STATS\r\n");
    EXPECT_EQ(cmd.type, CommandType::STATS);
}

// Edge Cases & Attack Defenses
TEST(ProtocolParserTest, MaliciousAndMalformedInputs) {
    // 1. Empty and whitespace
    EXPECT_EQ(ProtocolParser::parse("").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("   \r\n").type, CommandType::UNKNOWN);

    // 2. Token underflow
    EXPECT_EQ(ProtocolParser::parse("SET").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("SET key").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("GET").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("DEL").type, CommandType::UNKNOWN);

    // 3. Token overflow
    EXPECT_EQ(ProtocolParser::parse("GET k extra\r\n").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("DEL k extra\r\n").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("STATS extra\r\n").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("SET k v EX 10 extra\r\n").type, CommandType::UNKNOWN);

    // 4. Integer overflows on TTL
    EXPECT_EQ(ProtocolParser::parse("SET k v EX 9999999999999999999999999\r\n").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("SET k v EX -5\r\n").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("SET k v EX 0\r\n").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("SET k v EX abc\r\n").type, CommandType::UNKNOWN);
    EXPECT_EQ(ProtocolParser::parse("SET k v EX 10abc\r\n").type, CommandType::UNKNOWN);

    // 5. Unknown verbs
    EXPECT_EQ(ProtocolParser::parse("DROP DATABASE\r\n").type, CommandType::UNKNOWN);
}