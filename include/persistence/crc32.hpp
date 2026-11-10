#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

namespace detail {

constexpr std::array<uint32_t, 256> generate_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320; // IEEE 802.3 polynomial
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

} // namespace detail

class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t length) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xFFFFFFFF;

        for (size_t i = 0; i < length; ++i) {
            uint8_t lookup_index = (crc ^ bytes[i]) & 0xFF;
            crc = (crc >> 8) ^ TABLE[lookup_index];
        }

        return crc ^ 0xFFFFFFFF;
    }

private:
    static constexpr std::array<uint32_t, 256> TABLE = detail::generate_crc32_table();
};