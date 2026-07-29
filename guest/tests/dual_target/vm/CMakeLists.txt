cmake_minimum_required(VERSION 3.31)

project(ProductProtocolVmTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT PRODUCT_PROTOCOL_WASM OR NOT EXISTS "${PRODUCT_PROTOCOL_WASM}")
   message(FATAL_ERROR "PRODUCT_PROTOCOL_WASM must name the built product contract")
endif()
find_package(
   Forge CONFIG REQUIRED
   COMPONENTS
      chain_protocol
      contract_testing
      raw
)
find_package(ProductProtocol CONFIG REQUIRED)
find_package(Boost REQUIRED)

get_property(_imported_targets DIRECTORY PROPERTY IMPORTED_TARGETS)
foreach(_imported_target IN LISTS _imported_targets)
   if(_imported_target MATCHES "^Boost::")
      set_property(TARGET ${_imported_target} PROPERTY SYSTEM FALSE)
   endif()
endforeach()

add_executable(product_protocol_vm_tests vm_tests.cpp)
set_target_properties(product_protocol_vm_tests PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)
target_compile_definitions(
   product_protocol_vm_tests
   PRIVATE PRODUCT_PROTOCOL_WASM="${PRODUCT_PROTOCOL_WASM}"
)
target_link_libraries(
   product_protocol_vm_tests
   PRIVATE
      Product::protocol
      Forge::forge_chain_protocol
      Forge::forge_contract_testing
      Forge::forge_raw
)
