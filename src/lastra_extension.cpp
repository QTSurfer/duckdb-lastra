#define DUCKDB_EXTENSION_MAIN

#include "lastra_extension.hpp"
#include "lastra_reader.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/file_system.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <memory>
#include <vector>

namespace duckdb {

// ---- Bind data: holds parsed file info ----
struct LastraBindData : public TableFunctionData {
    std::string file_path;
    std::vector<uint8_t> file_data;
    lastra::LastraFile parsed;
    vector<LogicalType> return_types;
    vector<string> return_names;
};

// ---- Global state: tracks which row group we're scanning ----
struct LastraGlobalState : public GlobalTableFunctionState {
    idx_t current_rg = 0;
    idx_t row_offset = 0; // within current RG
    bool finished = false;
};

// ---- Bind function: read file, parse header, define output schema ----
static unique_ptr<FunctionData> LastraBind(ClientContext &context,
                                           TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types,
                                           vector<string> &names) {
    auto result = make_uniq<LastraBindData>();
    result->file_path = input.inputs[0].GetValue<string>();

    // Read entire file (TODO: for remote files, use HTTP Range for footer first)
    auto &fs = FileSystem::GetFileSystem(context);
    auto handle = fs.OpenFile(result->file_path, FileFlags::FILE_FLAGS_READ);
    auto file_size = fs.GetFileSize(*handle);
    result->file_data.resize(file_size);
    fs.Read(*handle, result->file_data.data(), file_size);
    handle->Close();

    // Parse
    result->parsed = lastra::parse(result->file_data.data(), result->file_data.size());

    // Build output schema from series columns
    for (auto &col : result->parsed.series_columns) {
        names.push_back(col.name);
        switch (col.data_type) {
            case lastra::DataType::LONG:
                return_types.push_back(LogicalType::BIGINT);
                break;
            case lastra::DataType::DOUBLE:
                return_types.push_back(LogicalType::DOUBLE);
                break;
            case lastra::DataType::BINARY:
                return_types.push_back(LogicalType::VARCHAR);
                break;
        }
    }

    result->return_types = return_types;
    result->return_names = names;
    return std::move(result);
}

// ---- Init global state ----
static unique_ptr<GlobalTableFunctionState> LastraInitGlobal(ClientContext &context,
                                                              TableFunctionInitInput &input) {
    return make_uniq<LastraGlobalState>();
}

// ---- Scan function: fill vectors from row groups ----
static void LastraScan(ClientContext &context, TableFunctionInput &data,
                       DataChunk &output) {
    auto &bind = data.bind_data->CastNoConst<LastraBindData>();
    auto &state = data.global_state->Cast<LastraGlobalState>();
    auto &file = bind.parsed;

    if (state.finished) {
        output.SetCardinality(0);
        return;
    }

    idx_t total_output = 0;
    idx_t max_count = STANDARD_VECTOR_SIZE;

    while (total_output < max_count && state.current_rg < static_cast<idx_t>(file.row_groups.size())) {
        auto &rg = file.row_groups[state.current_rg];
        idx_t rows_left = rg.row_count - state.row_offset;
        idx_t to_read = MinValue<idx_t>(rows_left, max_count - total_output);

        // For each column, decode from the appropriate chunk
        for (idx_t col = 0; col < file.series_columns.size(); col++) {
            auto &desc = file.series_columns[col];
            lastra::ColumnChunk chunk;

            if (file.has_row_groups) {
                chunk = file.rg_chunks[state.current_rg][col];
            } else {
                chunk = file.flat_chunks[col];
            }

            // If we're reading a partial RG (row_offset > 0), we need to decode the full RG
            // and skip rows. For simplicity, decode full RG each time.
            // TODO: optimize by caching decoded RG data
            switch (desc.data_type) {
                case lastra::DataType::LONG:
                    lastra::decode_long_column(chunk, rg.row_count, desc.codec,
                                              output.data[col], total_output);
                    break;
                case lastra::DataType::DOUBLE:
                    lastra::decode_double_column(chunk, rg.row_count, desc.codec,
                                                output.data[col], total_output);
                    break;
                case lastra::DataType::BINARY:
                    lastra::decode_binary_column(chunk, rg.row_count,
                                                output.data[col], total_output);
                    break;
            }
        }

        total_output += to_read;
        state.row_offset += to_read;

        if (state.row_offset >= static_cast<idx_t>(rg.row_count)) {
            state.current_rg++;
            state.row_offset = 0;
        }
    }

    if (state.current_rg >= static_cast<idx_t>(file.row_groups.size())) {
        state.finished = true;
    }

    output.SetCardinality(total_output);
}

// ---- Register the scan function ----
void RegisterLastraScan(DatabaseInstance &instance) {
    TableFunction lastra_scan("read_lastra", {LogicalType::VARCHAR},
                              LastraScan, LastraBind, LastraInitGlobal);
    lastra_scan.projection_pushdown = false; // TODO: add column pruning
    ExtensionUtil::RegisterFunction(instance, lastra_scan);

    // Also register as replacement scan for .lastra files
    auto &config = DBConfig::GetConfig(instance);
    config.replacement_scans.emplace_back(
        [](ClientContext &context, const string &table_name, ReplacementScanData *data)
            -> unique_ptr<TableRef> {
            if (!StringUtil::EndsWith(StringUtil::Lower(table_name), ".lastra")) {
                return nullptr;
            }
            auto table_function = make_uniq<TableFunctionRef>();
            vector<unique_ptr<ParsedExpression>> children;
            children.push_back(make_uniq<ConstantExpression>(Value(table_name)));
            table_function->function = make_uniq<FunctionExpression>("read_lastra", std::move(children));
            return std::move(table_function);
        });
}

// ---- Extension entry point ----
void LastraExtension::Load(DuckDB &db) {
    RegisterLastraScan(*db.instance);
}

std::string LastraExtension::Name() {
    return "lastra";
}

std::string LastraExtension::Version() const {
    return "0.1.0";
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void lastra_init(duckdb::DatabaseInstance &instance) {
    duckdb::DuckDB db(instance);
    db.LoadExtension<duckdb::LastraExtension>();
}

DUCKDB_EXTENSION_API const char *lastra_version() {
    return duckdb::DuckDB::LibraryVersion();
}

}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN must be defined
#endif
