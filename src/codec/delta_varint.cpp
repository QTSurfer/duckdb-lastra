#include "lastra_reader.hpp"
#include <cstdint>

namespace duckdb {
namespace lastra {
namespace codec {

// Zigzag decode: (n >>> 1) ^ -(n & 1)
static inline int64_t zigzag_decode(uint64_t n) {
    return static_cast<int64_t>((n >> 1) ^ (~(n & 1) + 1));
}

// Read unsigned varint from buffer, advance pos
static inline uint64_t read_varint(const uint8_t *data, size_t &pos, size_t len) {
    uint64_t result = 0;
    int shift = 0;
    while (pos < len) {
        uint8_t b = data[pos++];
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

void decode_delta_varint(const uint8_t *data, size_t length, int32_t count,
                         int64_t *output) {
    size_t pos = 0;

    // First value: zigzag varint
    int64_t prev = zigzag_decode(read_varint(data, pos, length));
    output[0] = prev;

    // Second value (if exists): delta
    int64_t prev_delta = 0;
    if (count > 1) {
        prev_delta = zigzag_decode(read_varint(data, pos, length));
        prev = prev + prev_delta;
        output[1] = prev;
    }

    // Remaining: delta-of-delta
    for (int32_t i = 2; i < count; i++) {
        int64_t delta_of_delta = zigzag_decode(read_varint(data, pos, length));
        prev_delta += delta_of_delta;
        prev += prev_delta;
        output[i] = prev;
    }
}

} // namespace codec
} // namespace lastra
} // namespace duckdb
