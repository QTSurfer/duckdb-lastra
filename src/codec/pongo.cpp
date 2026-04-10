#include "lastra_reader.hpp"
#include <cstring>
#include <cstdint>
#include <cmath>

namespace duckdb {
namespace lastra {
namespace codec {

// Pongo = decimal erasure + Gorilla XOR
// Format: [1 byte exponent] [gorilla-encoded erased mantissas]
// Erasure: multiply by 10^exponent, round, store as uint64 bits reinterpreted as double

// Forward declare gorilla decoder
void decode_gorilla(const uint8_t *data, size_t length, int32_t count, double *output);

// Pongo eraser: detects the common decimal exponent from a sample
// The encoder chose exponent E such that value * 10^E is integer for most values.
// Stored as first byte of the compressed data.

void decode_pongo(const uint8_t *data, size_t length, int32_t count,
                  double *output) {
    if (count == 0 || length == 0) return;

    // First byte: exponent (number of decimal places)
    uint8_t exponent = data[0];
    double factor = std::pow(10.0, static_cast<double>(exponent));

    // Remaining bytes: gorilla-encoded erased values
    // Erased value = round(original * factor), stored as raw uint64 bits reinterpreted as double
    decode_gorilla(data + 1, length - 1, count, output);

    // Restore: divide by factor
    for (int32_t i = 0; i < count; i++) {
        // The gorilla-decoded value is the erased integer bits reinterpreted as double
        uint64_t erased_bits;
        std::memcpy(&erased_bits, &output[i], 8);
        // The erased value is stored as (double)(int64_t)(original * factor)
        // So erased_bits is the IEEE 754 representation of that integer-as-double
        output[i] = output[i] / factor;
    }
}

} // namespace codec
} // namespace lastra
} // namespace duckdb
