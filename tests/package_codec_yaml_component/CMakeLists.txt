cmake_minimum_required(VERSION 3.31)

project(forge-package-codec-yaml-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS codec_yaml)

add_executable(forge_package_codec_yaml_component main.cpp)
target_link_libraries(forge_package_codec_yaml_component PRIVATE Forge::forge_codec_yaml)
