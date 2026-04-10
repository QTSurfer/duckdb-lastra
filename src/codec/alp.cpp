#include "lastra_reader.hpp"
#include <cstring>
#include <cstdint>
#include <cmath>

namespace duckdb {
namespace lastra {
namespace codec {

// ALP: Adaptive Lossless floating-Point compression
// Format: [1 byte exponent] [1 byte factor_idx] [4 bytes count]
//         [FOR: 8 bytes base] [1 byte bit_width]
//         [bit-packed deltas] [exceptions]
//
// Decode steps:
// 1. Read header: exponent, factor_idx, count
// 2. Read FOR base and bit_width
// 3. Unpack bit-packed deltas (each bit_width bits)
// 4. Add base to each delta
// 5. Multiply by 10^(-exponent) to restore doubles
// 6. Apply exceptions (exact values for outliers)

static inline uint64_t read_le_u64(const uint8_t *p) {
    uint64_t v = 0;
    std::memcpy(&v, p, 8);
    return v;
}

static inline uint32_t read_le_u32(const uint8_t *p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

static inline uint16_t read_le_u16(const uint8_t *p) {
    uint16_t v = 0;
    std::memcpy(&v, p, 2);
    return v;
}

void decode_alp(const uint8_t *data, size_t length, int32_t count,
                double *output) {
    if (count == 0 || length == 0) return;

    size_t pos = 0;

    // Header: exponent (1 byte), factor_idx (1 byte)
    uint8_t exponent = data[pos++];
    uint8_t factor_idx = data[pos++];
    (void)factor_idx; // reserved for future use

    // Count (4 bytes LE) — should match the caller's count
    uint32_t stored_count = read_le_u32(data + pos);
    pos += 4;
    if (static_cast<int32_t>(stored_count) != count) {
        // Fallback: use stored_count
        count = static_cast<int32_t>(stored_count);
    }

    // FOR base (8 bytes LE, as int64)
    int64_t base = static_cast<int64_t>(read_le_u64(data + pos));
    pos += 8;

    // Bit width (1 byte)
    uint8_t bit_width = data[pos++];

    // Decode factor
    double factor = std::pow(10.0, static_cast<double>(exponent));

    if (bit_width == 0) {
        // All values are identical (base)
        double val = static_cast<double>(base) / factor;
        for (int32_t i = 0; i < count; i++) {
            output[i] = val;
        }
        pos += 0; // no packed data
    } else {
        // Bit-packed deltas
        int packed_bytes = (count * bit_width + 7) / 8;
        const uint8_t *packed = data + pos;
        pos += packed_bytes;

        uint64_t mask = (bit_width == 64) ? ~0ULL : ((1ULL << bit_width) - 1);

        // Unpack each delta
        int bit_offset = 0;
        for (int32_t i = 0; i < count; i++) {
            int byte_idx = bit_offset / 8;
            int bit_idx = bit_offset % 8;

            // Read up to 9 bytes to cover the value
            uint64_t raw = 0;
            int bytes_needed = (bit_idx + bit_width + 7) / 8;
            for (int b = 0; b < bytes_needed && (byte_idx + b) < packed_bytes; b++) {
                raw |= static_cast<uint64_t>(packed[byte_idx + b]) << (b * 8);
            }
            uint64_t delta = (raw >> bit_idx) & mask;

            int64_t encoded = base + static_cast<int64_t>(delta);
            output[i] = static_cast<double>(encoded) / factor;

            bit_offset += bit_width;
        }
    }

    // Exceptions: [2 bytes exception_count] [per exception: 2 bytes index, 8 bytes exact double]
    if (pos + 2 <= length) {
        uint16_t exception_count = read_le_u16(data + pos);
        pos += 2;
        for (uint16_t e = 0; e < exception_count && pos + 10 <= length; e++) {
            uint16_t idx = read_le_u16(data + pos);
            pos += 2;
            double exact;
            std::memcpy(&exact, data + pos, 8);
            pos += 8;
            if (idx < static_cast<uint16_t>(count)) {
                output[idx] = exact;
            }
        }
    }
}

} // namespace codec
} // namespace lastra
} // namespace duckdb
