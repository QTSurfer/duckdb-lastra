cmake_minimum_required(VERSION 3.12)

set(TARGET_NAME lastra)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

include_directories(src/include)

set(EXTENSION_SOURCES
    src/lastra_extension.cpp
    src/lastra_reader.cpp
    src/codec/delta_varint.cpp
    src/codec/alp.cpp
    src/codec/gorilla.cpp
    src/codec/pongo.cpp
    src/codec/raw.cpp
    src/codec/varlen.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

target_link_libraries(${EXTENSION_NAME} duckdb_zstd)
target_link_libraries(${LOADABLE_EXTENSION_NAME} duckdb_zstd)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
