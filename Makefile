.PHONY: all clean debug release test

all: release

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
PROJ_DIR := $(dir $(MKFILE_PATH))
DUCKDB_DIR := $(PROJ_DIR)duckdb
NPROC := $(shell nproc 2>/dev/null || echo 4)

$(DUCKDB_DIR):
	git clone --depth 1 --branch v1.3.0 https://github.com/duckdb/duckdb.git $(DUCKDB_DIR)

release: $(DUCKDB_DIR)
	mkdir -p build/release && \
	cd build/release && \
	cmake -DCMAKE_BUILD_TYPE=Release \
	      -DDUCKDB_EXTENSION_NAMES="lastra" \
	      -DDUCKDB_EXTENSION_LASTRA_PATH="$(PROJ_DIR)" \
	      -DDUCKDB_EXTENSION_LASTRA_SHOULD_LINK=1 \
	      -DDUCKDB_EXTENSION_LASTRA_LOAD_TESTS=1 \
	      ../../duckdb && \
	cmake --build . --config Release -- -j$(NPROC)

debug: $(DUCKDB_DIR)
	mkdir -p build/debug && \
	cd build/debug && \
	cmake -DCMAKE_BUILD_TYPE=Debug \
	      -DDUCKDB_EXTENSION_NAMES="lastra" \
	      -DDUCKDB_EXTENSION_LASTRA_PATH="$(PROJ_DIR)" \
	      -DDUCKDB_EXTENSION_LASTRA_SHOULD_LINK=1 \
	      -DDUCKDB_EXTENSION_LASTRA_LOAD_TESTS=1 \
	      ../../duckdb && \
	cmake --build . --config Debug -- -j$(NPROC)

test: release
	cd build/release && ./test/unittest --test-dir $(PROJ_DIR) "[lastra]"

clean:
	rm -rf build
