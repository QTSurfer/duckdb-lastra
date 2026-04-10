#include "lastra_reader.hpp"
#include <cstring>

namespace duckdb {
namespace lastra {
namespace codec {

void decode_raw_longs(const uint8_t *data, size_t length, int32_t count,
                      int64_t *output) {
    for (int32_t i = 0; i < count && (i * 8 + 8) <= static_cast<int32_t>(length); i++) {
        std::memcpy(&output[i], data + i * 8, 8);
    }
}

void decode_raw_doubles(const uint8_t *data, size_t length, int32_t count,
                        double *output) {
    for (int32_t i = 0; i < count && (i * 8 + 8) <= static_cast<int32_t>(length); i++) {
        std::memcpy(&output[i], data + i * 8, 8);
    }
}

} // namespace codec
} // namespace lastra
} // namespace duckdb
