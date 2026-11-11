#include <gtest/gtest.h>
#include "protocol/rate_limiter.hpp"
#include <thread>
#include <chrono>

TEST(RateLimiterTest, BurstAndRefill) {
    // 10 tokens/sec, capacity of 5
    RateLimiter limiter(10.0, 5.0);

    // Consume 5 tokens immediately (burst)
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.allow("192.168.1.1"));
    }

    // 6th request must be rejected
    EXPECT_FALSE(limiter.allow("192.168.1.1"));

    // Sleep 250ms -> should refill ~2.5 tokens
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    // Next request must now succeed
    EXPECT_TRUE(limiter.allow("192.168.1.1"));
}

TEST(RateLimiterTest, IPIsolation) {
    RateLimiter limiter(10.0, 2.0);

    // Drain IP A
    EXPECT_TRUE(limiter.allow("10.0.0.1"));
    EXPECT_TRUE(limiter.allow("10.0.0.1"));
    EXPECT_FALSE(limiter.allow("10.0.0.1"));

    // IP B must remain unaffected
    EXPECT_TRUE(limiter.allow("10.0.0.2"));
    EXPECT_TRUE(limiter.allow("10.0.0.2"));
    EXPECT_FALSE(limiter.allow("10.0.0.2"));
}

TEST(RateLimiterTest, WhitelistBypass) {
    RateLimiter limiter(1.0, 1.0);
    limiter.whitelist_ip("127.0.0.1");

    // Can make 100 requests without ever being throttled
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(limiter.allow("127.0.0.1"));
    }
}