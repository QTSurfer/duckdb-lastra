# duckdb-lastra

DuckDB extension for reading [Lastra](https://github.com/QTSurfer/lastra-java) columnar time series files.

Enables native SQL queries against `.lastra` files — including remote files via HTTP range requests.

## Usage

```sql
-- Load extension
LOAD lastra;

-- Read a local file
SELECT * FROM 'data.lastra';

-- Or use the table function explicitly
SELECT * FROM read_lastra('data.lastra');

-- Temporal filter (row group pushdown)
SELECT * FROM 'daily_btc.lastra'
WHERE ts BETWEEN 1711152000000 AND 1711155600000;

-- Column selection
SELECT ts, close FROM 'ohlcv.lastra' LIMIT 100;

-- Validate the loaded extension version
SELECT lastra_version();
```

## Supported codecs

| Codec | Type | Description |
|-------|------|-------------|
| DELTA_VARINT | BIGINT | Timestamps (delta-of-delta + zigzag varint) |
| ALP | DOUBLE | Adaptive lossless floating-point |
| GORILLA | DOUBLE | XOR compression (Facebook VLDB 2015) |
| PONGO | DOUBLE | Decimal-aware erasure + Gorilla XOR |
| RAW | BIGINT/DOUBLE | Uncompressed |
| VARLEN | VARCHAR | Variable-length strings |
| VARLEN_ZSTD | VARCHAR | ZSTD-compressed strings |

## Build

```bash
make release    # clones DuckDB, builds extension
make test       # runs SQL tests
```

Requires: CMake 3.12+, C++17 compiler.

## Architecture

The extension registers:
- `read_lastra(path)` — table function that reads `.lastra` files
- Replacement scan — `FROM 'file.lastra'` works without explicit function call

Row groups with per-group timestamp statistics enable predicate pushdown: row groups outside the query window are skipped entirely.

## License

Apache-2.0
