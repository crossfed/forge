cmake_minimum_required(VERSION 3.31)

project(forge-package-tooling-manifest-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_DISABLE_FIND_PACKAGE_Clang ON)

find_package(Forge CONFIG REQUIRED COMPONENTS tooling_manifest)

add_executable(forge_package_tooling_manifest_component main.cpp)
target_link_libraries(forge_package_tooling_manifest_component PRIVATE Forge::forge_tooling_manifest)
