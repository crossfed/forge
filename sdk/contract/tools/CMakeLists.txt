cmake_minimum_required(VERSION 3.31)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
   project(ForgeContractTools LANGUAGES CXX)
   include(GNUInstallDirs)
   set(CMAKE_CXX_STANDARD 23)
   set(CMAKE_CXX_STANDARD_REQUIRED ON)
   set(CMAKE_CXX_EXTENSIONS OFF)
   find_package(Clang 22.1 CONFIG REQUIRED)
   find_package(Forge CONFIG REQUIRED COMPONENTS chain_protocol codec_json crypto vm_wasm)
endif()

add_library(forge_attr_plugin MODULE attr_plugin.cpp)
target_link_libraries(forge_attr_plugin PRIVATE clang-cpp LLVM)
target_include_directories(forge_attr_plugin SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_compile_definitions(forge_attr_plugin PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(
   forge_attr_plugin PROPERTIES
   PREFIX ""
   OUTPUT_NAME "forge-attr-plugin"
)

add_executable(forge-abigen abigen.cpp)
target_include_directories(forge-abigen SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_compile_definitions(forge-abigen PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(
   forge-abigen PRIVATE
      clang-cpp
      LLVM
      Forge::forge_chain_protocol
      Forge::forge_codec_json
)

add_executable(forge-contract-check contract_check.cpp)
target_link_libraries(
   forge-contract-check PRIVATE Forge::forge_chain_protocol Forge::forge_codec_json Forge::forge_vm_wasm
)

add_executable(forge-contract-manifest contract_manifest.cpp)
target_include_directories(
   forge-contract-manifest PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/../registry/include"
)
target_link_libraries(
   forge-contract-manifest PRIVATE
      Forge::forge_codec_json
      Forge::forge_crypto
      Forge::forge_vm_wasm
)

foreach(_target forge_attr_plugin forge-abigen forge-contract-check forge-contract-manifest)
   target_compile_options(
      ${_target}
      PRIVATE
         "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
         "-fdebug-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=."
         "-fdebug-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=."
   )
endforeach()

if(APPLE)
   set(_forge_contract_executable_rpath "@loader_path/../lib")
   set(_forge_contract_plugin_rpath "@loader_path/..")
elseif(UNIX)
   set(_forge_contract_executable_rpath "\$ORIGIN/../lib")
   set(_forge_contract_plugin_rpath "\$ORIGIN/..")
endif()
if(DEFINED _forge_contract_executable_rpath)
   set_target_properties(
      forge-abigen forge-contract-check forge-contract-manifest
      PROPERTIES
         BUILD_WITH_INSTALL_RPATH ON
         INSTALL_RPATH "${_forge_contract_executable_rpath}"
   )
   set_target_properties(
      forge_attr_plugin
      PROPERTIES
         BUILD_WITH_INSTALL_RPATH ON
         INSTALL_RPATH "${_forge_contract_plugin_rpath}"
   )
endif()

install(TARGETS forge-abigen forge-contract-check forge-contract-manifest RUNTIME DESTINATION bin)
install(TARGETS forge_attr_plugin LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/forge-contract")
