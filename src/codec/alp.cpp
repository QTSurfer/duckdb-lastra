#include "lastra_reader.hpp"
#include <cstring>
#include <cstdint>
#include <cmath>

namespace duckdb {
namespace lastra {
namespace codec {

// ALP wire format (alp-java, big-endian multi-byte fields):
// [e:1][f:1][bitWidth:1][frame:8 BE][exceptionCount:2 BE][packedLength:2 BE]
// [packed deltas: packedLength bytes, LE bit-packed]
// [exceptions: exceptionCount × (index:2 BE, value:8 BE raw double bits)]

static inline int64_t read_be_i64(const uint8_t *p) {
    return static_cast<int64_t>(
        (static_cast<uint64_t>(p[0]) << 56) | (static_cast<uint64_t>(p[1]) << 48) |
        (static_cast<uint64_t>(p[2]) << 40) | (static_cast<uint64_t>(p[3]) << 32) |
        (static_cast<uint64_t>(p[4]) << 24) | (static_cast<uint64_t>(p[5]) << 16) |
        (static_cast<uint64_t>(p[6]) << 8)  |  static_cast<uint64_t>(p[7]));
}

static inline uint16_t read_be_u16(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static const double POW10[] = {
    1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0,
    10000000.0, 100000000.0, 1000000000.0, 10000000000.0,
    100000000000.0, 1000000000000.0, 10000000000000.0,
    100000000000000.0, 1000000000000000.0, 10000000000000000.0,
    100000000000000000.0, 1000000000000000000.0
};

void decode_alp(const uint8_t *data, size_t length, int32_t count,
                double *output) {
    if (count == 0 || length < 15) return;

    size_t pos = 0;
    uint8_t e = data[pos++];
    uint8_t f = data[pos++];
    uint8_t bit_width = data[pos++];
    int64_t frame = read_be_i64(data + pos); pos += 8;
    uint16_t exception_count = read_be_u16(data + pos); pos += 2;
    uint16_t packed_length = read_be_u16(data + pos); pos += 2;

    double factor_e = (e < 19) ? POW10[e] : std::pow(10.0, e);
    double factor_f = (f < 19) ? POW10[f] : std::pow(10.0, f);

    if (bit_width == 0) {
        double val = static_cast<double>(frame) * factor_f / factor_e;
        for (int32_t i = 0; i < count; i++) {
            output[i] = val;
        }
    } else {
        const uint8_t *packed = data + pos;
        uint64_t mask = (bit_width == 64) ? ~0ULL : ((1ULL << bit_width) - 1);

        int bit_offset = 0;
        for (int32_t i = 0; i < count; i++) {
            int byte_idx = bit_offset / 8;
            int bit_idx = bit_offset % 8;

            uint64_t raw = 0;
            int bytes_needed = (bit_idx + bit_width + 7) / 8;
            for (int b = 0; b < bytes_needed && (byte_idx + b) < static_cast<int>(packed_length); b++) {
                raw |= static_cast<uint64_t>(packed[byte_idx + b]) << (b * 8);
            }
            int64_t encoded = frame + static_cast<int64_t>((raw >> bit_idx) & mask);
            output[i] = static_cast<double>(encoded) * factor_f / factor_e;

            bit_offset += bit_width;
        }
    }
    pos += packed_length;

    // Exceptions: [index:2 BE][value:8 BE raw double bits]
    for (uint16_t ex = 0; ex < exception_count && pos + 10 <= length; ex++) {
        uint16_t idx = read_be_u16(data + pos); pos += 2;
        uint64_t bits = static_cast<uint64_t>(read_be_i64(data + pos)); pos += 8;
        double exact;
        std::memcpy(&exact, &bits, 8);
        if (idx < static_cast<uint16_t>(count)) {
            output[idx] = exact;
        }
    }
}

} // namespace codec
} // namespace lastra
} // namespace duckdb
