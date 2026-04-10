#pragma once

#include "duckdb.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace duckdb {
namespace lastra {

// Wire format constants
static constexpr uint32_t MAGIC = 0x4C415354;        // "LAST" LE
static constexpr uint32_t FOOTER_MAGIC = 0x4C415321;  // "LAS!" LE
static constexpr uint16_t FLAG_HAS_EVENTS = 1;
static constexpr uint16_t FLAG_HAS_FOOTER = 1 << 1;
static constexpr uint16_t FLAG_HAS_CHECKSUMS = 1 << 2;
static constexpr uint16_t FLAG_HAS_ROW_GROUPS = 1 << 3;

enum class DataType : uint8_t { LONG = 0, DOUBLE = 1, BINARY = 2 };
enum class Codec : uint8_t {
    RAW = 0, DELTA_VARINT = 1, ALP = 2, VARLEN = 3,
    VARLEN_ZSTD = 4, VARLEN_GZIP = 5, GORILLA = 6, PONGO = 7
};

struct ColumnDescriptor {
    std::string name;
    DataType data_type;
    Codec codec;
};

struct RowGroupStats {
    int32_t row_count;
    int32_t offset;
    int64_t ts_min;
    int64_t ts_max;
};

struct ColumnChunk {
    const uint8_t *data;
    int32_t length;
    uint32_t crc;
};

struct LastraFile {
    int32_t series_row_count;
    int32_t series_col_count;
    int32_t events_row_count;
    int32_t events_col_count;
    uint16_t flags;
    bool has_checksums;
    bool has_row_groups;

    std::vector<ColumnDescriptor> series_columns;
    std::vector<ColumnDescriptor> event_columns;
    std::vector<RowGroupStats> row_groups;

    // Per row group, per column: chunk location
    // row_group_chunks[rg_index][col_index]
    std::vector<std::vector<ColumnChunk>> rg_chunks;

    // Flat layout (no row groups)
    std::vector<ColumnChunk> flat_chunks;
    std::vector<ColumnChunk> event_chunks;
};

// Parse a Lastra file from a buffer
LastraFile parse(const uint8_t *data, size_t length);

// Decode a column chunk into a DuckDB Vector
void decode_long_column(const ColumnChunk &chunk, int32_t count, Codec codec,
                        Vector &result, idx_t offset);
void decode_double_column(const ColumnChunk &chunk, int32_t count, Codec codec,
                          Vector &result, idx_t offset);
void decode_binary_column(const ColumnChunk &chunk, int32_t count,
                          Vector &result, idx_t offset);

} // namespace lastra

// Register the table scan function
void RegisterLastraScan(DatabaseInstance &instance);

} // namespace duckdb
