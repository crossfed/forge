cmake_minimum_required(VERSION 3.31)

project(ForgeContractCompatibilityCorpus LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ForgeContract CONFIG REQUIRED)
find_package(Forge CONFIG REQUIRED COMPONENTS asio chain_protocol crypto db_core db_object exceptions raw vm_wasm)
find_package(Boost REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(_spring "${CMAKE_CURRENT_SOURCE_DIR}/spring")
set(_eosio "${CMAKE_CURRENT_SOURCE_DIR}/eosio")

forge_add_contract(
   spring_eosio_boot
   CONTRACT eosio.boot
   SOURCES "${_spring}/contracts/eosio.boot/eosio.boot.cpp"
)
forge_add_contract(
   spring_eosio_token
   CONTRACT eosio.token
   SOURCES "${_spring}/contracts/eosio.token/eosio.token.cpp"
)
forge_add_contract(
   spring_eosio_msig
   CONTRACT eosio.msig
   SOURCES "${_spring}/contracts/eosio.msig/eosio.msig.cpp"
)
forge_add_contract(
   spring_eosio_wrap
   CONTRACT eosio.wrap
   SOURCES "${_spring}/contracts/eosio.wrap/eosio.wrap.cpp"
)
forge_add_contract(
   spring_eosio_system
   CONTRACT eosio.system
   SOURCES
      "${_spring}/contracts/eosio.system/eosio.system.cpp"
      "${_spring}/contracts/eosio.system/peer_keys.cpp"
   DISPATCH_SOURCE "${_spring}/contracts/eosio.system/eosio.system.cpp"
   INCLUDE_DIRECTORIES
      "${_spring}/contracts/eosio.system"
      "${_spring}/contracts/eosio.token"
)
forge_add_contract(
   spring_test_api
   CONTRACT test_api
   SOURCES "${_spring}/test-contracts/test_api/test_api.cpp"
)
forge_add_contract(
   spring_test_api_db
   CONTRACT test_api_db
   SOURCES "${_spring}/test-contracts/test_api_db/test_api_db.cpp"
)
forge_add_contract(
   spring_test_api_multi_index
   CONTRACT test_api_multi_index
   SOURCES "${_spring}/test-contracts/test_api_multi_index/test_api_multi_index.cpp"
)
forge_add_contract(
   eosio_bios
   CONTRACT eosio.bios
   SOURCES "${_eosio}/contracts/eosio.bios/src/eosio.bios.cpp"
   INCLUDE_DIRECTORIES "${_eosio}/contracts/eosio.bios/include"
)
forge_add_contract(
   eosio_boot
   CONTRACT eosio.boot
   SOURCES "${_eosio}/contracts/eosio.boot/src/eosio.boot.cpp"
   INCLUDE_DIRECTORIES "${_eosio}/contracts/eosio.boot/include"
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
      Forge::forge_crypto
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
