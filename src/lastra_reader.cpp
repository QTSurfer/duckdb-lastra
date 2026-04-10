#include "lastra_reader.hpp"
#include <cstring>
#include <stdexcept>

namespace duckdb {
namespace lastra {

static inline uint32_t read_u32(const uint8_t *p) {
    uint32_t v; std::memcpy(&v, p, 4); return v;
}
static inline int32_t read_i32(const uint8_t *p) {
    int32_t v; std::memcpy(&v, p, 4); return v;
}
static inline uint16_t read_u16(const uint8_t *p) {
    uint16_t v; std::memcpy(&v, p, 2); return v;
}
static inline int64_t read_i64(const uint8_t *p) {
    int64_t v; std::memcpy(&v, p, 8); return v;
}

LastraFile parse(const uint8_t *data, size_t length) {
    LastraFile file;
    size_t pos = 0;

    // Header (22 bytes)
    if (length < 22) throw std::runtime_error("Lastra file too short");
    uint32_t magic = read_u32(data + pos); pos += 4;
    if (magic != MAGIC) throw std::runtime_error("Not a Lastra file");
    pos += 2; // version
    file.flags = read_u16(data + pos); pos += 2;
    file.series_row_count = read_i32(data + pos); pos += 4;
    file.series_col_count = read_i32(data + pos); pos += 4;
    file.events_row_count = read_i32(data + pos); pos += 4;
    file.events_col_count = read_u16(data + pos); pos += 2;

    file.has_checksums = (file.flags & FLAG_HAS_CHECKSUMS) != 0;
    file.has_row_groups = (file.flags & FLAG_HAS_ROW_GROUPS) != 0;

    // Column descriptors
    auto read_descriptors = [&](int count) -> std::vector<ColumnDescriptor> {
        std::vector<ColumnDescriptor> cols;
        for (int i = 0; i < count; i++) {
            ColumnDescriptor col;
            col.codec = static_cast<Codec>(data[pos++]);
            col.data_type = static_cast<DataType>(data[pos++]);
            uint8_t col_flags = data[pos++];
            uint8_t name_len = data[pos++];
            col.name = std::string(reinterpret_cast<const char *>(data + pos), name_len);
            pos += name_len;
            if (col_flags & 0x02) {
                uint16_t meta_len = read_u16(data + pos);
                pos += 2 + meta_len; // skip metadata
            }
            cols.push_back(col);
        }
        return cols;
    };

    file.series_columns = read_descriptors(file.series_col_count);
    bool has_events = (file.flags & FLAG_HAS_EVENTS) != 0;
    if (has_events) {
        file.event_columns = read_descriptors(file.events_col_count);
    }

    // Detect footer size hint: last 8 bytes = [LAS! magic][footer size LE]
    size_t trailer_size = 4; // default: just LAS!
    if (length >= 8) {
        uint32_t trail_magic = read_u32(data + length - 8);
        if (trail_magic == FOOTER_MAGIC) {
            trailer_size = 8; // LAS! + footerSize
        }
    }

    // Data section
    if (file.has_row_groups) {
        // Parse footer using footer size hint
        int32_t footer_size = read_i32(data + length - 4);
        size_t fp = length - trailer_size - footer_size;

        int32_t rg_count = read_i32(data + fp); fp += 4;
        for (int32_t i = 0; i < rg_count; i++) {
            RowGroupStats stats;
            stats.offset = read_i32(data + fp); fp += 4;
            stats.row_count = read_i32(data + fp); fp += 4;
            stats.ts_min = read_i64(data + fp); fp += 8;
            stats.ts_max = read_i64(data + fp); fp += 8;
            file.row_groups.push_back(stats);
        }

        // Scan RG data forward using rgCount
        for (int32_t rg = 0; rg < rg_count; rg++) {
            std::vector<ColumnChunk> rg_cols;
            for (int c = 0; c < file.series_col_count; c++) {
                int32_t len = read_i32(data + pos);
                rg_cols.push_back({data + pos + 4, len, 0});
                pos += 4 + len;
            }
            file.rg_chunks.push_back(rg_cols);
        }

        // Events
        for (int i = 0; i < static_cast<int>(file.event_columns.size()); i++) {
            int32_t len = read_i32(data + pos);
            file.event_chunks.push_back({data + pos + 4, len, 0});
            pos += 4 + len;
        }

        // CRCs from footer
        if (file.has_checksums) {
            for (int32_t rg = 0; rg < rg_count; rg++) {
                for (int c = 0; c < file.series_col_count; c++) {
                    file.rg_chunks[rg][c].crc = read_u32(data + fp);
                    fp += 4;
                }
            }
        }
    } else {
        // Flat layout
        for (int c = 0; c < file.series_col_count; c++) {
            int32_t len = read_i32(data + pos);
            file.flat_chunks.push_back({data + pos + 4, len, 0});
            pos += 4 + len;
        }
        for (int i = 0; i < static_cast<int>(file.event_columns.size()); i++) {
            int32_t len = read_i32(data + pos);
            file.event_chunks.push_back({data + pos + 4, len, 0});
            pos += 4 + len;
        }

        // Single implicit row group
        file.row_groups.push_back({file.series_row_count, 0, 0, 0});
    }

    return file;
}

// Codec dispatch declarations
namespace codec {
    void decode_delta_varint(const uint8_t *data, size_t length, int32_t count, int64_t *output);
    void decode_gorilla(const uint8_t *data, size_t length, int32_t count, double *output);
    void decode_pongo(const uint8_t *data, size_t length, int32_t count, double *output);
    void decode_alp(const uint8_t *data, size_t length, int32_t count, double *output);
    void decode_raw_longs(const uint8_t *data, size_t length, int32_t count, int64_t *output);
    void decode_raw_doubles(const uint8_t *data, size_t length, int32_t count, double *output);
    void decode_varlen(const uint8_t *data, size_t length, int32_t count,
                       std::vector<std::string> &output, std::vector<bool> &nulls);
}

void decode_long_column(const ColumnChunk &chunk, int32_t count, Codec codec,
                        Vector &result, idx_t offset) {
    auto *data = FlatVector::GetData<int64_t>(result);
    switch (codec) {
        case Codec::DELTA_VARINT:
            codec::decode_delta_varint(chunk.data, chunk.length, count, data + offset);
            break;
        case Codec::RAW:
            codec::decode_raw_longs(chunk.data, chunk.length, count, data + offset);
            break;
        default:
            throw std::runtime_error("Unsupported LONG codec");
    }
}

void decode_double_column(const ColumnChunk &chunk, int32_t count, Codec codec,
                          Vector &result, idx_t offset) {
    auto *data = FlatVector::GetData<double>(result);
    switch (codec) {
        case Codec::ALP:
            codec::decode_alp(chunk.data, chunk.length, count, data + offset);
            break;
        case Codec::GORILLA:
            codec::decode_gorilla(chunk.data, chunk.length, count, data + offset);
            break;
        case Codec::PONGO:
            codec::decode_pongo(chunk.data, chunk.length, count, data + offset);
            break;
        case Codec::RAW:
            codec::decode_raw_doubles(chunk.data, chunk.length, count, data + offset);
            break;
        default:
            throw std::runtime_error("Unsupported DOUBLE codec");
    }
}

void decode_binary_column(const ColumnChunk &chunk, int32_t count,
                          Vector &result, idx_t offset) {
    std::vector<std::string> strings;
    std::vector<bool> nulls;
    codec::decode_varlen(chunk.data, chunk.length, count, strings, nulls);

    auto &validity = FlatVector::Validity(result);
    for (int32_t i = 0; i < count; i++) {
        if (nulls[i]) {
            validity.SetInvalid(offset + i);
        } else {
            FlatVector::GetData<string_t>(result)[offset + i] =
                StringVector::AddString(result, strings[i]);
        }
    }
}

} // namespace lastra
} // namespace duckdb
