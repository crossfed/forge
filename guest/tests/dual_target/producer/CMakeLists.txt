cmake_minimum_required(VERSION 3.31)

project(ProductProtocol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(
   Forge CONFIG REQUIRED
   COMPONENTS chain_protocol contract_testing crypto_digest raw vm_wasm
)
find_package(ForgeContract CONFIG REQUIRED)

add_subdirectory(libraries/chain)

add_executable(product_protocol_host_tests host_tests.cpp)
target_link_libraries(
   product_protocol_host_tests
   PRIVATE
      product_chain_protocol
      Forge::forge_chain_protocol
      Forge::forge_raw
)

forge_add_contract_project(
   product_guest
   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/guest"
   BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/product.guest"
   CONTRACT product
)
get_target_property(
   _product_wasm product_guest FORGE_CONTRACT_WASM_FILE
)

add_executable(product_protocol_vm_tests vm_tests.cpp)
target_compile_definitions(
   product_protocol_vm_tests
   PRIVATE PRODUCT_PROTOCOL_WASM="${_product_wasm}"
)
target_link_libraries(
   product_protocol_vm_tests
   PRIVATE
      product_chain_protocol
      Forge::forge_chain_protocol
      Forge::forge_contract_testing
      Forge::forge_raw
)
add_dependencies(product_protocol_vm_tests product_guest)
