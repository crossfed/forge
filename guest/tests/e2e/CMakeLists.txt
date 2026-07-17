cmake_minimum_required(VERSION 3.31)

project(ForgeContractE2ETests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT FORGE_CONTRACT_TEST_WASM OR NOT EXISTS "${FORGE_CONTRACT_TEST_WASM}")
   message(FATAL_ERROR "FORGE_CONTRACT_TEST_WASM must name a built contract")
endif()
if(NOT FORGE_CONTRACT_TEST_ALLOCATOR_WASM OR NOT EXISTS "${FORGE_CONTRACT_TEST_ALLOCATOR_WASM}")
   message(FATAL_ERROR "FORGE_CONTRACT_TEST_ALLOCATOR_WASM must name the allocator contract")
endif()
if(NOT FORGE_CONTRACT_TEST_LEGACY_WASM OR NOT EXISTS "${FORGE_CONTRACT_TEST_LEGACY_WASM}")
   message(FATAL_ERROR "FORGE_CONTRACT_TEST_LEGACY_WASM must name the legacy contract")
endif()
if(NOT FORGE_CONTRACT_TEST_DB_WASM OR NOT EXISTS "${FORGE_CONTRACT_TEST_DB_WASM}")
   message(FATAL_ERROR "FORGE_CONTRACT_TEST_DB_WASM must name the database intrinsic contract")
endif()
if(NOT FORGE_CONTRACT_TEST_DB_HOST_WASM OR NOT EXISTS "${FORGE_CONTRACT_TEST_DB_HOST_WASM}")
   message(FATAL_ERROR "FORGE_CONTRACT_TEST_DB_HOST_WASM must name the executable database host contract")
endif()

find_package(Forge CONFIG REQUIRED COMPONENTS asio chain_protocol db_core db_object exceptions raw vm_wasm)
find_package(Boost REQUIRED)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../host" "${CMAKE_CURRENT_BINARY_DIR}/host")

add_executable(forge_contract_e2e_tests contract_tests.cpp)
target_link_libraries(
   forge_contract_e2e_tests
   PRIVATE
      Forge::forge_chain_protocol
      Forge::forge_raw
      Forge::forge_vm_wasm
      forge_contract_test_host
      Boost::headers
)
target_compile_definitions(
   forge_contract_e2e_tests
   PRIVATE
      FORGE_CONTRACT_TEST_WASM="${FORGE_CONTRACT_TEST_WASM}"
      FORGE_CONTRACT_TEST_ALLOCATOR_WASM="${FORGE_CONTRACT_TEST_ALLOCATOR_WASM}"
      FORGE_CONTRACT_TEST_LEGACY_WASM="${FORGE_CONTRACT_TEST_LEGACY_WASM}"
      FORGE_CONTRACT_TEST_DB_WASM="${FORGE_CONTRACT_TEST_DB_WASM}"
      FORGE_CONTRACT_TEST_DB_HOST_WASM="${FORGE_CONTRACT_TEST_DB_HOST_WASM}"
      FORGE_CONTRACT_TEST_DB_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/../tooling/spring_db_intrinsics.txt"
)

enable_testing()
add_test(NAME forge_contract_e2e_tests COMMAND forge_contract_e2e_tests)
