cmake_minimum_required(VERSION 3.31)

project(MultiConfigContractLauncher LANGUAGES CXX)

find_package(ForgeContract CONFIG REQUIRED)

forge_add_contract_project(
   configuration_guest
   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/guest"
   BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/configuration.guest"
   CONTRACT configuration
)
get_target_property(
   _configuration_wasm configuration_guest FORGE_CONTRACT_WASM_FILE
)
get_target_property(
   _configuration_abi configuration_guest FORGE_CONTRACT_ABI_FILE
)
get_target_property(
   _configuration_manifest configuration_guest FORGE_CONTRACT_MANIFEST_FILE
)
file(
   GENERATE
   OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/artifact-properties-$<CONFIG>.txt"
   CONTENT
      "${_configuration_wasm}\n${_configuration_abi}\n${_configuration_manifest}\n"
)
