cmake_minimum_required(VERSION 3.31)

project(ForgeContractConsumerTest LANGUAGES NONE)

find_package(ForgeContract CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

forge_add_contract(
   hello
   SOURCES hello.cpp
)

forge_add_contract(
   allocatortst
   SOURCES allocator_contract.cpp
)

forge_add_contract(
   legacyhello
   SOURCES legacy_hello.cpp
)

set(_tooling_output "${CMAKE_CURRENT_BINARY_DIR}/tooling")
set(_tooling_build_dir "${CMAKE_CURRENT_BINARY_DIR}/hello.contract")
add_custom_target(
   forge_contract_tooling_fixtures ALL
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../tooling/run_abigen_fixtures.py"
      --abigen "${ForgeContract_ABIGEN}"
      --plugin "${ForgeContract_ATTR_PLUGIN}"
      --sysroot "${ForgeContract_SYSROOT}"
      --include "${ForgeContract_DATA_DIR}/include"
      --build-dir "${_tooling_build_dir}"
      --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/../tooling"
      --output "${_tooling_output}"
   DEPENDS hello
   VERBATIM
)

add_custom_target(
   forge_contract_checker_fixtures ALL
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../tooling/run_checker_fixtures.py"
      --checker "${ForgeContract_CHECK}"
      --clang "${ForgeContract_CLANG}"
      --wasm "${CMAKE_CURRENT_BINARY_DIR}/hello.wasm"
      --abi "${CMAKE_CURRENT_BINARY_DIR}/hello.abi"
      --imports "${ForgeContract_DATA_DIR}/registry/intrinsics.json"
      --manifest "${CMAKE_CURRENT_BINARY_DIR}/hello.contract.json"
      --output "${CMAKE_CURRENT_BINARY_DIR}/checker"
   DEPENDS hello
   VERBATIM
)

add_custom_target(
   forge_contract_reproducibility ALL
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../tooling/run_reproducibility.py"
      --cmake "${CMAKE_COMMAND}"
      --package "${ForgeContract_DIR}"
      --source "${CMAKE_CURRENT_SOURCE_DIR}/hello.cpp"
      --output "${CMAKE_CURRENT_BINARY_DIR}/reproducibility"
   DEPENDS hello
   VERBATIM
)
