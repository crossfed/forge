cmake_minimum_required(VERSION 3.31)

project(ProductGuest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ForgeContract CONFIG REQUIRED)

add_subdirectory(../libraries/chain product-chain)
add_subdirectory(libraries/storage)
add_subdirectory(libraries/revision)
add_subdirectory(libraries/unrelated)

forge_add_contract(
   product
   SOURCES entry.cpp
   LIBRARIES product_contract_revision
)
