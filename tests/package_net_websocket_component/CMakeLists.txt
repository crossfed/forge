cmake_minimum_required(VERSION 3.31)

project(forge-package-net-websocket-component LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS net_websocket)

add_executable(forge_package_net_websocket_component main.cpp)
target_link_libraries(forge_package_net_websocket_component PRIVATE Forge::forge_net_websocket)
