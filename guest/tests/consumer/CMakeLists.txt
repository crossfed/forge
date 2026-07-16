cmake_minimum_required(VERSION 3.31)

project(ForgeContractConsumerTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ForgeContract CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(
   forge_contract_host_intrinsic_fixture
   OBJECT
      "${CMAKE_CURRENT_SOURCE_DIR}/../tooling/host_intrinsics.cpp"
)
target_include_directories(
   forge_contract_host_intrinsic_fixture
   PRIVATE
      "${ForgeContract_DATA_DIR}/include"
)

forge_add_contract(
   hello
   SOURCES hello.cpp
)

forge_add_contract(
   minimal
   SOURCES minimal.cpp
)

forge_add_contract(
   multisource
   SOURCES multi_source_helper.cpp multi_source.cpp
   DISPATCH_SOURCE multi_source.cpp
)

forge_add_contract(
   allocatortst
   SOURCES allocator_contract.cpp
)

forge_add_contract(
   legacyhello
   SOURCES legacy_hello.cpp
)

set(_db_intrinsics_wasm "${CMAKE_CURRENT_BINARY_DIR}/dbintrinsics.wasm")
add_custom_target(
   forge_contract_database_intrinsic_fixtures ALL
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../tooling/run_intrinsic_fixtures.py"
      --manifest "${ForgeContract_DATA_DIR}/registry/intrinsics.json"
      --golden "${CMAKE_CURRENT_SOURCE_DIR}/../tooling/spring_db_intrinsics.txt"
      --eosio-db-header "${ForgeContract_DATA_DIR}/include/eosio/db.h"
      --cdt-fixture "${CMAKE_CURRENT_SOURCE_DIR}/../tooling/cdt/capi/db.c"
   COMMAND
      "${ForgeContract_CLANG}"
      --target=wasm32
      --sysroot=${ForgeContract_SYSROOT}
      -x c
      -ffreestanding
      -nostdlib
      -fno-ident
      -mcpu=mvp
      -I${ForgeContract_DATA_DIR}/include
      -Wl,--no-entry
      -Wl,--export=test_db
      -Wl,--allow-undefined
      -Wl,--strip-all
      "${CMAKE_CURRENT_SOURCE_DIR}/../tooling/cdt/capi/db.c"
      -o "${_db_intrinsics_wasm}"
   BYPRODUCTS "${_db_intrinsics_wasm}"
   DEPENDS forge_contract_host_intrinsic_fixture
   VERBATIM
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
