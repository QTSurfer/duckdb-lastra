#include "lastra_reader.hpp"
#include <cstdint>
#include <cstring>

namespace duckdb {
namespace lastra {
namespace codec {

// Delta-varint wire format (lastra-java DeltaVarintCodec):
// [first value: 8 bytes LE fixed]
// [second delta: zigzag varint]
// [remaining: delta-of-delta zigzag varints]

static inline int64_t read_le_i64(const uint8_t *p) {
    int64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

static inline int64_t zigzag_decode(uint64_t n) {
    return static_cast<int64_t>((n >> 1) ^ (~(n & 1) + 1));
}

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
    if (count == 0) return;
    size_t pos = 0;

    // First value: fixed 8 bytes LE
    int64_t prev = read_le_i64(data + pos);
    pos += 8;
    output[0] = prev;

    // Second value: first delta as zigzag varint
    int64_t prev_delta = 0;
    if (count > 1) {
        prev_delta = zigzag_decode(read_varint(data, pos, length));
        prev = prev + prev_delta;
        output[1] = prev;
    }

    // Remaining: delta-of-delta zigzag varints
    for (int32_t i = 2; i < count; i++) {
        int64_t dod = zigzag_decode(read_varint(data, pos, length));
        prev_delta += dod;
        prev += prev_delta;
        output[i] = prev;
    }
}

} // namespace codec
} // namespace lastra
} // namespace duckdb
