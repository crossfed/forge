cmake_minimum_required(VERSION 3.31)

project(forge-package-codec-base58-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS codec_base58)

add_executable(forge_package_codec_base58_component main.cpp)
target_link_libraries(forge_package_codec_base58_component PRIVATE Forge::forge_codec_base58)
