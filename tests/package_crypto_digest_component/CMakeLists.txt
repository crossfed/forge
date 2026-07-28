cmake_minimum_required(VERSION 3.31)

project(forge-package-crypto-digest-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS crypto_digest)

function(forge_add_deleted_digest_fixture source)
   get_filename_component(_case "${source}" NAME_WE)
   add_executable("forge_digest_negative_${_case}" EXCLUDE_FROM_ALL "${source}")
   target_link_libraries("forge_digest_negative_${_case}" PRIVATE Forge::forge_crypto_digest)
endfunction()

include("${CMAKE_CURRENT_SOURCE_DIR}/compile_fail_fixtures.cmake")
foreach(_entry IN LISTS FORGE_DIGEST_COMPILE_FAIL_CASES)
   string(REPLACE "|" ";" _parts "${_entry}")
   list(GET _parts 0 _case)
   forge_add_deleted_digest_fixture("${_case}.cpp")
endforeach()

add_executable(forge_package_crypto_digest_component main.cpp)
target_link_libraries(forge_package_crypto_digest_component PRIVATE Forge::forge_crypto_digest)

enable_testing()
add_test(NAME positive_consumer COMMAND forge_package_crypto_digest_component)
add_test(
   NAME deleted_digest_views
   COMMAND
      ${CMAKE_COMMAND}
      "-DFORGE_PACKAGE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      -P
      "${CMAKE_CURRENT_SOURCE_DIR}/verify_compile_fail.cmake"
)
set_tests_properties(deleted_digest_views PROPERTIES DEPENDS positive_consumer)
