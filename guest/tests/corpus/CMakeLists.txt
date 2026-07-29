cmake_minimum_required(VERSION 3.31)

project(ForgeContractCompatibilityCorpus LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ForgeContract CONFIG REQUIRED)
find_package(
   Forge CONFIG REQUIRED
   COMPONENTS asio chain_protocol crypto_asymmetric crypto_bls crypto_bn256 crypto_digest crypto_math db_core
              db_object exceptions raw vm_wasm
)
find_package(Boost REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(_spring "${CMAKE_CURRENT_SOURCE_DIR}/spring")
set(_eosio "${CMAKE_CURRENT_SOURCE_DIR}/eosio")

forge_add_contract(
   spring_eosio_boot
   CONTRACT eosio.boot
   SOURCES "${_spring}/contracts/eosio.boot/eosio.boot.cpp"
   HEADERS "${_spring}/contracts/eosio.boot/eosio.boot.hpp"
)
forge_add_contract(
   spring_eosio_token
   CONTRACT eosio.token
   SOURCES "${_spring}/contracts/eosio.token/eosio.token.cpp"
   HEADERS "${_spring}/contracts/eosio.token/eosio.token.hpp"
)
forge_add_contract(
   spring_eosio_msig
   CONTRACT eosio.msig
   SOURCES "${_spring}/contracts/eosio.msig/eosio.msig.cpp"
   HEADERS "${_spring}/contracts/eosio.msig/eosio.msig.hpp"
)
forge_add_contract(
   spring_eosio_wrap
   CONTRACT eosio.wrap
   SOURCES "${_spring}/contracts/eosio.wrap/eosio.wrap.cpp"
   HEADERS "${_spring}/contracts/eosio.wrap/eosio.wrap.hpp"
)

set(_spring_system_root "${CMAKE_CURRENT_BINARY_DIR}/corpus-source/spring-eosio-system")
file(MAKE_DIRECTORY "${_spring_system_root}")
set(
   _spring_system_inputs
   eosio.system.cpp
   peer_keys.cpp
   producer_pay.cpp
   delegate_bandwidth.cpp
   voting.cpp
   exchange_state.cpp
   rex.cpp
   eosio.system.hpp
   exchange_state.hpp
   native.hpp
   peer_keys.hpp
)
foreach(_input IN LISTS _spring_system_inputs)
   configure_file(
      "${_spring}/contracts/eosio.system/${_input}"
      "${_spring_system_root}/${_input}"
      COPYONLY
   )
endforeach()
configure_file(
   "${_spring}/contracts/eosio.token/eosio.token.hpp"
   "${_spring_system_root}/eosio.token.hpp"
   COPYONLY
)
forge_add_contract(
   spring_eosio_system
   CONTRACT eosio.system
   SOURCE_ROOT "${_spring_system_root}"
   SOURCES
      eosio.system.cpp
      peer_keys.cpp
   DISPATCH_SOURCE eosio.system.cpp
   HEADERS
      producer_pay.cpp
      delegate_bandwidth.cpp
      voting.cpp
      exchange_state.cpp
      rex.cpp
      eosio.system.hpp
      exchange_state.hpp
      native.hpp
      peer_keys.hpp
      eosio.token.hpp
)
forge_add_contract(
   spring_test_api
   CONTRACT test_api
   SOURCES "${_spring}/test-contracts/test_api/test_api.cpp"
   HEADERS
      "${_spring}/test-contracts/test_api/test_action.cpp"
      "${_spring}/test-contracts/test_api/test_api.hpp"
      "${_spring}/test-contracts/test_api/test_api_common.hpp"
      "${_spring}/test-contracts/test_api/test_chain.cpp"
      "${_spring}/test-contracts/test_api/test_checktime.cpp"
      "${_spring}/test-contracts/test_api/test_compiler_builtins.cpp"
      "${_spring}/test-contracts/test_api/test_crypto.cpp"
      "${_spring}/test-contracts/test_api/test_datastream.cpp"
      "${_spring}/test-contracts/test_api/test_permission.cpp"
      "${_spring}/test-contracts/test_api/test_print.cpp"
      "${_spring}/test-contracts/test_api/test_transaction.cpp"
      "${_spring}/test-contracts/test_api/test_types.cpp"
      "${_spring}/test-contracts/capi/eosio/types.h"
)
forge_add_contract(
   spring_test_api_db
   CONTRACT test_api_db
   SOURCES "${_spring}/test-contracts/test_api_db/test_api_db.cpp"
   HEADERS "${_spring}/test-contracts/test_api_db/test_api_db.hpp"
)
forge_add_contract(
   spring_test_api_multi_index
   CONTRACT test_api_multi_index
   SOURCES "${_spring}/test-contracts/test_api_multi_index/test_api_multi_index.cpp"
   HEADERS "${_spring}/test-contracts/test_api_multi_index/test_api_multi_index.hpp"
)

set(_eosio_bios_root "${CMAKE_CURRENT_BINARY_DIR}/corpus-source/eosio-bios")
file(MAKE_DIRECTORY "${_eosio_bios_root}/eosio.bios")
configure_file(
   "${_eosio}/contracts/eosio.bios/src/eosio.bios.cpp"
   "${_eosio_bios_root}/eosio.bios.cpp"
   COPYONLY
)
configure_file(
   "${_eosio}/contracts/eosio.bios/include/eosio.bios/eosio.bios.hpp"
   "${_eosio_bios_root}/eosio.bios/eosio.bios.hpp"
   COPYONLY
)
forge_add_contract(
   eosio_bios
   CONTRACT eosio.bios
   SOURCE_ROOT "${_eosio_bios_root}"
   SOURCES eosio.bios.cpp
   HEADERS eosio.bios/eosio.bios.hpp
)

set(_eosio_boot_root "${CMAKE_CURRENT_BINARY_DIR}/corpus-source/eosio-boot")
file(MAKE_DIRECTORY "${_eosio_boot_root}/eosio.boot")
configure_file(
   "${_eosio}/contracts/eosio.boot/src/eosio.boot.cpp"
   "${_eosio_boot_root}/eosio.boot.cpp"
   COPYONLY
)
configure_file(
   "${_eosio}/contracts/eosio.boot/include/eosio.boot/eosio.boot.hpp"
   "${_eosio_boot_root}/eosio.boot/eosio.boot.hpp"
   COPYONLY
)
forge_add_contract(
   eosio_boot
   CONTRACT eosio.boot
   SOURCE_ROOT "${_eosio_boot_root}"
   SOURCES eosio.boot.cpp
   HEADERS eosio.boot/eosio.boot.hpp
)

set(
   _contracts
   spring_eosio_boot
   spring_eosio_token
   spring_eosio_msig
   spring_eosio_wrap
   spring_eosio_system
   spring_test_api
   spring_test_api_db
   spring_test_api_multi_index
   eosio_bios
   eosio_boot
)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../host" "${CMAKE_CURRENT_BINARY_DIR}/host")

add_executable(forge_contract_corpus_tests e2e/contract_tests.cpp)
target_link_libraries(
   forge_contract_corpus_tests
   PRIVATE
      Forge::forge_chain_protocol
      Forge::forge_crypto_digest
      Forge::forge_exceptions
      Forge::forge_raw
      Forge::forge_vm_wasm
      forge_contract_test_host
      Boost::headers
)
target_compile_definitions(
   forge_contract_corpus_tests
   PRIVATE
      FORGE_CONTRACT_CORPUS_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
      FORGE_CONTRACT_CORPUS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_dependencies(forge_contract_corpus_tests ${_contracts})

enable_testing()
add_test(
   NAME forge_contract_corpus_integrity
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check.py"
      integrity
      --root "${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(
   NAME forge_contract_corpus_abi
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check.py"
      abi
      --root "${CMAKE_CURRENT_SOURCE_DIR}"
      --build-dir "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
   NAME forge_contract_corpus_artifacts
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check.py"
      artifacts
      --root "${CMAKE_CURRENT_SOURCE_DIR}"
      --build-dir "${CMAKE_CURRENT_BINARY_DIR}"
      --intrinsics "${ForgeContract_DATA_DIR}/registry/intrinsics.json"
)
add_test(NAME forge_contract_corpus_e2e COMMAND forge_contract_corpus_tests)

add_custom_target(
   forge_contract_corpus_integrity ALL
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check.py"
      integrity
      --root "${CMAKE_CURRENT_SOURCE_DIR}"
   COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/check.py"
      matrix
      --root "${CMAKE_CURRENT_SOURCE_DIR}"
   VERBATIM
)

add_custom_target(
   forge_contract_corpus ALL
   DEPENDS ${_contracts} forge_contract_corpus_integrity forge_contract_corpus_tests
)
