cmake_minimum_required(VERSION 3.31)

project(forge-package-crypto-digest-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS crypto_digest)

function(forge_expect_dangling_digest_view source)
   get_filename_component(_case "${source}" NAME_WE)
   try_compile(
      _compiled
      SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${source}"
      LINK_LIBRARIES Forge::forge_crypto_digest
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
      COMPILE_DEFINITIONS -Werror=dangling
      OUTPUT_VARIABLE _output
   )
   if(_compiled)
      message(FATAL_ERROR "${_case}: dangling digest view compiled successfully")
   endif()
   if(NOT _output MATCHES "(-Wdangling|-Wreturn-stack-address|temporary whose address)")
      message(FATAL_ERROR "${_case}: failed for an unrelated reason:\n${_output}")
   endif()
endfunction()

forge_expect_dangling_digest_view(dangling_constructor.cpp)
forge_expect_dangling_digest_view(dangling_encoder_result.cpp)
forge_expect_dangling_digest_view(dangling_hash.cpp)
forge_expect_dangling_digest_view(dangling_return.cpp)

add_executable(forge_package_crypto_digest_component main.cpp)
target_link_libraries(forge_package_crypto_digest_component PRIVATE Forge::forge_crypto_digest)
target_compile_options(
   forge_package_crypto_digest_component
   PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Werror=dangling>
)
