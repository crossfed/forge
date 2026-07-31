cmake_minimum_required(VERSION 3.31)

project(MultiConfigContractLauncher LANGUAGES CXX)

find_package(ForgeContract CONFIG REQUIRED)

forge_add_contract_project(
   configuration_guest
   SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/guest"
   BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/configuration.guest"
   CONTRACT configuration
)
