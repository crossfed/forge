cmake_minimum_required(VERSION 3.31)

project(forge_vendor_compile_policy_test LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_property(_forge_generator_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_forge_generator_is_multi_config)
   file(WRITE "${CMAKE_BINARY_DIR}/generator-is-multi-config.txt" "true\n")
else()
   file(WRITE "${CMAKE_BINARY_DIR}/generator-is-multi-config.txt" "false\n")
endif()

if(MSVC)
   set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Od")
   set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od")
   add_compile_options("$<$<CONFIG:Debug>:/fsanitize=address>")
else()
   set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
   set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
   add_compile_options("$<$<CONFIG:Debug>:-fsanitize=undefined>")
endif()

include("${FORGE_SOURCE_DIR}/cmake/ForgeVendorCompilePolicy.cmake")

add_library(vendor_c STATIC vendor_c.c)
add_library(vendor_cxx STATIC vendor_cxx.cpp)
add_library(forge_owned STATIC forge_owned.cpp)

forge_apply_vendored_implementation_policy(vendor_c)
forge_apply_vendored_implementation_policy(vendor_cxx)
