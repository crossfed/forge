cmake_minimum_required(VERSION 3.31)

project(forge-package-chain-savanna-values-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS chain_savanna_values)

add_executable(forge_package_chain_savanna_values_component main.cpp)
target_link_libraries(
   forge_package_chain_savanna_values_component PRIVATE
      Forge::forge_chain_savanna_values
)
