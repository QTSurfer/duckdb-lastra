#include "lastra_reader.hpp"
#include <cstring>
#include <vector>
#include "zstd.h"
using duckdb_zstd::ZSTD_decompress;
using duckdb_zstd::ZSTD_isError;

namespace duckdb {
namespace lastra {
namespace codec {

static constexpr uint8_t COMPRESSION_NONE = 0;
static constexpr uint8_t COMPRESSION_ZSTD = 1;
static constexpr uint8_t COMPRESSION_GZIP = 2;

// Varlen format: [1 byte compression] [4 bytes uncompressed_size]
//                [compressed or raw payload]
// Payload: for each value: [4 bytes length] [data] (length=-1 for null)

void decode_varlen(const uint8_t *data, size_t length, int32_t count,
                   std::vector<std::string> &output, std::vector<bool> &nulls) {
    if (count == 0 || length == 0) return;

    output.resize(count);
    nulls.resize(count, false);

    size_t pos = 0;
    uint8_t compression = data[pos++];

    uint32_t uncompressed_size = 0;
    std::memcpy(&uncompressed_size, data + pos, 4);
    pos += 4;

    const uint8_t *payload;
    std::vector<uint8_t> decompressed;

    if (compression == COMPRESSION_ZSTD) {
        decompressed.resize(uncompressed_size);
        size_t result = ZSTD_decompress(decompressed.data(), uncompressed_size,
                                        data + pos, length - pos);
        if (ZSTD_isError(result)) {
            // Fallback: treat as empty
            return;
        }
        payload = decompressed.data();
        length = uncompressed_size;
    } else {
        payload = data + pos;
        length -= pos;
    }

    // Parse values: [4 bytes length] [data]
    size_t vpos = 0;
    for (int32_t i = 0; i < count && vpos < length; i++) {
        int32_t val_len;
        std::memcpy(&val_len, payload + vpos, 4);
        vpos += 4;
        if (val_len < 0) {
            nulls[i] = true;
        } else if (val_len == 0) {
            output[i] = "";
        } else {
            output[i] = std::string(reinterpret_cast<const char *>(payload + vpos), val_len);
            vpos += val_len;
        }
    }
}

} // namespace codec
} // namespace lastra
} // namespace duckdb
