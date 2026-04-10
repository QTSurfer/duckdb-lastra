.PHONY: all clean debug release test

all: release

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
PROJ_DIR := $(dir $(MKFILE_PATH))

# DuckDB source (clone if missing)
DUCKDB_DIR := $(PROJ_DIR)duckdb

$(DUCKDB_DIR):
	git clone --depth 1 --branch v1.3.0 https://github.com/duckdb/duckdb.git $(DUCKDB_DIR)

release: $(DUCKDB_DIR)
	mkdir -p build/release && \
	cd build/release && \
	cmake -DCMAKE_BUILD_TYPE=Release \
	      -DEXTENSION_STATIC_BUILD=1 \
	      -DDUCKDB_EXTENSION_NAMES="lastra" \
	      -DDUCKDB_EXTENSION_LASTRA_PATH="$(PROJ_DIR)" \
	      -DDUCKDB_EXTENSION_LASTRA_SHOULD_LINK=1 \
	      ../../duckdb && \
	cmake --build . --config Release -- -j$(shell nproc)

debug: $(DUCKDB_DIR)
	mkdir -p build/debug && \
	cd build/debug && \
	cmake -DCMAKE_BUILD_TYPE=Debug \
	      -DEXTENSION_STATIC_BUILD=1 \
	      -DDUCKDB_EXTENSION_NAMES="lastra" \
	      -DDUCKDB_EXTENSION_LASTRA_PATH="$(PROJ_DIR)" \
	      -DDUCKDB_EXTENSION_LASTRA_SHOULD_LINK=1 \
	      ../../duckdb && \
	cmake --build . --config Debug -- -j$(shell nproc)

test: release
	./build/release/test/unittest --test-dir . "[lastra]"

clean:
	rm -rf build
