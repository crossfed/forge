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

find_package(Forge CONFIG REQUIRED COMPONENTS chain_protocol raw vm_wasm)
find_package(Boost REQUIRED)

add_executable(forge_contract_e2e_tests contract_tests.cpp)
target_link_libraries(
   forge_contract_e2e_tests
   PRIVATE
      Forge::forge_chain_protocol
      Forge::forge_raw
      Forge::forge_vm_wasm
      Boost::headers
)
target_compile_definitions(
   forge_contract_e2e_tests
   PRIVATE
      FORGE_CONTRACT_TEST_WASM="${FORGE_CONTRACT_TEST_WASM}"
      FORGE_CONTRACT_TEST_ALLOCATOR_WASM="${FORGE_CONTRACT_TEST_ALLOCATOR_WASM}"
      FORGE_CONTRACT_TEST_LEGACY_WASM="${FORGE_CONTRACT_TEST_LEGACY_WASM}"
)

enable_testing()
add_test(NAME forge_contract_e2e_tests COMMAND forge_contract_e2e_tests)
