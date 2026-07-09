cmake_minimum_required(VERSION 3.31)

project(forge-package-api-p2p-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS api_p2p)

add_executable(forge_package_api_p2p_component main.cpp)
target_link_libraries(forge_package_api_p2p_component PRIVATE Forge::forge_api_p2p)
