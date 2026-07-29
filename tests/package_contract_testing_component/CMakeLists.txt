cmake_minimum_required(VERSION 3.31)

project(forge-package-contract-testing-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_DISABLE_FIND_PACKAGE_Clang ON)

find_package(Forge CONFIG REQUIRED COMPONENTS contract_testing)

add_executable(forge_package_contract_testing_component main.cpp)
target_link_libraries(
   forge_package_contract_testing_component
   PRIVATE Forge::forge_contract_testing
)
