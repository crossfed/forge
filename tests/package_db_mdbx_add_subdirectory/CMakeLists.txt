cmake_minimum_required(VERSION 3.31)

project(forge-db-mdbx-add-subdirectory LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED FORGE_SOURCE_DIR)
   message(FATAL_ERROR "FORGE_SOURCE_DIR is required")
endif()

set(FORGE_ENABLE_MDBX ON CACHE BOOL "" FORCE)
set(FORGE_ENABLE_ROCKSDB OFF CACHE BOOL "" FORCE)
set(FORGE_ENABLE_TUI OFF CACHE BOOL "" FORCE)
set(FORGE_ENABLE_VENDOR_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${FORGE_SOURCE_DIR}" forge)

add_executable(forge_db_mdbx_add_subdirectory main.cpp)
target_link_libraries(forge_db_mdbx_add_subdirectory PRIVATE forge_db_mdbx)
