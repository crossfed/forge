cmake_minimum_required(VERSION 3.31)

project(forge-package-plugins-db-store LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Forge CONFIG REQUIRED COMPONENTS plugins_db_store)

add_executable(forge_package_plugins_db_store main.cpp)
target_link_libraries(forge_package_plugins_db_store PRIVATE Forge::forge_plugins_db_store)
