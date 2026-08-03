cmake_minimum_required(VERSION 3.31)

project(forge-xml-add-subdirectory LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED FORGE_SOURCE_DIR)
   message(FATAL_ERROR "FORGE_SOURCE_DIR is required")
endif()

set(FORGE_ENABLE_ROCKSDB OFF CACHE BOOL "" FORCE)
set(FORGE_ENABLE_TUI OFF CACHE BOOL "" FORCE)
set(FORGE_ENABLE_VENDOR_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${FORGE_SOURCE_DIR}" forge)

if(NOT TARGET Forge::forge_raw)
   message(FATAL_ERROR "Forge build-tree package must expose Forge::forge_raw")
endif()
get_target_property(forge_raw_alias Forge::forge_raw ALIASED_TARGET)
if(NOT "${forge_raw_alias}" STREQUAL "forge_raw")
   message(FATAL_ERROR "Forge::forge_raw must alias the build-tree forge_raw target")
endif()

add_executable(forge_codec_xml_add_subdirectory main.cpp)
target_link_libraries(
   forge_codec_xml_add_subdirectory
   PRIVATE
      forge_codec_xml
      Forge::forge_raw
)
