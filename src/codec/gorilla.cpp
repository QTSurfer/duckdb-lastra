#include "lastra_reader.hpp"
#include <cstring>
#include <cstdint>

namespace duckdb {
namespace lastra {
namespace codec {

// Bit reader for Gorilla XOR encoding
class BitReader {
public:
    BitReader(const uint8_t *data, size_t length)
        : data_(data), length_(length), byte_pos_(0), bit_pos_(0) {}

    uint64_t read_bits(int n) {
        uint64_t result = 0;
        for (int i = 0; i < n; i++) {
            if (byte_pos_ < length_) {
                result = (result << 1) | ((data_[byte_pos_] >> (7 - bit_pos_)) & 1);
                bit_pos_++;
                if (bit_pos_ == 8) { bit_pos_ = 0; byte_pos_++; }
            }
        }
        return result;
    }

    bool read_bit() {
        return read_bits(1) != 0;
    }

private:
    const uint8_t *data_;
    size_t length_;
    size_t byte_pos_;
    int bit_pos_;
};

void decode_gorilla(const uint8_t *data, size_t length, int32_t count,
                    double *output) {
    if (count == 0) return;

    BitReader reader(data, length);

    // First value: raw 64 bits
    uint64_t prev_bits;
    prev_bits = reader.read_bits(64);
    std::memcpy(&output[0], &prev_bits, 8);

    int prev_leading = 0;
    int prev_trailing = 0;

    for (int32_t i = 1; i < count; i++) {
        if (!reader.read_bit()) {
            // 0: same as previous
            std::memcpy(&output[i], &prev_bits, 8);
        } else if (!reader.read_bit()) {
            // 10: new leading/trailing
            int leading = static_cast<int>(reader.read_bits(6));
            int block_size = static_cast<int>(reader.read_bits(6));
            if (block_size == 0) block_size = 64;
            int trailing = 64 - leading - block_size;

            uint64_t xor_val = reader.read_bits(block_size) << trailing;
            prev_bits ^= xor_val;
            std::memcpy(&output[i], &prev_bits, 8);
            prev_leading = leading;
            prev_trailing = trailing;
        } else {
            // 11: reuse leading/trailing
            int block_size = 64 - prev_leading - prev_trailing;
            uint64_t xor_val = reader.read_bits(block_size) << prev_trailing;
            prev_bits ^= xor_val;
            std::memcpy(&output[i], &prev_bits, 8);
        }
    }
}

} // namespace codec
} // namespace lastra
} // namespace duckdb
