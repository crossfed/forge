cmake_minimum_required(VERSION 3.31)

project(forge-package-blobdb-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS blobdb)

add_executable(forge_package_blobdb_component main.cpp)
target_link_libraries(forge_package_blobdb_component PRIVATE Forge::forge_blobdb)
