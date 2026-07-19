cmake_minimum_required(VERSION 3.31)

project(forge-package-contract-abi-component LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS contract_abi)

add_executable(forge_package_contract_abi_component main.cpp)
target_link_libraries(forge_package_contract_abi_component PRIVATE Forge::forge_contract_abi)
