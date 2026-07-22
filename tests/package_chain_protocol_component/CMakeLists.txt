cmake_minimum_required(VERSION 3.31)

project(forge-package-chain-protocol-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS chain_protocol)

add_executable(forge_package_chain_protocol_component main.cpp producer_schedule.cpp transaction_reexport.cpp)
target_link_libraries(forge_package_chain_protocol_component PRIVATE Forge::forge_chain_protocol)
